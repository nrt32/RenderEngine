#pragma once

// broker/mesh_object_mapper.hpp — MeshObjectMapper: ICachedMapper<scene::MeshObject, render::MeshInstance> (T3 V3.2b).
//
// One file per mapper (guardrail broker_per_type). Cached by generation+content.
// Forwards through render::AssetRegistry (one GL object per distinct data::Mesh
// pointer, deduped globally — T dedup invariant). Same data::Mesh pointer twice
// via Broker still dedups to one GL object when later AssetId path lands (T7).
// No raw gl* (gpu_api_ownership — render/ helpers own GL via core/).

#include <memory>
#include <optional>
#include <unordered_map>

#include "broker/i_mapper.hpp"
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
class MeshObjectMapper : public ICachedMapper<scene::MeshObject, render::MeshInstance> {
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

    /// Cached translation: short-circuits when generation unchanged for the id.
    data::Result<render::MeshInstance> mapCached(
        const scene::MeshObject& app,
        const scene::TranslateContext& ctx) override;

    /// Invalidate cached entry for the given object id.
    void invalidate(uint64_t id) override;

    /// Access registry (for test dedup invariant — slotCount). Shared handle:
    /// the mapper co-owns it (non-null unless constructed with nullptr).
    const std::shared_ptr<render::AssetRegistry>& registry() const noexcept {
        return registry_;
    }

   private:
    std::shared_ptr<render::AssetRegistry> registry_;
    std::shared_ptr<MaterialMapper> materials_;
    struct Entry {
        uint64_t generation{0};
        render::MeshInstance instance{};
    };
    std::unordered_map<uint64_t, Entry> cache_;
};

} // namespace re::broker
