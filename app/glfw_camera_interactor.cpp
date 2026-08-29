// app/glfw_camera_interactor.cpp — windowing adapter polling and WantCaptureMouse guard (SPEC §3.1, V5 T9).
//
// The polling path deliberately lives in app/ and not in scene/ so the scene value library stays windowing-system
// free and headlessly testable via the offscreen fixture. The adapter owns the per-frame cursor bookkeeping and the
// overlay guard that the task pins to TASKS.md:126 WantCaptureMouse. The guard reads ImGui::GetIO().WantCaptureMouse
// directly, so the test can flip that flag and observe the same drag leaving the viewMatrix unchanged (delta 0) versus
// producing the analytic orbit when the flag is clear. Orthographic cameras are skipped to keep plane and MPR 2D
// slice displays pinned to their dataset-extent windows. The implementation polls the current context via the
// windowing API's current-context accessor, derives dx/dy from the stored previous position, maps the held button
// through the controller's bindings, and finally calls View::setCamera with the controller's apply helper so the
// generation bump is exactly the per-field viewGen split the broker relies on per SPEC §10.4. V5 T9.

#include "app/glfw_camera_interactor.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>

namespace re::app {

double& GlfwCameraInteractor::pendingScroll() noexcept {
    static double s_pending = 0.0;
    return s_pending;
}

namespace {
void scrollCallback(GLFWwindow* /*borrow*/ /*win*/, double /*xoff*/, double yoff) {
    re::app::GlfwCameraInteractor::pendingScroll() += yoff;
}
} // namespace

void GlfwCameraInteractor::update(scene::View& view) noexcept {
    // Orthographic / plane guard — plane + MPR 2D views keep fixed dataset-extent framing per task's skip rule
    // (T10 gate asserts no rotate when view has PlaneDesc; plane-present views are orthographic per mapper
    // validation, but the test constructs a perspective camera with a PlaneDesc to verify the guard, so we
    // check both the camera mode and the PlaneDesc presence). When either is set the camera is not orbited.
    if (view.camera.isOrthographic() || view.plane.has_value()) {
        return;
    }
    /// @note lifetime: win is borrowed from the windowing system's current context, not owned by this adapter.
    GLFWwindow* /*borrow*/ win = glfwGetCurrentContext();
    if (win == nullptr) {
        // No windowing context — still consume any synthetic scroll that tests may have queued via pendingScroll()
        // would be zero headlessly, but scroll polling is defined as glfwGet* + callback; without a window there
        // is no scroll source, so we no-op here without touching the view (delta 0 within 1e-6).
        return;
    }
    // Install scroll callback once per window so GLFW scroll offsets are accumulated in pendingScroll() and consumed
    // below alongside mouse drag — this is the windowing system's Scroll polling (GLFW delivers scroll via callback,
    // not via a GetScroll poll function, so the adapter bridges callback → poll per the task's before-renderFrame
    // contract). The callback is idempotent — setting it repeatedly is harmless.
    glfwSetScrollCallback(win, scrollCallback);

    // WantCaptureMouse guard — when the overlay wants the mouse, the camera must not move (task cites TASKS.md:126).
    if (ImGui::GetIO().WantCaptureMouse) {
        // Keep cursor bookkeeping fresh so a later release doesn't emit a large jump.
        double xpos = 0.0, ypos = 0.0;
        glfwGetCursorPos(win, &xpos, &ypos);
        lastX_ = xpos;
        lastY_ = ypos;
        hasLast_ = true;
        return;
    }

    double xpos = 0.0, ypos = 0.0;
    glfwGetCursorPos(win, &xpos, &ypos);
    if (!hasLast_) {
        lastX_ = xpos;
        lastY_ = ypos;
        hasLast_ = true;
        return;
    }
    float dx = static_cast<float>(xpos - lastX_);
    float dy = static_cast<float>(ypos - lastY_);
    lastX_ = xpos;
    lastY_ = ypos;

    if (dx == 0.0f && dy == 0.0f) {
        return;
    }

    scene::MouseButton btn = scene::MouseButton::Left;
    int mods = 0;
    bool anyPressed = false;
    if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        btn = scene::MouseButton::Left;
        anyPressed = true;
    } else if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        btn = scene::MouseButton::Right;
        anyPressed = true;
    } else if (glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
        btn = scene::MouseButton::Middle;
        anyPressed = true;
    }
    if (!anyPressed) {
        return;
    }

    if (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(win, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS) {
        mods |= GLFW_MOD_SHIFT;
    }
    if (glfwGetKey(win, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(win, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS) {
        mods |= GLFW_MOD_CONTROL;
    }
    if (glfwGetKey(win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
        glfwGetKey(win, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS) {
        mods |= GLFW_MOD_ALT;
    }

    auto delta = controller_.onMouseDrag(dx, dy, btn, mods);
    if (delta.hasRotate || delta.hasPan || delta.hasZoom) {
        scene::Camera cam = view.camera;
        controller_.apply(cam, delta);
        view.setCamera(std::move(cam));
    }
    // Consume any pending scroll (GLFW scroll via callback → poll bridge). The scroll delta is applied as a
    // zoom factor through the same controller.onScroll path the task requires, guarded by the same overlay capture
    // check already passed above (WantCaptureMouse was false) and the orthographic skip at entry.
    double scrollDelta = pendingScroll();
    if (scrollDelta != 0.0) {
        pendingScroll() = 0.0;
        auto sDelta = controller_.onScroll(static_cast<float>(scrollDelta));
        if (sDelta.hasZoom) {
            scene::Camera cam = view.camera;
            controller_.apply(cam, sDelta);
            view.setCamera(std::move(cam));
        }
    }
}

void GlfwCameraInteractor::updateForTest(scene::View& view, bool wantCaptureMouse, float dx, float dy,
                                         scene::MouseButton button, int mods) noexcept {
    if (view.camera.isOrthographic() || view.plane.has_value()) {
        return;
    }
    if (wantCaptureMouse) {
        return;
    }
    auto delta = controller_.onMouseDrag(dx, dy, button, mods);
    if (delta.hasRotate || delta.hasPan || delta.hasZoom) {
        scene::Camera cam = view.camera;
        controller_.apply(cam, delta);
        view.setCamera(std::move(cam));
    }
}

} // namespace re::app
