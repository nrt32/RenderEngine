// broker/slice_display.cpp — slice-display camera factories (the exact
// pre-move math; see the header for the contracts).

#include "broker/slice_display.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace re::broker {
namespace {

/// The 3D view's vertical field of view: 45 degrees.
constexpr float k3dFovDeg = 45.0f;

/// The camera framing factor: the eye stands this many bounding diagonals
/// from the slice-state crosshair.
constexpr float kCameraDistanceFactor = 1.5f;

} // namespace

scene::Camera makeSliceCamera(float widthUnits, float heightUnits) {
    // Eye far back along +Z so the clip volume encloses both the display
    // plane at z = 0 and any contour's display-frame z (the held voxel
    // coordinate + 0.5) — see kSliceEyeDistance in the header. The XY mapping
    // does not depend on this distance (pure Z translation feeding an
    // unchanged ortho window).
    scene::Camera camera{glm::vec3(0.0f, 0.0f, kSliceEyeDistance),
                         glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)};
    camera.setOrtho(0.0f, widthUnits, 0.0f, heightUnits, 0.1f,
                    2.0f * kSliceEyeDistance);
    return camera;
}

scene::Camera makeSliceCamera(const data::Image& image) {
    return makeSliceCamera(static_cast<float>(image.width()),
                           static_cast<float>(image.height()));
}

scene::Camera make3dCamera(const glm::vec3& crosshairCenter,
                           const data::Aabb& meshBounds, float aspect) {
    // The camera stands kCameraDistanceFactor bounding diagonals from the
    // crosshair along the (1,1,1) diagonal direction and looks at it, so the
    // box (and the slice planes' intersection) stays framed as the slice
    // state changes.
    const float diagonal = glm::length(meshBounds.max - meshBounds.min);
    const float distance = kCameraDistanceFactor * diagonal;
    const glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    const glm::vec3 eye = crosshairCenter + direction * distance;

    scene::Camera camera{eye, crosshairCenter, glm::vec3(0.0f, 1.0f, 0.0f)};
    camera.setPerspective(k3dFovDeg, aspect, distance / 10.0f,
                          distance * 10.0f);
    return camera;
}

render::Camera toRenderCamera(const scene::Camera& camera) {
    render::Camera out;
    out.view = camera.viewMatrix();
    out.proj = camera.projMatrix();
    out.position = camera.eye();
    return out;
}

} // namespace re::broker
