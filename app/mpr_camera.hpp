#pragma once

// app/mpr_camera.hpp — the MPR camera + slice-display transform scaffolding
// (SPEC §3, FR-app.2/3).
//
// Hosts `make3dCamera` (moved from the deleted app/mpr_contour.hpp in V3.8b,
// T11) and the 2D slice-view camera/model pair `makeSliceCamera` /
// `makeSliceModel` (moved from app/mpr_sample.cpp in the T11 review so the
// gate tests drive the EXACT functions the sample composes with — the T11
// user-verified defect was precisely a sample-vs-test camera divergence).
// This is deliberately its own translation unit: unlike app/mpr_slice.hpp
// (kept data/+volume/-only and GL-header-free), these helpers return the plain
// render::Camera struct, so they depend on render/types.hpp. There are still
// no GL calls here (render::Camera is a plain struct; the GL draws belong to
// render::MeshRenderer / render::PlaneRenderer / render::ContourRenderer).
//
// The slice-state ↔ 3D-view interplay: the 3D camera looks at the intersection
// point of the three slice planes — the crosshair
// (sagittalX + 0.5, coronalY + 0.5, transverseZ + 0.5) — so changing the
// slice state moves the 3D view's focus.

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "app/mpr_slice.hpp" // MprSliceState
#include "data/image.hpp"    // data::Image
#include "data/mesh.hpp"     // data::Aabb
#include "render/types.hpp"  // render::Camera (plain struct, no GL)

namespace re::app {

/// The 3D-view camera (FR-app.3): looks at the crosshair from an eye at
/// `crosshair + normalize(1, 1, 1) * distance` with
/// `distance = 1.5 * |bounds.max - bounds.min|` (the box's bounding diagonal).
/// `aspect` is the viewport's width/height ratio for the perspective
/// projection (v1 FOV 45°, near = distance/10, far = distance*10).
render::Camera make3dCamera(const MprSliceState& state,
                            const data::Aabb& meshBounds, float aspect);

/// The 2D slice-view camera (FR-app.2): an orthographic down-Z camera mapping
/// `image`'s pixel space `[0,imgW]x[0,imgH]` onto the full viewport 1:1 in
/// image units (pixel `(px,py)` of the displayed slice sits at display-space
/// coordinate `(px+0.5, py+0.5)` when the viewport matches the image size; the
/// ortho window `[0,imgW]x[0,imgH]` scales uniformly otherwise, exactly like
/// the displayed slice quad itself).
///
/// CAMERA ENCLOSURE CONTRACT (T11 review, user-verified defect 2026-08-24):
/// a GPU contour layer drawn through THIS camera carries geometry at
/// display-frame z = the held voxel-layer coordinate + 0.5 (the slice plane),
/// which for any real volume is FAR above the slice quad's z = 0. The eye
/// therefore stands `kSliceEyeDistance` units back along +Z with
/// `far = 2 * kSliceEyeDistance`, so the ortho clip volume covers display z in
/// `[-kSliceEyeDistance, +kSliceEyeDistance)` — enclosing the slice quad
/// (z = 0) AND every voxel-center coordinate + 0.5 of datasets with axes up to
/// 2 * kSliceEyeDistance voxels (sample_ct: 128 => max 128.5, covered ~8x).
/// A camera that does NOT enclose those z values clips every emitted
/// contour quad away silently (no GL error, no failed Result) — the contour
/// simply vanishes, which is exactly the reported "MPR shows no contour"
/// failure. The XY mapping is independent of the eye distance (pure Z
/// translation + unchanged ortho window), so the slice image renders
/// pixel-identically to the original z = 5 camera.
render::Camera makeSliceCamera(const data::Image& image);

/// The model matrix scaling the shared unit quad [-1,1]^2 onto `image`'s pixel
/// rectangle `[0,imgW]x[0,imgH]` at z = 0, so the whole slice image fills the
/// viewport when viewed through makeSliceCamera(image). Pure math, shared by
/// the sample and the gate tests (the pair MUST move together: the quad and
/// the contour layer share one display frame).
glm::mat4 makeSliceModel(const data::Image& image);

} // namespace re::app
