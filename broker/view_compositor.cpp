// broker/view_compositor.cpp — ViewCompositor: owns the render-side ReView
// map keyed by the stable (layout, view) identity and drives draw+present.
// Persistence contract (SPEC §10.2/§10.3, landed by the V3.5 persistence
// task): views are looked up by STABLE key, never recreated wholesale, so a
// camera change or 2D→3D toggle keeps the same ReView object (and its GPU
// targets) alive across frames; only genuinely new/removed layout entries
// create/destroy ReViews.

#include "broker/view_compositor.hpp"

#include "core/draw.hpp"
#include "render/view.hpp"

namespace re::broker {

render::View* /*borrow*/ ViewCompositor::getView(uint64_t layoutId, uint64_t viewId) noexcept {
    StableKey k{1, layoutId, viewId};
    auto it = views_.find(k);
    return it == views_.end() ? nullptr : it->second.get();
}

const render::View* /*borrow*/ ViewCompositor::getView(uint64_t layoutId, uint64_t viewId) const noexcept {
    StableKey k{1, layoutId, viewId};
    auto it = views_.find(k);
    return it == views_.end() ? nullptr : it->second.get();
}

render::View* /*borrow*/ ViewCompositor::ensureView(uint64_t layoutId, const scene::View& appView) {
    StableKey k{1, layoutId, appView.id};
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
        } else {
            ++it;
        }
    }
}

void ViewCompositor::clear() noexcept { views_.clear(); }

data::Result<void> ViewCompositor::renderAll() {
    for (auto& kv : views_) {
        auto* rv = kv.second.get();
        if (!rv) continue;
        core::DrawContext ctx;
        auto r = rv->renderWithEnsure(ctx);
        if (r.failed()) return r;
    }
    return data::Result<void>(data::value);
}

data::Result<void> ViewCompositor::presentAll(core::Framebuffer* /*borrow*/ destination) {
    for (auto& kv : views_) {
        auto* rv = kv.second.get();
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
