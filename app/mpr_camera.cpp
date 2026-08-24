// app/mpr_camera.cpp — MPR camera + slice-display transform implementations
// (FR-app.2/3; `make3dCamera` moved from the deleted app/mpr_contour.cpp and
// `makeSliceCamera`/`makeSliceModel` moved from app/mpr_sample.cpp in the
// V3.8b T11 review, so the gate tests drive the sample's exact functions).

#include "app/mpr_camera.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace re::app {

namespace {

/// The 3D view's vertical field of view: 45 degrees in radians.
constexpr float k3dFovY = 0.7853981633974483f;

/// The camera framing factor: the eye stands this many bounding diagonals from
/// the slice-state crosshair (documented in make3dCamera).
constexpr float kCameraDistanceFactor = 1.5f;

/// The slice-view eye distance along +Z (see makeSliceCamera): large enough
/// that the ortho clip volume `[-kSliceEyeDistance, +kSliceEyeDistance)`
/// encloses BOTH the slice quad (display z = 0) AND every contour layer's
/// display-frame z (the held voxel-layer coordinate + 0.5 — up to ~128.5 for
/// the 128-voxel axes of sample_ct, covered ~8x; datasets with axes up to
/// 1023 voxels stay enclosed). The T11 user-verified defect was a camera that
/// clipped every contour quad away at z = +35.5/+64.5 (eye z = 5,
/// far = 10): the geometry-shader quads were silently outside the clip
/// volume — no GL error, no failed Result, just no contour pixels.
constexpr float kSliceEyeDistance = 512.0f;

} // namespace

render::Camera make3dCamera(const MprSliceState& state,
                            const data::Aabb& meshBounds, float aspect) {
    // The slice-state crosshair: the intersection point of the three slice
    // planes, in voxel-index units through the voxel centers.
    const glm::vec3 crosshair(static_cast<float>(state.sagittalX) + 0.5f,
                              static_cast<float>(state.coronalY) + 0.5f,
                              static_cast<float>(state.transverseZ) + 0.5f);

    // The camera stands `kCameraDistanceFactor` bounding diagonals from the
    // crosshair along the (1,1,1) diagonal direction and looks at it, so the
    // box (and the slice planes' intersection) stays framed as the slice state
    // changes.
    const float diagonal = glm::length(meshBounds.max - meshBounds.min);
    const float distance = kCameraDistanceFactor * diagonal;
    const glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 1.0f, 1.0f));
    const glm::vec3 eye = crosshair + direction * distance;

    render::Camera camera;
    camera.position = eye;
    camera.view = glm::lookAt(eye, crosshair, glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj =
        glm::perspective(k3dFovY, aspect, distance / 10.0f, distance * 10.0f);
    return camera;
}

render::Camera makeSliceCamera(const data::Image& image) {
    return makeSliceCamera(static_cast<float>(image.width()),
                           static_cast<float>(image.height()));
}

render::Camera makeSliceCamera(float widthUnits, float heightUnits) {
    render::Camera camera;
    // Eye far back along +Z so the clip volume encloses both the display
    // plane at z = 0 and any contour's display-frame z (the held voxel
    // coordinate + 0.5) — see the CAMERA ENCLOSURE CONTRACT in
    // app/mpr_camera.hpp. The XY mapping does not depend on this distance
    // (pure Z translation feeding an unchanged ortho window).
    camera.position = glm::vec3(0.0f, 0.0f, kSliceEyeDistance);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(0.0f, widthUnits, 0.0f, heightUnits, 0.1f,
                             2.0f * kSliceEyeDistance);
    return camera;
}

glm::mat4 makeSliceModel(const data::Image& image) {
    const float halfW = static_cast<float>(image.width()) * 0.5f;
    const float halfH = static_cast<float>(image.height()) * 0.5f;
    return glm::translate(glm::mat4(1.0f), glm::vec3(halfW, halfH, 0.0f)) *
           glm::scale(glm::mat4(1.0f), glm::vec3(halfW, halfH, 1.0f));
}

} // namespace re::app
