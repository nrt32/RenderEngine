#pragma once

// broker/mesh_slice_object_mapper.hpp — MeshSliceObjectMapper (SPEC §11.3
// per-type inventory `mesh_slice_object_mapper.*`): cached translation
//
//   scene::MeshSliceObject{mesh ref, transform, presentation}
//     + view plane (TranslateContext::view.viewPlane — §11.4: the VIEW owns
//       the plane, the item does not carry one)
//     → render::SliceScene{one MeshInstance, ClipPlane}
//
// The mapped slice is drawn by render::SliceRenderer's geometry-shader clip:
// each triangle is cut against the world-space plane on the GPU and the kept
// half-space plus the on-plane cross-section are emitted (FR-render.4). A
// bridged mesh-slice layer is therefore a REAL clipped draw, never a no-op.
//
// Contextual mapping (SPEC §11.4): like VolumeSliceObjectMapper the plane
// comes from the view by value; a World-space plane passes through unchanged,
// a VoxelIndex one converts only when the context carries a VolumeContext
// (delegated to broker::convertViewPlaneToClipPlane — the ONE PlaneMapper
// rule). Cache: object id + generation + plane identity (the plane rides in
// through the context, so a planeGen-only change must re-translate).
//
// Typed errors (SPEC §5): code 1 = null mesh reference; code 2 = context has
// no view plane; code 3 = material translation or plane conversion failure
// (propagated); code 4 = null AssetRegistry / MaterialMapper. No raw gl*
// (guardrail gpu_api_ownership).

#include <memory>
#include <unordered_map>

#include "broker/i_mapper.hpp"
#include "broker/material_mapper.hpp"
#include "render/asset_registry.hpp"
#include "render/slice_renderer.hpp" // render::SliceScene / MeshInstance
#include "scene/object.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Mesh slice object mapper — cached translation scene::MeshSliceObject
/// (+ view plane) -> render::SliceScene.
class MeshSliceObjectMapper
    : public ICachedMapper<scene::MeshSliceObject, render::SliceScene> {
   public:
    using AppType = scene::MeshSliceObject;
    using ReType = render::SliceScene;

    /// Construct with the shared asset registry + composed presentation
    /// mapper (same ownership pattern as MeshObjectMapper; `materials` null
    /// self-wires a private MaterialMapper over the same registry).
    explicit MeshSliceObjectMapper(std::shared_ptr<render::AssetRegistry> registry,
                                   std::shared_ptr<MaterialMapper> materials = nullptr);

    /// Pure translation (see header for the plane contract and error codes).
    data::Result<render::SliceScene> map(
        const scene::MeshSliceObject& app,
        const scene::TranslateContext& ctx) const override;

    /// Cached translation: cache hit requires BOTH unchanged object
    /// generation AND unchanged view plane.
    data::Result<render::SliceScene> mapCached(
        const scene::MeshSliceObject& app,
        const scene::TranslateContext& ctx) override;

    /// Invalidate cached entry for the given object id.
    void invalidate(uint64_t id) override;

   private:
    std::shared_ptr<render::AssetRegistry> registry_;
    std::shared_ptr<MaterialMapper> materials_;
    struct Entry {
        uint64_t generation{0};
        scene::PlaneDesc plane{};
        bool hasPlane{false};
        render::SliceScene scene{};
    };
    std::unordered_map<uint64_t, Entry> cache_;
};

} // namespace re::broker
