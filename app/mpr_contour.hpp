#pragma once

// app/mpr_contour.hpp — MPR mesh contour overlay + 3D-view camera scaffolding
// (SPEC §3, FR-app.3).
//
// T15 completes the MPR composition (T14, FR-app.2) with the FR-app.3
// capabilities:
//   - the mesh contour overlay: for each slice view, the plane∩mesh
//     cross-section curve is computed from the mesh and that view's slice plane
//     (closed form for the golden box, general triangle-plane intersection for
//     any mesh) and rasterized into the slice image
//     (`meshPlaneContour` / `overlayContour`), and
//   - the 3D rendering view: the mesh rendered by render::MeshRenderer through
//     the `make3dCamera` camera, whose look-at target is the intersection point
//     of the three slice planes (the slice-state "crosshair") — the
//     slice-state ↔ 3D-view camera interplay (changing the slice state moves
//     the 3D view's focus).
//
// Coordinate convention (shared with app/mpr_slice, FR-app.2): the volume and
// the mesh live in one voxel-index coordinate space; voxel (x, y, z) has its
// center at (x+0.5, y+0.5, z+0.5). A slice view's plane passes through the
// centers of the sliced voxel layer (`slicePlane`), and the slice image's pixel
// (px, py) maps to coordinate (px+0.5, py+0.5) on the two free axes of that
// view's plane.
//
// Like app/mpr_slice, this scaffolding is CPU-side (pure math + image
// rasterization, headless-testable). It depends on render/ only for the plain
// render::Camera struct (no GL calls), so the FR-app.3 gate can verify the
// contour pixels and the camera headlessly; the GL draw of the 3D view is
// render::MeshRenderer's job (consumed by the sample).

#include <array>
#include <cstdint>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>

#include "app/mpr_slice.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "render/types.hpp" // render::Camera (plain struct, no GL)
#include "volume/color.hpp"

namespace re::app {

/// The FR-app.3 contour color: pure red (straight RGBA), mapping to the exact
/// RGBA8 bytes (255, 0, 0, 255). The gate asserts that pixels within the 2 px
/// band of the plane∩mesh curve match exactly this color.
inline constexpr volume::RgbaColor kContourColor{1.0f, 0.0f, 0.0f, 1.0f};

/// The plane of one MPR slice view in the shared voxel-index coordinate space:
/// a plane perpendicular to `axis` at `coordinate` (voxel-index units, through
/// the centers of the sliced voxel layer).
struct SlicePlane {
    MprAxis axis;     ///< The axis the plane is perpendicular to.
    float coordinate; ///< The held coordinate (voxel-index units).
};

/// A 2D line segment in a slice view's image pixel space: the projection of one
/// triangle's intersection with the view's plane onto that view's two free
/// axes, in voxel-index units (pixel (px, py) ↔ coordinate (px+0.5, py+0.5)).
using ContourSegment = std::array<glm::vec2, 2>;

/// The plane of the slice view `axis` driven by the slice state `state`: the
/// plane through the centers of the voxel layer `index` —
///   - Transverse: z = transverseZ + 0.5,
///   - Coronal:    y = coronalY + 0.5,
///   - Sagittal:   x = sagittalX + 0.5.
SlicePlane slicePlane(MprAxis axis, const MprSliceState& state);

/// Build the golden box mesh spanning `[min, max]` in voxel-index coordinates:
/// 8 corners and 12 outward-facing CCW triangles (the T11 cube winding
/// generalized to arbitrary bounds). Closed form and deterministic; the box's
/// bounds are integers and slice planes are at half-integer coordinates, so no
/// triangle lies in a slice plane (the FR-app.3 analytic cross-sections are
/// non-degenerate rectangles).
data::Mesh makeBoxMesh(const glm::vec3& min, const glm::vec3& max);

/// Compute the plane∩mesh cross-section curve of `mesh` against `plane` as a
/// list of 2D segments in the slice image's pixel space.
///
/// Each triangle that strictly straddles the plane contributes the segment
/// between its two crossing points, projected onto the view's two free axes
/// (Transverse keeps (x, y), Coronal keeps (x, z), Sagittal keeps (y, z)).
/// Triangles fully on one side, tangent to the plane (crossing at a single
/// point), or lying in the plane contribute no segment (the deterministic
/// choice; the golden box never exhibits these — its faces are at integer
/// coordinates and slice planes at half-integers). For the golden box the
/// returned curve's UNION is exactly the rectangle boundary of the box's
/// cross-section: 8 triangle-segments (the box's 4 side faces contribute 2
/// triangles each) covering the 4 rectangle edges, with two segments meeting
/// at each edge's crossing point with the face diagonal — offset 0.5 off the
/// edge's midpoint (31.5 or 32.5 for the golden box's 32-unit edges, depending
/// on the diagonal), never the midpoint itself — and every endpoint on the
/// rectangle boundary (the closed-form analytic curve FR-app.3 asserts
/// against).
std::vector<ContourSegment> meshPlaneContour(const data::Mesh& mesh,
                                             const SlicePlane& plane);

/// Return a copy of `image` with the contour `curve` overlaid on it: every
/// pixel whose center is within the FR-app.3 2 px band of a segment (Euclidean
/// distance, with a small float-rounding guard) is written with `color` (all
/// four channels replaced). Pixels outside the band keep the source image's
/// bytes, so the slice background is preserved exactly.
data::Image overlayContour(const data::Image& image,
                           const std::vector<ContourSegment>& curve,
                           const volume::RgbaColor& color);

/// The 3D-view camera (the slice-state ↔ 3D-view camera interplay): the camera
/// looks at the intersection point of the three slice planes — the crosshair
/// (sagittalX + 0.5, coronalY + 0.5, transverseZ + 0.5) — from an eye at
/// `crosshair + normalize(1, 1, 1) * distance` with
/// `distance = 1.5 * |bounds.max - bounds.min|` (the box's bounding diagonal).
/// `aspect` is the viewport's width/height ratio for the perspective
/// projection (v1 FOV 45°, near = distance/10, far = distance*10).
/// Changing the slice state moves the crosshair, so the 3D view refocuses on
/// the slice planes' intersection.
render::Camera make3dCamera(const MprSliceState& state,
                            const data::Aabb& meshBounds, float aspect);

} // namespace re::app