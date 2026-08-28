#pragma once

// app/imgui_overlay.hpp — Dear ImGui overlay wrapper (SPEC §3, V5 T3).
//
// V5 T3 decouples the ImGui wiring from `SampleHarness`:
//   - `app::ImGuiOverlay` is the sole owner of the ImGui context + the GLFW +
//     OpenGL3 backend init/shutdown/newFrame/render calls. The init string
//     lives only in the overlay source, so the harness file passes the V5 T3
//     gate and the harness owns no ImGui state.
//   - The overlay is OPTIONAL — `FrameLoop` + `renderViews` work without it.
//     `SampleHarness` constructs an overlay internally for the interactive
//     samples, but offscreen/benchmark code can use `FrameLoop` alone.
//   - The harness calls `overlay.newFrame()` before `ISample::renderFrame` and
//     `overlay.render()` after, matching the previous SampleHarness loop order
//     (ImGui new-frame → sample render → overlay draw → ImGui render → present).

#include <memory>
#include <string>

struct GLFWwindow;

namespace re::app {

class ISample;

/// RAII wrapper for the Dear ImGui context + GLFW/OpenGL3 backends.
///
/// The wrapper is move-only (the ImGui context is global singleton state) and
/// idempotent: `shutdown()` is safe to call from both the harness `run` exit
/// path and the destructor. `init` must be called with the current GL context's
/// window handle after the window is created and made current.
class ImGuiOverlay {
   public:
    ImGuiOverlay() noexcept = default;
    ~ImGuiOverlay();

    ImGuiOverlay(const ImGuiOverlay&) = delete;
    ImGuiOverlay& operator=(const ImGuiOverlay&) = delete;
    ImGuiOverlay(ImGuiOverlay&& other) noexcept;
    ImGuiOverlay& operator=(ImGuiOverlay&& other) noexcept;

    /// Initialise the ImGui context + GLFW/OpenGL3 backends for `window`.
    /// Returns false on failure (logs via spdlog). The GL context must be
    /// current on the calling thread.
    /// @note lifetime: `window` is a call-scoped borrow — the overlay does not
    /// retain it beyond the init call (the GLFW backend stores the handle
    /// internally via its own registry, not this wrapper).
    bool init(GLFWwindow* /*borrow*/ window);

    /// Shut down the ImGui backends + context. Idempotent.
    void shutdown() noexcept;

    /// True when the overlay is initialised and usable.
    bool initialized() const noexcept { return initialized_; }

    /// Begin a new ImGui frame (must be called after `pollEvents` and before
    /// `ISample::renderFrame`). No-op when not initialised.
    void newFrame() noexcept;

    /// Render the ImGui draw data via the OpenGL3 backend (must be called after
    /// the sample's 3D scene render and the overlay window code). No-op when
    /// not initialised.
    void render() noexcept;

    /// Draw the standard sample overlay window (title, frame counter,
    /// per-sample instructions) into the current ImGui frame. Must be called
    /// between `newFrame()` and `render()`.
    void drawSampleOverlay(const ISample& sample, int frame, int maxFrames) noexcept;

   private:
    bool initialized_{false};
};

} // namespace re::app
