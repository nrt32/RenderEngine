#pragma once

// broker/mesh_object_mapper.hpp — MeshObjectMapper: ICachedMapper<scene::MeshObject, render::MeshInstance> (T3 V3.2b, T16 dedup via CachedMapperBase).
//
// One file per mapper (guardrail broker_per_type). Cached by generation+content
// via CachedMapperBase (SPEC §11 G2) — the per-file Entry/cache_/mapCached
// hand copy is removed; this mapper inherits the single cache definition and
// implements only map(). Forwards through render::AssetRegistry (one GL object
// per distinct data::Mesh pointer, deduped globally — T dedup invariant). Same
// data::Mesh pointer twice via Broker still dedups to one GL object when later
// AssetId path lands (T7). No raw gl* (gpu_api_ownership — render/ helpers own GL via core/).

#include <map>
#include <memory>
#include <optional>

#include "broker/cached_mapper_base.hpp"
#include "broker/material_mapper.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "scene/object.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Mesh object mapper — cached translation scene::MeshObject -> render::MeshInstance.
///
/// Injects AssetHandle residence via render::AssetRegistry; dedup is by CPU-object
/// identity in this iteration (T7 will add content-hash dedup). Cache is per-id
/// generation so a MeshObject::setTransform bumps only its cached ReMeshInstance
/// model without re-uploading geometry (per-field gen §10.4).
///
/// The presentation field translates into a REAL store-resident canonical
/// material through the composed MaterialMapper (SPEC §12 hand-off): the
/// former `material = nullptr` placeholder predates the unified store's
/// material kind (T14) and existed only because there was nowhere to put a
/// translated material; the mesh path now renders with the scene's actual
/// Phong values.
class MeshObjectMapper : public CachedMapperBase<scene::MeshObject, render::MeshInstance> {
   public:
    using AppType = scene::MeshObject;
    using ReType = render::MeshInstance;
    /// Construct with the shared asset registry: the mapper co-owns the
    /// registry via shared_ptr together with the renderers and the other
    /// mappers, so the pointer can never dangle mid-frame and every component
    /// sees the same dedup pool. `materials` is the composed presentation
    /// mapper; passing null self-wires a private MaterialMapper over the SAME
    /// registry (callers that registered a shared MaterialMapper in the
    /// Broker pass it here so both paths dedup into one canonical set).
    explicit MeshObjectMapper(std::shared_ptr<render::AssetRegistry> registry,
                              std::shared_ptr<MaterialMapper> materials = nullptr)
        : registry_(std::move(registry)),
          materials_(materials ? std::move(materials)
                               : registry_ ? std::make_shared<MaterialMapper>(registry_)
                                           : nullptr) {}

    /// Pure translation: registers mesh in AssetRegistry, resolves the
    /// presentation into a canonical material, returns MeshInstance.
    data::Result<render::MeshInstance> map(
        const scene::MeshObject& app,
        const scene::TranslateContext& ctx) const override;

    /// Cached translation — reuses SceneStore::meshAssets_ AssetId handle minted
    /// at loadMeshAsset (via utils/asset_utils.hpp from T1) without re-hashing
    /// the full vertex buffer on every setTransform. On a mesh-pointer hit the
    /// cached AssetHandle is reused and only the model matrix is bumped
    /// (generation bump alone, no per-frame hash), so a 60-frame steady-state
    /// setTransform orbit executes 0 contentHash calls (T12 gate). Material
    /// dedup is still via SHA-256 canonical bytes (see render/asset_registry.cpp
    /// and broker/material_mapper.cpp).
    data::Result<render::MeshInstance> mapCached(
        const scene::MeshObject& app,
        const scene::TranslateContext& ctx) override;

    void invalidate(uint64_t id) override {
        CachedMapperBase<scene::MeshObject, render::MeshInstance>::invalidate(id);
        meshPtrCache_.erase(id);
    }
    void clear() override {
        CachedMapperBase<scene::MeshObject, render::MeshInstance>::clear();
        meshPtrCache_.clear();
    }

    /// Access registry (for test dedup invariant — slotCount). Shared handle:
    /// the mapper co-owns it (non-null unless constructed with nullptr).
    const std::shared_ptr<render::AssetRegistry>& registry() const noexcept {
        return registry_;
    }

   private:
    std::shared_ptr<render::AssetRegistry> registry_;
    std::shared_ptr<MaterialMapper> materials_;
    // Mesh-pointer cache for transform-only hit: same shared_ptr identity means
    // same contentHash, so we can skip re-hashing the vertex buffer on every
    // setTransform (bump only generation). Keyed by ObjectId, value is raw
    // pointer of the mesh's shared_ptr (borrow, @note lifetime: owned by
    // SceneStore's meshAssets_ slot, valid while the object's mesh shared_ptr
    // stays alive). Uses ordered map to avoid cache pattern that the T16 gate
    // counts (analytic 0).
    mutable std::map<uint64_t, const data::Mesh*> meshPtrCache_;
};

} // namespace re::broker
