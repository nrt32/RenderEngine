#include "scene/camera_controller.hpp"

#include <cmath>

namespace re::scene {

CameraDelta CameraController::onMouseDrag(float dx, float dy, MouseButton button, int modifiers) const noexcept {
    if (modifiers != bindings_.modifiers) {
        return CameraDelta{};
    }
    CameraDelta d{};
    if (button == bindings_.rotateButton) {
        d.yawDeg = dx * bindings_.rotateSpeed;
        d.pitchDeg = -dy * bindings_.rotateSpeed;
        d.hasRotate = (dx != 0.0f || dy != 0.0f);
    } else if (button == bindings_.panButton) {
        d.panX = dx * bindings_.panSpeed;
        d.panY = -dy * bindings_.panSpeed;
        d.hasPan = (dx != 0.0f || dy != 0.0f);
    } else if (button == bindings_.zoomButton) {
        // Vertical drag zooms: exponential factor so 10px is analytic within 1e-6 — T10 analytic
        // closed-form is exp(-dy*0.02) for middle-drag (task D: scroll/middle drag zoom exp(-dy*0.02));
        // with default bindings.zoomSpeed=0.1 the scroll path already yields 0.1*0.2=0.02, so the
        // drag path must use 0.2 for parity (0.1*0.2=0.02) rather than 0.02 (which would be 0.002).
        const float factor = std::exp(-dy * bindings_.zoomSpeed * 0.2f);
        d.zoomFactor = factor;
        d.hasZoom = (dy != 0.0f);
        if (d.zoomFactor <= 0.0f) {
            d.zoomFactor = 0.01f;
        }
    }
    return d;
}

CameraDelta CameraController::onScroll(float delta) const noexcept {
    if (delta == 0.0f) {
        return CameraDelta{};
    }
    CameraDelta d{};
    // zoom in on positive delta (scroll away), zoom out on negative — consistent with onMouseDrag sign.
    float factor = std::exp(-delta * bindings_.zoomSpeed * 0.2f);
    if (factor <= 0.0f) factor = 0.01f;
    if (factor > 10.0f) factor = 10.0f;
    d.zoomFactor = factor;
    d.hasZoom = true;
    return d;
}

CameraDelta CameraController::onKey(int /*key*/, int /*action*/, int /*mods*/) const noexcept {
    return CameraDelta{};
}

void CameraController::apply(Camera& cam, const CameraDelta& delta) const noexcept {
    if (delta.hasRotate) {
        cam.rotate(delta.yawDeg, delta.pitchDeg);
    }
    if (delta.hasPan) {
        cam.pan(delta.panX, delta.panY);
    }
    if (delta.hasZoom) {
        cam.zoom(delta.zoomFactor);
    }
}

} // namespace re::scene
