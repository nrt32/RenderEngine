// broker/material_mapper.cpp — MaterialMapper: scene presentation values →
// canonical store-resident Phong materials (value dedup; see header).

#include "broker/material_mapper.hpp"

#include <cstring>

#include "data/content_hash.hpp"
#include "render/phong_material.hpp"

namespace re::broker {
namespace {

/// Content hash of a PhongDesc VALUE: every field participates (baseColor
/// RGBA, specular RGB, shininess, doubleSided), fed component-wise so glm
/// padding cannot leak into the identity. Deterministic across runs — the
/// same property the asset kinds' hashes guarantee (data/content_hash.hpp).
std::uint64_t phongValueHash(const scene::PhongDesc& p) noexcept {
    std::uint64_t h = data::hashStableBytes(&p.baseColor.r, sizeof(float) * 4);
    h ^= data::hashStableBytes(&p.specular.r, sizeof(float) * 3);
    h ^= data::hashStableBytes(&p.shininess, sizeof(float));
    const std::uint8_t dbl = p.doubleSided ? 1u : 0u;
    h ^= data::hashStableBytes(&dbl, 1);
    return h;
}

} // namespace

data::Result<std::shared_ptr<render::IMaterial>> MaterialMapper::map(
    const scene::MeshMaterialDesc& app,
    const scene::TranslateContext& /*ctx*/) const {
    if (!registry_) {
        return data::makeError<std::shared_ptr<render::IMaterial>>(
            1, "MaterialMapper: null AssetRegistry");
    }

    // Only the Phong alternative exists this iteration, so the value hash
    // covers exactly the PhongDesc fields; MeshMaterialDesc is a closed
    // single-alternative carrier (SPEC §12 deferral), and a new lighting
    // model later extends the hash + visitor additively without touching any
    // caller.
    const std::uint64_t key = phongValueHash(app.phong);

    auto hit = byValue_.find(key);
    if (hit != byValue_.end()) {
        return data::makeValue<std::shared_ptr<render::IMaterial>>(
            std::const_pointer_cast<render::PhongMaterial>(hit->second.canonical));
    }
    // First sight of this value: build the canonical and register it in the
    // store's material table so identical values share one slot across every
    // consumer of the registry (the same dedup story as meshes/textures).
    // Every PhongDesc field with an RE counterpart is carried: baseColor via
    // the constructor, specular/shininess through PhongMaterial's public
    // members — the dedup hash above must cover exactly the mapped content,
    // otherwise two values differing only in an unmapped field would occupy
    // two store slots while rendering identically (a silent value drop).
    // doubleSided is deliberately NOT carried: IMaterial has no double-sided
    // facet this iteration (RE-minimal §12.4) and no renderer consumes one.
    auto canonical = std::make_shared<render::PhongMaterial>(app.phong.baseColor);
    canonical->specular = app.phong.specular;
    canonical->shininess = app.phong.shininess;
    auto registered = registry_->registerMaterial(canonical);
    if (registered.failed()) {
        return data::makeError<std::shared_ptr<render::IMaterial>>(
            registered.error().code, registered.error().message);
    }
    Entry entry;
    entry.canonical = canonical;
    entry.handle = *registered;
    byValue_.emplace(key, entry);

    // The instance handed out shares ownership with the cache's canonical
    // (a copy of the same shared_ptr), so mapped instances can never dangle
    // their material while this mapper lives.
    return data::makeValue<std::shared_ptr<render::IMaterial>>(canonical);
}

} // namespace re::broker
