// broker/material_mapper.cpp — MaterialMapper: scene presentation values →
// canonical store-resident Phong materials (value dedup; see header).

#include "broker/material_mapper.hpp"

#include <cstring>

#include <spdlog/spdlog.h>

#include "data/content_hash.hpp"
#include "render/phong_material.hpp"

namespace re::broker {
namespace {

/// Content hash of a PhongDesc VALUE: SHA-256 continuation of canonical LE
/// floats in fixed order (baseColor RGBA, specular RGB, shininess, ambient,
/// diffuse) — doubleSided dropped with typed warning, consistent with
/// render/asset_registry.cpp:44 materialContentHash (same field set, same
/// canonicalization via memcpy+htole32 with NaN canonicalized, same SHA-256
/// truncated 64). Deterministic across runs and across LE/BE hosts per
/// SPEC §10.1 hierarchical Version:LayoutId:Type:Hash. XOR weak → SHA-256
/// continuation (T12).
std::uint64_t phongValueHash(const scene::PhongDesc& p) noexcept {
    if (p.doubleSided) {
        spdlog::warn(
            "MaterialMapper: PhongDesc.doubleSided=true is dropped — "
            "IMaterial has no double-sided facet this iteration (RE-minimal "
            "SPEC §12.4) — typed warning: doubleSided not carried to "
            "render::PhongMaterial, rendering will be single-sided");
    }
    ::re::data::detail::SHA256 h;
    auto feed = [&](float v) {
        uint32_t le = ::re::data::canonicalFloatBits(v);
        h.update(reinterpret_cast<const uint8_t*>(&le), sizeof(le));
    };
    feed(p.baseColor.r);
    feed(p.baseColor.g);
    feed(p.baseColor.b);
    feed(p.baseColor.a);
    feed(p.specular.r);
    feed(p.specular.g);
    feed(p.specular.b);
    feed(p.shininess);
    // PhongDesc has no ambient/diffuse carrier this iteration; for hash
    // continuity with render::PhongMaterial (which does carry ambient+diffuse)
    // we feed the RE defaults (ambient 0, diffuse 1) so the two hashes cover
    // the same logical field set baseColor+specular+shininess+ambient+diffuse
    // with doubleSided consistently dropped on both sides.
    feed(0.0f); // ambient default
    feed(1.0f); // diffuse default
    uint8_t d[32];
    h.final(d);
    return ::re::data::detail::truncatedLE64(d);
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
