#pragma once

// app/mpr_slice.hpp — MPR layout + slice-state scaffolding (SPEC §3, FR-app.2).
//
// MPR is app-level composition (SPEC §3: "MPR is app-level composition, not a
// module"), and the 2x2 viewport grid + the axis sampling convention are pure
// CPU scaffolding shared by the MPR sample (T14) and its gate tests. This
// header is deliberately GL-free and ImGui-free: it depends only on data/ and
// volume/ so the T14 gate can test the layout constants and the per-axis slice
// sampling headlessly (FR-app.2) without pulling in the harness/GL stack.
//
// The pinned axis convention (SPEC §4 FR-app.2): Transverse = slice at constant
// Z (axial), Coronal = constant Y, Sagittal = constant X. `makeSliceImage`
// samples the volume along exactly that axis and maps each voxel through a
// transfer function to an RGBA slice image (the CPU-side of what the T/C/S
// views display; the shared slice-state/camera scaffolding for the 2D views).
//
// Since V3.8b (T11) this header also hosts the plane∩mesh scaffolding that
// used to live in app/mpr_contour.hpp (deleted): the per-view `SlicePlane`,
// its derivation from the slice state (`slicePlane`), and the golden box mesh
// builder (`makeBoxMesh`). These are pure CPU value helpers (data/ + volume/
// only, still GL-free); the contour itself is now computed ON THE GPU by
// render::ContourRenderer via broker::ContourMapper — see docs/render.md.
//
// The 2x2 viewport grid (SPEC §4 FR-app.2): a 1280x960 window split into four
// 640x480 viewports, with T (top-left), C (top-right), S (bottom-left) and the
// 3D view (bottom-right). `mprViewports` returns the four rectangles (GL
// pixel coordinates, y up from the bottom scanline, matching core::setViewport).

#include <array>
#include <cstdint>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "volume/transfer_function.hpp"

namespace re::app {

/// The three orthogonal slice views of an MPR layout, and the SPEC FR-app.2
/// axis each one samples along:
///   - Transverse: slice at constant Z (axial);
///   - Coronal:    slice at constant Y;
///   - Sagittal:   slice at constant X.
enum class MprAxis {
    Transverse, ///< constant Z
    Coronal,    ///< constant Y
    Sagittal,   ///< constant X
};

/// A rectangle in GL pixel coordinates (origin bottom-left), used for the MPR
/// viewport grid.
struct MprViewport {
    int x{0};        ///< Left edge, in pixels from the window's left.
    int y{0};        ///< Bottom edge, in pixels from the window's bottom.
    int width{0};    ///< Width in pixels.
    int height{0};   ///< Height in pixels.
};

/// The slice-state scaffolding: which voxel-index plane each 2D view is on.
/// v1 holds each view on a fixed (constructor-chosen) index. The slice state
/// DRIVES the MPR composition: each slice view's contour plane (`slicePlane`)
/// and the 3D view's camera look-at target (app::make3dCamera, app/mpr_camera.hpp)
/// are both derived from it — the slice-state ↔ 3D-view camera interplay
/// (FR-app.3).
struct MprSliceState {
    std::uint32_t transverseZ{0}; ///< Transverse slice index (constant Z).
    std::uint32_t coronalY{0};    ///< Coronal slice index (constant Y).
    std::uint32_t sagittalX{0};   ///< Sagittal slice index (constant X).
};

/// The FR-app.3 contour stroke color: pure red straight RGBA, mapping to the
/// exact RGBA8 bytes (255, 0, 0, 255). The GPU contour layer
/// (render::ContourObject via broker::ContourMapper + render::ContourRenderer)
/// draws its opaque strokes in exactly this color; the FR-app.3 gate asserts
/// pixels within the ±2 px band of the plane∩mesh curve match it within
/// 1/255.
inline constexpr glm::vec4 kContourColor{1.0f, 0.0f, 0.0f, 1.0f};

/// The plane of one MPR slice view in the shared voxel-index coordinate space:
/// a plane perpendicular to `axis` at `coordinate` (voxel-index units, through
/// the centers of the sliced voxel layer). Feeds both the slice views' GPU
/// contour planes (scene::ContourObject::plane / render::ClipPlane) and the
/// analytic cross-section rectangles the gate asserts against.
struct SlicePlane {
    MprAxis axis;     ///< The axis the plane is perpendicular to.
    float coordinate; ///< The held coordinate (voxel-index units).
};

/// The four MPR viewports in a 2x2 grid for a `windowWidth` x `windowHeight`
/// window (SPEC FR-app.2). The window is split into four equal quadrants; the
/// returned array holds, in order: T (top-left), C (top-right), S
/// (bottom-left), 3D (bottom-right). Each quadrant is `windowWidth/2` x
/// `windowHeight/2` pixels. For the SPEC window 1280x960 this yields four
/// 640x480 viewports at the pinned grid positions.
std::array<MprViewport, 4> mprViewports(int windowWidth, int windowHeight);

/// Build a 2D RGBA slice image of `dataset` through `tf`, sampling along the
/// axis `axis` at voxel-index `index` per the SPEC FR-app.2 convention.
///
/// The slice is a rectangle over the two free axes of `dataset`:
///   - Transverse (constant Z = `index`): width = sizeX, height = sizeY, pixel
///     (x, y) sampled from voxel (x, y, index);
///   - Coronal (constant Y = `index`): width = sizeX, height = sizeZ, pixel
///     (x, z) sampled from voxel (x, index, z);
///   - Sagittal (constant X = `index`): width = sizeY, height = sizeZ, pixel
///     (y, z) sampled from voxel (index, y, z).
///
/// Each sampled voxel is mapped through `tf` (FR-vol.1) to a straight RGBA
/// color and stored as RGBA8 bytes (round(v * 255 + 0.5)) in a data::Image
/// (row-major top-left origin). The returned image's `width()`/`height()` and
/// per-pixel bytes are the explainable acceptance values the T14 gate asserts.
///
/// Precondition (caller-validated): `index` is within [0, size) of the axis
/// being held constant; callers derive the index from the dataset's dims.
data::Image makeSliceImage(const data::VolumeDataset& dataset,
                           const volume::TransferFunction& tf, MprAxis axis,
                           std::uint32_t index);

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
///
/// The box is a NON-MANIFOLD quad shell (6 faces x 4 vertices = 24 vertices,
/// 12 triangles; each face owns its four corner vertices), so every vertex's
/// area-weighted normal equals its face's geometric normal — each face renders
/// FLAT under the v1 deterministic lighting. Faces are emitted in painter's
/// order for the make3dCamera view (eye along the (1,1,1) diagonal): far faces
/// (-Z, -X, -Y) first, near faces (+X, +Y) next, +Z last — with depth test off
/// the near +Z face overdraws the far ones at the viewport center.
data::Mesh makeBoxMesh(const glm::vec3& min, const glm::vec3& max);

} // namespace re::app
