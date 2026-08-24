// render/view.cpp — View (ReView) implementation: each screen section owns a
// ViewTarget (its own framebuffer), binds it, and draws its IRenderable item
// list in order; `blitToWindow` then copies every target into its rect of the
// shared window framebuffer. The view never inspects concrete renderer types
// — items are type-erased draw calls — so adding a technique needs no View
// change.

#include "render/view.hpp"

#include "core/draw.hpp"
#include "render/view_target.hpp"

namespace re::render {

View::View(ViewRect rect, glm::vec4 clearColor) : rect_(rect), clearColor_(clearColor) {}

data::Result<void> View::ensureTarget() {
    if (rect_.width <= 0 || rect_.height <= 0) {
        return data::makeError<void>(1, "View: invalid rect size");
    }
    const auto w = static_cast<std::uint32_t>(rect_.width);
    const auto h = static_cast<std::uint32_t>(rect_.height);
    if (!target_.has_value()) {
        auto t = ViewTarget::create(w, h);
        if (t.failed()) {
            return data::makeError<void>(t.error().code, t.error().message);
        }
        target_ = std::move(*t);
        return data::Result<void>(data::value);
    }
    if (target_->width() != w || target_->height() != h) {
        auto r = target_->resize(w, h);
        if (r.failed()) return r;
    }
    return data::Result<void>(data::value);
}

data::Result<void> View::render(core::DrawContext& ctx) {
    if (!target_.has_value()) {
        return data::makeError<void>(2, "View: target not created (call ensureTarget)");
    }
    // Begin the pass through the ONE shared prologue (T17: core::DrawContext::
    // beginPass — the six-call bind+viewport+clear+depth-off+blend-off sequence
    // exists exactly once, in core/draw.hpp). The View is the composition
    // owner: it clears exactly once here, and layers never clear.
    ctx.beginPass(&target_->framebuffer(), target_->width(), target_->height(),
                  clearColor_.r, clearColor_.g, clearColor_.b, clearColor_.a);

    for (auto& item : items_) {
        auto res = item->drawLayer(camera_, ctx);
        if (res.failed()) {
            return res;
        }
    }
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
