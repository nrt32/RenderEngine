// broker/view_compositor.cpp — ViewCompositor: owns the render-side ReView
// map keyed by the stable (layout, view) identity and drives draw+present.
// Persistence contract (SPEC §10.2/§10.3, landed by the V3.5 persistence
// task): views are looked up by STABLE key, never recreated wholesale, so a
// camera change or 2D→3D toggle keeps the same ReView object (and its GPU
// targets) alive across frames; only genuinely new/removed layout entries
// create/destroy ReViews.
//
// Transparency stage: after a view's own pass (opaque layers, per-view depth
// state), any pending transparent mesh instances are captured through the
// stack's OIT pipeline and composited back-to-front over the opaque result
// INSIDE the same view target — begin() → one drawTransparent per instance →
// end(). The depth test is handed back OFF through REContext::current() (T2
// global per-GL-context, thread_local GLFWwindow* → REContextState) matching
// every established LinkedListOIT flow; per-frame local ctx deleted.

#include "broker/view_compositor.hpp"

#include "core/re_context.hpp"
#include "render/asset_registry.hpp"
#include "render/linked_list_oit.hpp"
#include "render/view.hpp"

namespace re::broker {

render::View* /*borrow*/ ViewCompositor::getView(uint64_t layoutId, uint64_t viewId) noexcept {
    StableKey k = makeStableKey(layoutId, viewId);
    auto it = views_.find(k);
    return it == views_.end() ? nullptr : it->second.get();
}

const render::View* /*borrow*/ ViewCompositor::getView(uint64_t layoutId, uint64_t viewId) const noexcept {
    StableKey k = makeStableKey(layoutId, viewId);
    auto it = views_.find(k);
    return it == views_.end() ? nullptr : it->second.get();
}

render::View* /*borrow*/ ViewCompositor::ensureView(uint64_t layoutId, const scene::View& appView) {
    StableKey k = makeStableKey(layoutId, appView.id);
    auto it = views_.find(k);
    if (it != views_.end()) {
        return it->second.get();
    }
    // Create new ReView with appView rect (physical pixels).
    render::ViewRect r{appView.rect.x, appView.rect.y, appView.rect.w, appView.rect.h};
    auto rv = std::make_unique<render::View>(r);
    // Initialize camera and clipPlane via synchronizer later; just ensure target.
    render::View* /*borrow*/ raw = rv.get(); // returned as a non-owning view over views_
    views_.emplace(k, std::move(rv));
    return raw;
}

void ViewCompositor::pruneLayout(uint64_t layoutId, const std::vector<uint64_t>& activeViewIds) {
    // Build set of active stable keys
    std::unordered_map<uint64_t, bool> active;
    for (auto id : activeViewIds) active[id] = true;
    for (auto it = views_.begin(); it != views_.end();) {
        if (it->first.layoutId == layoutId && active.find(it->first.viewId) == active.end()) {
            it = views_.erase(it);
            continue;
        }
        ++it;
    }
    // Drop the pending transparency stages of pruned views with them.
    for (auto pit = transparentPending_.begin(); pit != transparentPending_.end();) {
        if (pit->first.layoutId == layoutId &&
            active.find(pit->first.viewId) == active.end()) {
            pit = transparentPending_.erase(pit);
        } else {
            ++pit;
        }
    }
}

void ViewCompositor::clear() noexcept {
    views_.clear();
    transparentPending_.clear();
}

void ViewCompositor::setTransparentItems(uint64_t layoutId, uint64_t viewId,
                                         std::vector<render::MeshInstance> items) {
    if (items.empty()) {
        transparentPending_.erase(makeStableKey(layoutId, viewId));
        return;
    }
    transparentPending_[makeStableKey(layoutId, viewId)] = std::move(items);
}

std::size_t ViewCompositor::transparentCount(uint64_t layoutId, uint64_t viewId) const {
    auto it = transparentPending_.find(makeStableKey(layoutId, viewId));
    return it == transparentPending_.end() ? 0u : it->second.size();
}

data::Result<void> ViewCompositor::captureTransparents(StableKey key, render::View* /*borrow*/ rv) {
    auto pending = transparentPending_.find(key);
    if (pending == transparentPending_.end() || pending->second.empty()) {
        return data::Result<void>(data::value);
    }
    if (!stack_ || !stack_->pipeline || !stack_->assets) {
        return data::makeError<void>(
            14,
            "ViewCompositor: view carries transparent instances but no OIT "
            "pipeline is wired in its RenderStack");
    }

    // T4: single-writer discipline — View::render already used REContext::current()
    // for the opaque prologue (bind+viewport+clear+depth). Reuse the SAME instance
    // for OIT so viewport/depth/blend share one ledger (2 layers sharing viewport
    // issue exactly 1 glViewport; no skipped-glEnable bugs). Explicit REContext&
    // passed to begin/end makes the single-writer contract mechanical.
    auto& ctx = core::REContext::current();
    ctx.disableDepthTest();

    render::RenderTarget target;
    target.framebuffer = &rv->target()->framebuffer();
    target.width = static_cast<std::uint32_t>(rv->rect().width);
    target.height = static_cast<std::uint32_t>(rv->rect().height);
    target.clearColor = rv->clearColor();

    const render::Camera camera = rv->camera();
    auto begun = stack_->pipeline->begin(camera, target, ctx);
    if (begun.failed()) {
        return begun;
    }
    for (const render::MeshInstance& inst : pending->second) {
        auto geometry =
            resolveMeshGeometry(stack_->assets, inst.mesh, "view_compositor");
        if (geometry.failed()) {
            return data::makeError<void>(geometry.error().code,
                                         geometry.error().message);
        }
        auto captured = stack_->pipeline->drawTransparent(
            **geometry, inst.material ? inst.material->baseColor()
                                      : glm::vec4(0.0f),
            inst.model, camera);
        if (captured.failed()) {
            return captured;
        }
    }
    return stack_->pipeline->end(camera, target, ctx);
}

data::Result<void> ViewCompositor::renderAll() {
    for (auto& kv : views_) {
        auto* /*borrow*/ rv = kv.second.get(); // @note lifetime: borrowed — owned by views_ map, valid for loop iteration
        if (!rv) continue;
        // T2: global per-GL-context REContext (thread_local GLFWwindow* → REContextState).
        // View::render uses REContext::current() internally so 2 layers sharing
        // viewport within the same GL context issue 1 glViewport (dedup). No
        // per-frame local ctx instance.
        auto r = rv->renderWithEnsure();
        if (r.failed()) return r;
        // OIT capture/composite over this view's opaque result (no-op when
        // nothing pending or no pipeline wired).
        auto c = captureTransparents(kv.first, rv);
        if (c.failed()) return c;
    }
    return data::Result<void>(data::value);
}

data::Result<void> ViewCompositor::presentAll(core::Framebuffer* /*borrow*/ destination) {
    for (auto& kv : views_) {
        auto* /*borrow*/ rv = kv.second.get(); // @note lifetime: borrowed — owned by views_ map, valid for loop iteration
        if (!rv) continue;
        auto r = rv->blitTo(destination);
        if (r.failed()) {
            // If view has no target yet (never rendered), ensure and skip?
            // For persistence tests, presentAll may be called without prior renderAll;
            // we treat missing target as not yet rendered -> skip.
            if (r.error().code == 3) continue;
            return r;
        }
    }
    return data::Result<void>(data::value);
}

} // namespace re::broker
