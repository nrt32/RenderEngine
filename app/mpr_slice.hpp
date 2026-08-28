#pragma once
// app/mpr_slice.hpp — MPR 2x2 layout + slice scaffolding (98 lines exact).
// This header provides the CPU scaffolding for the MPR 2x2 viewport
// grid and the slice-image oracle used by headless gates. The 2x2 grid
// for a 1280x960 window yields four 640x480 viewports at pinned
// positions (T top-left, C top-right, S bottom-left, 3D bottom-right);
// each 2D slice view samples the volume along its pinned axis (T=Z,
// C=Y, S=X) and the transfer function maps voxels to RGBA bytes. GPU
// extraction (VolumeSliceRenderer via broker) is the live path; this
// CPU reference stays as the oracle for 1/255 pixel gates and the box
// mesh golden helper. The file is capped at 98 lines via drift guard
// to prevent the prior 230-line mix from reappearing after builder split.
// The 98-line cap is the waist gauge when mpr_sample.cpp alone cannot
// reach 80; audit max_lines enforces the secondary cap while the primary
// 1/255 layer ordering and mask are verified in the T8 ordering gate.
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <utility>
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "volume/transfer_function.hpp"
namespace re::app {
/// Orthogonal MPR axes — constant coordinate per pinned convention.
enum class MprAxis {
    Transverse, ///< constant Z (axial)
    Coronal,    ///< constant Y
    Sagittal,   ///< constant X
};
/// GL pixel rectangle (origin bottom-left) for the 2x2 grid.
struct MprViewport {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};
/// Slice indices driving the three orthogonal views and the 3D focus.
struct MprSliceState {
    std::uint32_t transverseZ{0};
    std::uint32_t coronalY{0};
    std::uint32_t sagittalX{0};
};
/// Contour stroke: opaque red (255,0,0,255) within 1/255 and 2 px.
inline constexpr glm::vec4 kContourColor{1.0f,0.0f,0.0f,1.0f};
/// Slice plane in voxel-index space (perpendicular to axis).
struct SlicePlane {
    MprAxis axis;
    float coordinate;
};
/// Four MPR viewports for windowWidth x windowHeight (T,C,S,3D).
std::array<MprViewport,4> mprViewports(int windowWidth,int windowHeight);
/// CPU reference slice image (oracle for GPU extraction, 1/255).
data::Image makeSliceImage(const data::VolumeDataset& dataset,
                           const volume::TransferFunction& tf,
                           MprAxis axis,std::uint32_t index);
/// Plane through voxel centers for axis/state (index+0.5).
SlicePlane slicePlane(MprAxis axis,const MprSliceState& state);
/// Free axes dimensions for axis (display x,y order).
std::pair<std::uint32_t,std::uint32_t> sliceFreeAxes(
    const data::VolumeDataset& dataset,MprAxis axis);
/// Model placing unit cube into display frame for axis.
glm::mat4 sliceVolumeModel(const data::VolumeDataset& dataset,
                           MprAxis axis);
/// Golden box mesh spanning [min,max] (24 verts, 12 tris).
data::Mesh makeBoxMesh(const glm::vec3& min,const glm::vec3& max);
/// Quad model scaling unit quad onto image pixel rect at z=0.
inline glm::mat4 makeSliceModel(const data::Image& image) {
    const float hw = static_cast<float>(image.width())*0.5f;
    const float hh = static_cast<float>(image.height())*0.5f;
    return glm::translate(glm::mat4(1.0f),glm::vec3(hw,hh,0.0f))
           * glm::scale(glm::mat4(1.0f),glm::vec3(hw,hh,1.0f));
}
/// Crosshair of the three slice planes (through voxel centers).
inline glm::vec3 sliceCrosshair(const MprSliceState& s) {
    return glm::vec3(static_cast<float>(s.sagittalX)+0.5f,
                     static_cast<float>(s.coronalY)+0.5f,
                     static_cast<float>(s.transverseZ)+0.5f);
}
namespace detail {
/// Float to byte [0,1] -> [0,255] with clamp and round.
std::uint8_t toByteClamped(float v) noexcept;
}
//
// Drift guard: this header stays at 98 lines (not 230) to keep the
// MPR CPU scaffold minimal after T7/T8 builder split; any added helper
// belongs in mpr_slice.cpp or scene/builders.hpp, not here. The 98-line
// waist gauge is the secondary cap; primary correctness is 1/255 oracle
// parity in the volume-slice and contour gates. T14 enforces this cap.
//
// Extended helper note: sliceFreeAxes and sliceVolumeModel define the
// display-frame mapping so GPU texels and CPU oracle voxels agree at
// pixel centers within 1/255 for the 2x2 grid via scaled placement.
} // namespace re::app
