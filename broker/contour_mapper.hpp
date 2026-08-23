#pragma once

// broker/contour_mapper.hpp — ContourMapper for the contour overlay
// (V3.8b T11, FR-app.3): pure translation of scene::ContourObject to
// render::ContourObject via IMapper<scene::ContourObject, render::ContourObject>.
//
// One file per mapper (guardrail broker_per_type). Pure translation (ISP: no
// cache — the underlying AssetRegistry dedup already makes repeated
// registrations of the same CPU mesh free, so a generation cache would add a
// second bookkeeping layer without a second reason to change):
//
//   scene::ContourObject{const data::Mesh*, transform, PlaneDesc, color}
//     → render::ContourObject{AssetHandle, ClipPlane, color, model}
//
// Injects AssetHandle residence via render::AssetRegistry (RE-minimal: the RE
// side carries only the handle — SPEC §12.4). PlaneDesc → ClipPlane is the
// broker-side plane conversion (scene/plane_desc.hpp): Space::World maps
// 1:1; Space::VoxelIndex needs the volume-context voxel→world conversion,
// which this mapper does not perform — typed error, never a silent identity
// map (SPEC §5). The plane is carried in the OBJECT'S LOCAL frame (post
// `transform`), matching render::ClipPlane's post-model evaluation in the
// shader — an app that displays a mesh through an axis-permutation model
// expresses its slice plane in that display frame (see app/mpr_sample.cpp).
// No raw gl* (guardrail gpu_api_ownership — render/ owns GL via core/).

#include "broker/i_mapper.hpp"
#include "render/asset_registry.hpp"
#include "render/contour_renderer.hpp"
#include "scene/object.hpp"
#include "scene/plane_desc.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Contour mapper — pure translation scene::ContourObject ->
/// render::ContourObject.
class ContourMapper : public IMapper<scene::ContourObject,
                                     render::ContourObject> {
   public:
    using AppType = scene::ContourObject;
    using ReType = render::ContourObject;

    /// Construct with the shared asset registry (must outlive mapper).
    explicit ContourMapper(render::AssetRegistry* registry)
        : registry_(registry) {}

    /// Pure translation: registers the mesh in the AssetRegistry (deduped by
    /// CPU-object identity) and carries plane/color/model across. Typed
    /// errors: code 1 null mesh pointer; code 2 null AssetRegistry; code 3
    /// Space::VoxelIndex plane (voxel→world conversion not performed here).
    data::Result<render::ContourObject> map(
        const scene::ContourObject& app,
        const scene::TranslateContext& ctx) const override;

    /// Access registry (for test dedup invariant — slotCount).
    render::AssetRegistry* registry() const noexcept { return registry_; }

   private:
    render::AssetRegistry* registry_;
};

} // namespace re::broker
