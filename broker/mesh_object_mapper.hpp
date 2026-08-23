#pragma once

// broker/mesh_object_mapper.hpp — MeshObjectMapper: ICachedMapper<scene::MeshObject, render::MeshInstance> (T3 V3.2b).
//
// One file per mapper (guardrail broker_per_type). Cached by generation+content.
// Forwards through render::AssetRegistry (one GL object per distinct data::Mesh
// pointer, deduped globally — T dedup invariant). Same data::Mesh pointer twice
// via Broker still dedups to one GL object when later AssetId path lands (T7).
// No raw gl* (gpu_api_ownership — render/ helpers own GL via core/).

#include <optional>
#include <unordered_map>

#include "broker/i_mapper.hpp"
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
class MeshObjectMapper : public ICachedMapper<scene::MeshObject, render::MeshInstance> {
   public:
    using AppType = scene::MeshObject;
    using ReType = render::MeshInstance;
    /// Construct with the shared asset registry (must outlive mapper).
    explicit MeshObjectMapper(render::AssetRegistry* registry) : registry_(registry) {}

    /// Pure translation: registers mesh in AssetRegistry and returns MeshInstance.
    data::Result<render::MeshInstance> map(
        const scene::MeshObject& app,
        const scene::TranslateContext& ctx) const override;

    /// Cached translation: short-circuits when generation unchanged for the id.
    data::Result<render::MeshInstance> mapCached(
        const scene::MeshObject& app,
        const scene::TranslateContext& ctx) override;

    /// Invalidate cached entry for the given object id.
    void invalidate(uint64_t id) override;

    /// Access registry (for test dedup invariant — slotCount).
    render::AssetRegistry* registry() const noexcept { return registry_; }

   private:
    render::AssetRegistry* registry_;
    struct Entry {
        uint64_t generation{0};
        render::MeshInstance instance{};
    };
    std::unordered_map<uint64_t, Entry> cache_;
};

} // namespace re::broker
