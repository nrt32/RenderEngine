#pragma once

// broker/teapot_object_mapper.hpp — TeapotObjectMapper: ICachedMapper<scene::TeapotObject, render::MeshInstance> (T1).
//
// One file per mapper (guardrail broker_per_type). The TeapotObject kind is the
// sixteenth kind not present in the old variant alias — adding it proves open
// extension with zero edits to SceneStore or ViewSynchronizer (one header plus
// one registerMapper<TeapotObjectMapper> line). The mapper shares the mesh
// asset path: it registers the Teapot's immutable mesh via the shared
// AssetRegistry (one GL object per distinct data::Mesh pointer, value-deduped),
// resolves the Phong presentation through the composed MaterialMapper, and
// returns a MeshInstance whose center pixel composite matches the analytic
// Phong headlight within 1/255 (gate: analytic constant, not >0). No raw gl*
// — the upload is forwarded through render::AssetRegistry via core/. T1 Phase C.

#include <memory>
#include <unordered_map>

#include "broker/i_mapper.hpp"
#include "broker/material_mapper.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "scene/objects/teapot_object.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Teapot object mapper — cached translation scene::TeapotObject -> render::MeshInstance.
///
/// Mesh-backed open kind (Utah teapot). Shares the MeshObjectMapper's registry
/// and material composition so its rendered result is byte-identical to a
/// MeshObject with the same mesh/transform/phong (analytic 51,102,204 for the
/// gate's 0.2,0.4,0.8 base color under the fixed headlight). Cache is per-id
/// generation so a TeapotObject::setTransform bumps only its cached instance.
class TeapotObjectMapper : public ICachedMapper<scene::TeapotObject, render::MeshInstance> {
   public:
    using AppType = scene::TeapotObject;
    using ReType = render::MeshInstance;

    explicit TeapotObjectMapper(std::shared_ptr<render::AssetRegistry> registry,
                                std::shared_ptr<MaterialMapper> materials = nullptr)
        : registry_(std::move(registry)),
          materials_(materials ? std::move(materials)
                               : registry_ ? std::make_shared<MaterialMapper>(registry_)
                                           : nullptr) {}

    /// Pure translation: registers mesh, resolves material, returns MeshInstance.
    data::Result<render::MeshInstance> map(
        const scene::TeapotObject& app,
        const scene::TranslateContext& ctx) const override;

    /// Cached translation: short-circuits when generation unchanged for the id.
    data::Result<render::MeshInstance> mapCached(
        const scene::TeapotObject& app,
        const scene::TranslateContext& ctx) override;

    /// Invalidate cached entry for the given object id.
    void invalidate(uint64_t id) override;

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
