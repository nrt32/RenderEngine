#pragma once

// broker/plane_mapper.hpp — PlaneMapper: the stateless abstract-plane
// conversion mapper (SPEC §11.3 per-type inventory row `plane_mapper.*`):
//
//   scene::PlaneDesc{normal, point, Space::World | Space::VoxelIndex}
//     → render::ClipPlane{normal, point}            (both world space)
//
// This is the ONE place in the engine that owns the voxel-index → world plane
// conversion (SPEC §11.4.2: conversion normalises to world before reaching the
// render side; the mapper receives what the math needs via the ISP-segregated
// `TranslateContext`). Consumers compose it instead of re-deriving the math:
// the view synchronizer converts every 2D view's clip plane through it, and
// the slice-family object mappers (VolumeSliceObjectMapper,
// MeshSliceObjectMapper) convert their view-supplied plane through it.
//
// Conversion contract (analytic, gate-pinned):
//   - Space::World: normal and point pass through unchanged (world is the
//     stable RE representation).
//   - Space::VoxelIndex: the POINT is interpreted as a coordinate in the
//     volume's continuous voxel-index grid where voxel layer i along an axis
//     spans [i, i + 1] index units — its CENTER is i + 0.5 (the same center
//     convention the CPU slice oracle and the GPU extraction texel mapping
//     use). The world point is therefore
//         world = indexPlacement * ((point + 0.5) * voxelSpacing)
//     where `indexPlacement` is VolumeContext::volumeModel UNDER THE BINDING
//     SEMANTICS BELOW. With the identity placement and unit spacing the
//     conversion is exactly point + 0.5 (e.g. a z=35 voxel-index plane lands
//     at world z=35.5 — the pinned gate constant). The NORMAL is NOT
//     transformed: scene::PlaneDesc declares the VoxelIndex space for the
//     POINT only ("normal in world", scene/plane_desc.hpp), so carrying it
//     through keeps the declared semantics without an inverse-transpose.
//
// BINDING SEMANTICS of VolumeContext::volumeModel on this path: it is the
// transform from CONTINUOUS VOXEL-INDEX SPACE to world — NOT the renderer's
// unit-cube model. The two differ by the dataset dims: the engine's canonical
// RE model M places the normalized cube [0,1]^3 (voxel-center index i at
// model coordinate i / (dim - 1)), so the index placement is RECOVERED from
// a known unit-cube model analytically as
//     indexPlacement = M * scale(1 / max(dim - 1, 1)) * translate(-0.5)
// (broker::indexPlacementFromModel below — the one shared change-of-basis;
// for the MPR display models this recovers exactly the pure axis permutation,
// which is what makes a voxel-index view plane land at display z =
// heldIndex + 0.5).
//
// Typed errors (SPEC §5, no exceptions): code 1 = VoxelIndex requested but the
// context carries no VolumeContext (conversion is impossible without dims/
// model/spacing); code 2 = non-positive voxel spacing or zero dataset dims in
// the supplied VolumeContext. A World-space plane with any context (including
// an empty one) always succeeds — LSP: absent roles keep preconditions weak.
//
// One file per mapper (guardrail broker_per_type). Pure translation (ISP): no
// cache — the mapping is a closed-form affine transform of its inputs.

#include "broker/i_mapper.hpp"
#include "render/types.hpp" // render::ClipPlane
#include "scene/plane_desc.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Plane mapper — pure translation scene::PlaneDesc -> render::ClipPlane.
class PlaneMapper : public IMapper<scene::PlaneDesc, render::ClipPlane> {
   public:
    using AppType = scene::PlaneDesc;
    using ReType = render::ClipPlane;

    /// Pure translation. World planes pass through; VoxelIndex planes are
    /// lifted to world through the context's VolumeContext (see the file
    /// comment for the exact analytic rule and the typed error codes).
    data::Result<render::ClipPlane> map(
        const scene::PlaneDesc& app,
        const scene::TranslateContext& ctx) const override;
};

/// Free-function form of the SAME conversion, shared by callers that already
/// hold a decomposed context (the slice mappers convert a view plane through
/// this while building their volume context from the mapped object). Kept in
/// this file so the analytic rule has exactly one definition.
data::Result<render::ClipPlane> convertViewPlaneToClipPlane(
    const scene::PlaneDesc& plane, const scene::VolumeContext& volume);

/// The index-space → world placement recovered from a canonical unit-cube
/// model (see the binding semantics above): `model` places [0,1]^3 with
/// voxel-center index i at model coordinate i / max(dim - 1, 1); the returned
/// matrix maps CONTINUOUS INDEX coordinates to the same world frame. For an
/// identity model this is scale(1/max(dim-1,1)) * translate(-0.5); for the
/// MPR display models it recovers exactly the pure axis permutation.
glm::mat4 indexPlacementFromModel(const glm::mat4& model,
                                  const glm::ivec3& dims);

} // namespace re::broker
