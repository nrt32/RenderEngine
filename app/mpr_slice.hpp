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
// The 2x2 viewport grid (SPEC §4 FR-app.2): a 1280x960 window split into four
// 640x480 viewports, with T (top-left), C (top-right), S (bottom-left) and the
// 3D view (bottom-right). `mprViewports` returns the four rectangles (GL
// pixel coordinates, y up from the bottom scanline, matching core::setViewport).

#include <array>
#include <cstdint>

#include "data/image.hpp"
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
/// DRIVES the T15 composition: each slice view's contour plane
/// (app::slicePlane, app/mpr_contour.hpp) and the 3D view's camera look-at
/// target (app::make3dCamera) are both derived from it — the slice-state ↔
/// 3D-view camera interplay (FR-app.3).
struct MprSliceState {
    std::uint32_t transverseZ{0}; ///< Transverse slice index (constant Z).
    std::uint32_t coronalY{0};    ///< Coronal slice index (constant Y).
    std::uint32_t sagittalX{0};   ///< Sagittal slice index (constant X).
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

} // namespace re::app
