#pragma once

// broker/slice_display.hpp — RE-side slice-display camera factories
// (FR-app.2/3 display scaffolding).
//
// Formerly app/mpr_camera.*: these helpers BUILD render-side cameras, so they
// belong on the broker side of the ACL — app/ must not name render/ types,
// and broker/ is the only library that sees both scene/ and render/. The
// functions return scene::Camera VALUES (the GL-free app-side currency the
// bridge translates through CameraMapper), so samples configure views with
// them directly and tests convert to a draw-time render::Camera with
// toRenderCamera() where they drive a renderer directly.
//
// The math is byte-identical to the pre-move implementations (same glm calls,
// same constants), which is what preserves the analytic pixel gates:
//
//   - makeSliceCamera: an orthographic down-Z camera whose clip volume
//     ENCLOSES both the displayed plane (display z = 0) AND every voxel-center
//     coordinate + 0.5 a contour/extraction layer can sit at — the enclosure
//     contract below. The XY mapping is independent of the eye distance.
//   - make3dCamera: the MPR 3D crosshair view — eye on the (1,1,1) diagonal
//     from the slice-plane intersection point, 45° perspective.
//
// Ownership/lifetime: pure value construction; nothing is retained.

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "data/image.hpp"
#include "data/mesh.hpp"
#include "render/types.hpp" // render::Camera (broker may include render/)
#include "scene/camera.hpp"

namespace re::broker {

/// The slice-view eye distance along +Z: large enough that the ortho clip
/// volume `[-kSliceEyeDistance, +kSliceEyeDistance)` encloses BOTH the slice
/// quad (display z = 0) AND every contour layer's display-frame z (the held
/// voxel-layer coordinate + 0.5 — up to ~128.5 for the 128-voxel axes of
/// sample_ct, covered ~8x; datasets with axes up to 1023 voxels stay
/// enclosed). A camera that does NOT enclose those z values clips every
/// emitted layer away silently (no GL error, no failed Result) — geometry
/// simply vanishes; this constant is the fix for that defect class.
inline constexpr float kSliceEyeDistance = 512.0f;

/// The 2D slice-view camera over an abstract display rectangle
/// `[0,widthUnits] x [0,heightUnits]`: orthographic window exactly that
/// rectangle, eye `kSliceEyeDistance` back along +Z looking at the origin, so
/// pixel centers land on display-space coordinates when the viewport matches
/// the rectangle 1:1. This is the form the GPU slice-extraction views use
/// (the displayed rectangle spans the dataset's two free axes in voxel-index
/// units; no CPU slice image exists).
scene::Camera makeSliceCamera(float widthUnits, float heightUnits);

/// The same 2D slice-view camera over an image's pixel space
/// `[0,imgW]x[0,imgH]`: pixel `(px,py)` of the displayed slice sits at
/// display-space coordinate `(px+0.5, py+0.5)` when the viewport matches the
/// image size. Delegates to the extent overload with the image dimensions.
scene::Camera makeSliceCamera(const data::Image& image);

/// The 3D-view camera (FR-app.3): looks at `crosshairCenter` (the
/// intersection point of the three slice planes) from an eye at
/// `crosshair + normalize(1,1,1) * distance` with
/// `distance = 1.5 * |bounds.max - bounds.min|` (the bounding diagonal).
/// `aspect` is the viewport's width/height ratio for the 45° perspective
/// projection (near = distance/10, far = distance*10).
scene::Camera make3dCamera(const glm::vec3& crosshairCenter,
                           const data::Aabb& meshBounds, float aspect);

/// The draw-time RE camera for a scene::Camera value: the exact translation
/// CameraMapper performs (view/proj matrices + eye position) without cache or
/// context validation. Tests driving a renderer/view DIRECTLY convert with
/// this; the bridge path uses CameraMapper instead. Kept next to the
/// factories so the sample-vs-test camera pairing cannot drift.
render::Camera toRenderCamera(const scene::Camera& camera);

} // namespace re::broker
