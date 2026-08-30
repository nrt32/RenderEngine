// render/view.cpp — View (ReView) implementation: each screen section owns a
// ViewTarget (its own framebuffer), binds it, and draws its IRenderable item
// list in order; `blitToWindow` then copies every target into its rect of the
// shared window framebuffer. The view never inspects concrete renderer types
// — items are type-erased draw calls — so adding a technique needs no View
// change.

#include "render/view.hpp"

#include "core/re_context.hpp"
#include "render/view_target.hpp"

namespace re::render {

View::View(ViewRect rect, glm::vec4 clearColor) : rect_(rect), clearColor_(clearColor) {}

data::Result<void> View::ensureTarget() {
    if (rect_.width <= 0 || rect_.height <= 0) {
        return data::makeError<void>(1, "View: invalid rect size");
    }
    const auto w = static_cast<std::uint32_t>(rect_.width);
    const auto h = static_cast<std::uint32_t>(rect_.height);
    const DepthMode wantedMode =
        depthTest_ ? DepthMode::Enabled : DepthMode::ColorOnly;
    if (!target_.has_value()) {
        auto t = ViewTarget::create(w, h, wantedMode);
        if (t.failed()) {
            return data::makeError<void>(t.error().code, t.error().message);
        }
        target_ = std::move(*t);
        return data::Result<void>(data::value);
    }
    // A depth-mode mismatch recreates the target outright so the new
    // attachment set (with or without the depth texture) is created and
    // asserted complete together: a depth-tested view must draw into a target
    // that physically owns a depth attachment, and a color-only view must not
    // keep a stale one. As with the size path below, only the inner ViewTarget
    // changes — the View object itself persists (persistence contract).
    if (target_->depthMode() != wantedMode) {
        auto t = ViewTarget::create(w, h, wantedMode);
        if (t.failed()) {
            return data::makeError<void>(t.error().code, t.error().message);
        }
        target_ = std::move(*t);
        return data::Result<void>(data::value);
    }
    // Pure size change: reallocate storage at the new rect size, preserving
    // this view's depth mode.
    if (target_->width() != w || target_->height() != h) {
        auto r = target_->resize(w, h);
        if (r.failed()) return r;
    }
    return data::Result<void>(data::value);
}

namespace {
thread_local const std::vector<ReLight>* t_currentLights = nullptr;
thread_local bool t_currentIs2D = false;
} // namespace
const std::vector<ReLight>* currentViewLights() noexcept { return t_currentLights; }
void setCurrentViewLights(const std::vector<ReLight>* l) noexcept { t_currentLights = l; }
bool currentViewIs2D() noexcept { return t_currentIs2D; }
void setCurrentViewIs2D(bool v) noexcept { t_currentIs2D = v; }

data::Result<void> View::render() {
    if (!target_.has_value()) {
        return data::makeError<void>(2, "View: target not created (call ensureTarget)");
    }
    // T2: global per-GL-context REContext (thread_local GLFWwindow* → REContextState).
    // Begin the pass through the ONE shared prologue (bind+viewport+clear+
    // depth-state+blend-off exists exactly once, in core/re_context.hpp). The
    // per-view depthTest flag drives the depth branch: enabled (+ depth cleared to
    // 1.0) for occlusion views, disabled for default color-only painter's-order.
    // View is composition owner: it clears exactly once here, layers never clear.
    // 2 layers sharing viewport within same GL context issue 1 glViewport (dedup
    // via the global current's cache). Per-frame local ctx instances deleted.
    // T15: publish per-View lights to the thread-local currentViewLights slot
    // before the drawLayer loop so MeshRenderer can forward the world-space
    // dirWS to uLightDir once per View (ViewSynchronizer per-field lightsGen
    // already cached the ReLight vector; empty keeps fixed headlight).
    auto& ctx = core::REContext::current();
    ctx.beginPass(&target_->framebuffer(), target_->width(), target_->height(),
                  clearColor_.r, clearColor_.g, clearColor_.b, clearColor_.a,
                  depthTest_);
    t_currentLights = &lights_;
    t_currentIs2D = is2D();
    for (auto& item : items_) {
        auto res = item->drawLayer(camera_);
        if (res.failed()) {
            t_currentLights = nullptr;
            t_currentIs2D = false;
            return res;
        }
    }
    t_currentLights = nullptr;
    t_currentIs2D = false;
    return data::Result<void>(data::value);
}

data::Result<void> View::blitTo(core::Framebuffer* /*borrow*/ destination) const {
    if (!target_.has_value()) {
        return data::makeError<void>(3, "View: target not created (call ensureTarget/render first)");
    }
    // Engine-side blit present: copy ViewTarget's FBO into its pinned window rect.
    const auto& fb = target_->framebuffer();
    const int sw = static_cast<int>(target_->width());
    const int sh = static_cast<int>(target_->height());
    // Source rect is whole target (0,0,w,h); dest is ViewRect.
    return core::blit(fb, 0, 0, sw, sh, destination, rect_.x, rect_.y, rect_.width, rect_.height);
}

} // namespace re::render
