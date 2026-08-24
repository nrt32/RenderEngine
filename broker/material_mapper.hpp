#pragma once

// broker/material_mapper.hpp — MaterialMapper: the scene → RE material
// hand-off (SPEC §11.3 per-type inventory `material_mapper.*`, SPEC §12.2).
//
//   scene::MeshMaterialDesc{PhongDesc{baseColor, specular, shininess, ...}}
//     → std::shared_ptr<render::IMaterial>  (a render::PhongMaterial value)
//
// This mapper replaces the MeshObjectMapper `material = nullptr` placeholder:
// that stub existed only until the unified asset store landed its material
// kind (render::AssetRegistry::registerMaterial, SPEC §7 T14), which it now
// has — so presentation values translate into REAL store-resident canonical
// materials and the mesh path renders with the scene's actual colors instead
// of falling back to an invalid-instance error or a hardcoded color.
//
// Value dedup (the store's material identity): every distinct PhongDesc VALUE
// maps to one shared_ptr, cached here by the hash of the desc's bytes. The
// canonical instance is also registered in the shared AssetRegistry material
// table (`registerMaterial`), so two scene objects carrying identical
// presentation values share one store slot — the same content-hash dedup the
// mesh/volume/image kinds get (gate evidence: materialSlotCount() == 1 after
// registering equal values twice). The mapper co-owns each canonical it hands
// out (the cache holds the shared_ptr), so a MeshInstance's material can never
// dangle while this mapper is alive; entries live for the mapper's lifetime,
// which matches the samples' program-duration composition root.
//
// Only Phong exists this iteration (SPEC §12 V3.7 deferral: PBR/Slice/Contour
// materials are non-goals), so the mapper's translation is a closed-form value
// construction today. Every PhongDesc field with an RE counterpart is carried
// (baseColor, specular, shininess — the dedup hash covers exactly that mapped
// content); `doubleSided` has no IMaterial facet this iteration and is
// documented as dropped at the mapping site. The MeshMaterialDesc carrier is a
// closed variant, however, so adding another lighting model later is one new
// alternative plus one visitor overload here — never an edit to MeshObject or
// any caller (OCP via variant, SPEC §11.3.2).
//
// Typed errors (SPEC §5): code 1 = null AssetRegistry (construction misuse);
// code 2 = the store rejected the registration (propagated). No raw gl*
// (guardrail gpu_api_ownership — the store uploads nothing for materials; the
// GPU consumes the values through the renderers' uniforms).

#include <memory>
#include <unordered_map>

#include "broker/i_mapper.hpp"
#include "render/asset_registry.hpp"
#include "scene/material_desc.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Material mapper — pure translation scene::MeshMaterialDesc ->
/// std::shared_ptr<render::IMaterial> with value dedup.
class MaterialMapper : public IMapper<scene::MeshMaterialDesc,
                                      std::shared_ptr<::re::render::IMaterial>> {
   public:
    using AppType = scene::MeshMaterialDesc;
    using ReType = std::shared_ptr<::re::render::IMaterial>;

    /// Construct with the shared asset registry: the mapper co-owns the store
    /// together with the renderers (T13 shared-ownership rule), so registered
    /// canonicals resolve for the composition root's whole lifetime.
    explicit MaterialMapper(std::shared_ptr<render::AssetRegistry> registry)
        : registry_(std::move(registry)) {}

    /// Pure translation: returns the canonical shared material for `app`'s
    /// VALUE, creating (and store-registering) it on first sight.
    data::Result<std::shared_ptr<render::IMaterial>> map(
        const scene::MeshMaterialDesc& app,
        const scene::TranslateContext& ctx) const override;

    /// Live canonical count in the mapper's value cache (test evidence: two
    /// equal descs map to ONE entry).
    std::size_t cachedCount() const noexcept { return byValue_.size(); }

   private:
    // Mutable cache behind a const map(): mapping is logically pure (same
    // input → same canonical instance), the mutation is memoisation only.
    struct Entry {
        std::shared_ptr<const render::PhongMaterial> canonical;
        render::MaterialHandle handle{};
    };
    mutable std::unordered_map<std::uint64_t, Entry> byValue_;

    std::shared_ptr<render::AssetRegistry> registry_;
};

} // namespace re::broker
