#pragma once

// app/frame_loop.hpp — window-free render helper + FrameLoop (SPEC §3, V5 T3).
//
// V5 T3 decouples the sample harness into three ownership domains:
//   - core/window.hpp stays the visible-window owner (GL 4.6 core context, GLFW);
//   - app/imgui_overlay.hpp owns the Dear ImGui context + GLFW/OpenGL3 backends;
//   - this header owns the render loop that is callable without a Window.
//
// The hard coupling `SampleHarness::initImGui` (which previously called the
// ImGui GLFW init inside sample_harness.cpp) is removed — the overlay module
// is the sole owner of that backend string, so the harness file passes the V5
// T3 gate.
//
// The prerequisite for T4 offscreen is the window-free helper `renderViews`:
// it renders a span of `scene::View` values through a `broker::AppContext`
// (sync → renderAll → presentAll) into any `core::Framebuffer` destination
// (nullptr = window default, otherwise the caller's FBO — e.g. the offscreen
// `ViewTarget` created by `renderOffscreen`). It never touches `core::Window`
// or GLFW, so it is reusable from a hidden `utils::OffscreenContext`.
//
// `FrameLoop` is the optional poll/render/present coordinator for the
// Window path: it holds a borrowed `core::Window` + `broker::AppContext` and
// exposes `poll()`, `render(views)`, `present()` as three separate steps so
// samples can interleave ImGui overlay work between render and present. The
// bounded `SampleHarness::run(maxFrames)` remains the sole public contract;
// `runInteractive()` is the opt-in helper for `until shouldClose()` loops.

#include <span>
#include <vector>

#include "broker/app_context.hpp"
#include "core/framebuffer.hpp"
#include "core/window.hpp"
#include "data/result.hpp"
#include "scene/view.hpp"

namespace re::app {

/// Render `views` through `ctx` into `destination` without a Window.
///
/// The helper is the window-free rendering primitive that T4 `renderOffscreen`
/// will reuse: it calls `ctx.bridge().sync(views, ctx.store())`, then
/// `ctx.bridge().renderAll()`, then `ctx.bridge().presentAll(destination)`.
/// The destination is borrowed for the duration of the call only — nullptr
/// means the window's default framebuffer (like the interactive path), a
/// concrete `core::Framebuffer` is the caller's offscreen FBO (the T4 path).
/// No `core::Window` or GLFW call is made, so this is callable from a hidden
/// offscreen context.
///
/// Returns the first typed error from the three stages, or success.
///
/// @note lifetime: `destination` is a call-scoped borrow — owned by the
/// caller, consumed synchronously, never retained.
data::Result<void> renderViews(std::span<const scene::View> views,
                               broker::AppContext& ctx,
                               core::Framebuffer* /*borrow*/ destination);

/// Window-free overload that preserves the SPEC-described signature
/// `renderViews(span<View>, SceneStore&, Framebuffer&)` for callers that
/// already hold the store handle. The store must be `ctx.store()` — the helper
/// validates identity via address comparison in debug and forwards to the
/// canonical overload.
data::Result<void> renderViews(std::span<const scene::View> views,
                               scene::SceneStore& store,
                               broker::AppContext& ctx,
                               core::Framebuffer* /*borrow*/ destination);

/// Optional coordinator for the interactive Window path (SPEC §3, V5 T3).
///
/// `FrameLoop` holds a borrowed `core::Window` (for `pollEvents` /
/// `swapBuffers` / `shouldClose`) and a borrowed `broker::AppContext` (for
/// `sync` / `renderAll` / `presentAll`). It separates the loop into
/// `poll()` → `render()` → `present()` so the caller (SampleHarness) can run
/// the ImGui overlay between `render` and `present`. The class itself does not
/// own the ImGui context — that is `app::ImGuiOverlay` (V5 T3).
class FrameLoop {
   public:
    /// Construct with borrowed window + context. Both must outlive the loop.
    /// @note lifetime: `window` and `ctx` are non-owning borrows — valid while
    /// this FrameLoop is.
    FrameLoop(::re::core::Window* /*borrow*/ window, broker::AppContext* /*borrow*/ ctx) noexcept
        : window_(window), ctx_(ctx) {}

    /// Poll window/input events (`glfwPollEvents` via the window).
    void poll() noexcept;

    /// Render `views` into the window's default framebuffer via the bridge.
    /// Equivalent to `renderViews(views, *ctx_, nullptr)` but named for the
    /// loop's `render` step (so `poll → render → present` reads as the loop).
    data::Result<void> render(std::span<const scene::View> views);

    /// Render `views` into an explicit destination framebuffer (offscreen loop).
    /// @note lifetime: `destination` is a call-scoped borrow.
    data::Result<void> renderTo(std::span<const scene::View> views,
                                core::Framebuffer* /*borrow*/ destination);

    /// Present the rendered frame (swap buffers on the window). No-op when
    /// window is null (offscreen path — presentation is readback instead).
    void present() noexcept;

    /// True if the window's close flag is set.
    bool shouldClose() const noexcept;

   private:
    ::re::core::Window* /*borrow*/ window_{nullptr};
    broker::AppContext* /*borrow*/ ctx_{nullptr};
};

} // namespace re::app
