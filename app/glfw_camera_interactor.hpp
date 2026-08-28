#pragma once

// app/glfw_camera_interactor.hpp — windowing adapter that polls cursor and button state and forwards to scene::CameraController (SPEC §3.1, V5 T9).
//
// The adapter is the only place that may touch windowing handles and overlay capture state. Each frame the caller
// (a sample's renderFrame) invokes update(view) before the live-dims sync; the adapter polls the current context's
// cursor position and mouse buttons via the windowing API, derives the pixel delta since the previous frame, respects
// the overlay's WantCaptureMouse guard (when the overlay wants the mouse, the camera is not touched), forwards the
// delta into the pure-math controller's onMouseDrag / onScroll translators, and finally mutates the view's camera
// through View::mutateCamera so the per-field viewGen bump propagates to the broker's generation cache per SPEC
// §10.4, letting the synchronizer re-translate only the dirty camera fields. Orthographic views are skipped because
// plane + MPR 2D orthographic displays are fixed to the dataset extents and should not orbit on drag. V5 T9.

#include "scene/camera_controller.hpp"
#include "scene/view.hpp"

namespace re::app {

/// Windowing adapter that polls input and drives a scene::View's camera through the pure-math controller.
///
/// The adapter is stateful — it remembers the previous cursor position so it can derive dx/dy without the caller
/// tracking it. Construction is lightweight; update() is a no-op when there is no current context, when the
/// overlay has capture, or when the view's camera is orthographic (the plane/MPR 2D skip). The WantCaptureMouse
/// guard is implemented by reading ImGui::GetIO().WantCaptureMouse directly, matching the task's cited line
/// TASKS.md:126 provenance, so the test can flip that flag and observe the delta going to zero within 1e-6.
class GlfwCameraInteractor {
   public:
    explicit GlfwCameraInteractor(scene::CameraController controller = scene::CameraController{}) noexcept
        : controller_(controller) {}

    explicit GlfwCameraInteractor(scene::CameraController controller, double initialX, double initialY) noexcept
        : controller_(controller), lastX_(initialX), lastY_(initialY), hasLast_(true) {}

    void setController(scene::CameraController c) noexcept { controller_ = c; }
    const scene::CameraController& controller() const noexcept { return controller_; }

    /// Poll the current windowing context's cursor and buttons, respect the overlay capture guard, and mutate
    /// the supplied view's camera if a mapped drag is active. The view's camera is mutated via
    /// View::mutateCamera so viewGen and generation bump correctly and the broker re-translates only dirty
    /// fields. Orthographic cameras are left untouched (the plane and MPR 2D displays keep their fixed slice
    /// framing). No-ops when no current context exists or no button is held.
    void update(scene::View& view) noexcept;

    /// Scroll accumulator exposed for the windowing scroll callback — GLFW delivers scroll via callback, not
    /// poll, so the adapter installs a per-window scroll callback on first update() and stores the pending delta
    /// here until the next update() consumes it via controller.onScroll. Headless tests never create a window so
    /// the accumulator stays at zero and no mutation occurs.
    static double& pendingScroll() noexcept;

    /// Test helper that bypasses windowing polling and drives the controller with explicit synthetic input while
    /// still honouring the WantCaptureMouse guard and the orthographic skip. When wantCaptureMouse is true the
    /// view is left untouched (delta 0 within 1e-6); when false the delta is forwarded into View::mutateCamera
    /// exactly as the real poll path would. Orthographic cameras are left untouched in both paths (plane + MPR 2D
    /// skip). This lets the unit test assert the guard without creating a real window or cursor, while still
    /// exercising the same mutation path.
    void updateForTest(scene::View& view, bool wantCaptureMouse, float dx, float dy,
                       scene::MouseButton button, int mods) noexcept;

    /// Reset the stored cursor so the next update seeds without emitting a delta.
    void reset() noexcept { hasLast_ = false; }

   private:
    scene::CameraController controller_{};
    double lastX_{0.0};
    double lastY_{0.0};
    bool hasLast_{false};
};

} // namespace re::app
