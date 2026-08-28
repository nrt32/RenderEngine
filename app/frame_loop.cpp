// app/frame_loop.cpp — FrameLoop + renderViews implementation (SPEC §3, V5 T3): the window-free sync→renderAll→presentAll helper that T4 offscreen will reuse without a Window, plus the FrameLoop poll/render/present coordinator for the interactive Window path and the bounded-default discipline that keeps CI green when the env var is forgotten.

#include "app/frame_loop.hpp"

#include <cassert>

#include "broker/app_context.hpp"
#include "core/window.hpp"

namespace re::app {

data::Result<void> renderViews(std::span<const scene::View> views,
                               broker::AppContext& ctx,
                               core::Framebuffer* /*borrow*/ destination) {
    // The single site for the broker façade sequence that was previously
    // pasted into six ISample::renderFrame implementations and duplicated as
    // app::syncRenderPresent. T3 makes it window-free so T4 offscreen can
    // reuse it: no Window, no GLFW, just sync → renderAll → presentAll.
    auto s = ctx.bridge().sync(views, ctx.store());
    if (s.failed()) {
        return s;
    }
    auto r = ctx.bridge().renderAll();
    if (r.failed()) {
        return r;
    }
    return ctx.bridge().presentAll(destination);
}

data::Result<void> renderViews(std::span<const scene::View> views,
                               scene::SceneStore& store,
                               broker::AppContext& ctx,
                               core::Framebuffer* /*borrow*/ destination) {
    // Debug identity check: the store passed must be the context's store —
    // a mismatched store would sync views against a different backing memory
    // than the bridge's cached generations expect, producing silent stale
    // renders with no typed error. Assert catches wiring bugs in debug.
    (void)store;
    assert(&store == &ctx.store() && "renderViews: store must be ctx.store()");
    return renderViews(views, ctx, destination);
}

void FrameLoop::poll() noexcept {
    if (window_ != nullptr) {
        window_->pollEvents();
    }
}

data::Result<void> FrameLoop::render(std::span<const scene::View> views) {
    if (ctx_ == nullptr) {
        return data::makeError<void>(1, "FrameLoop: no AppContext");
    }
    return renderViews(views, *ctx_, nullptr);
}

data::Result<void> FrameLoop::renderTo(std::span<const scene::View> views,
                                        core::Framebuffer* /*borrow*/ destination) {
    if (ctx_ == nullptr) {
        return data::makeError<void>(1, "FrameLoop: no AppContext");
    }
    return renderViews(views, *ctx_, destination);
}

void FrameLoop::present() noexcept {
    if (window_ != nullptr) {
        window_->swapBuffers();
    }
}

bool FrameLoop::shouldClose() const noexcept {
    if (window_ == nullptr) {
        return false;
    }
    return window_->shouldClose();
}

} // namespace re::app
