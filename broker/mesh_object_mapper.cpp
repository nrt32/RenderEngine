// broker/mesh_object_mapper.cpp — MeshObjectMapper cached translation
// (no raw gl*). The presentation field resolves into a canonical store
// material through the composed MaterialMapper — see the header comment.

#include "broker/mesh_object_mapper.hpp"

namespace re::broker {

data::Result<render::MeshInstance> MeshObjectMapper::map(
    const scene::MeshObject& app, const scene::TranslateContext& ctx) const {
    if (!app.mesh) {
        return data::makeError<render::MeshInstance>(1, "MeshObjectMapper: null mesh asset reference");
    }
    if (!registry_) {
        return data::makeError<render::MeshInstance>(2, "MeshObjectMapper: null AssetRegistry");
    }
    auto h = registry_->registerAsset(*app.mesh);
    if (h.failed()) {
        return data::makeError<render::MeshInstance>(h.error().code, h.error().message);
    }
    if (!materials_) {
        return data::makeError<render::MeshInstance>(
            3, "MeshObjectMapper: null MaterialMapper");
    }
    auto material = materials_->map(app.presentation, ctx);
    if (material.failed()) {
        return data::makeError<render::MeshInstance>(material.error().code,
                                                     material.error().message);
    }
    render::MeshInstance inst;
    inst.mesh = *h;
    // The presentation's REAL translated material: a canonical, value-deduped
    // store-resident PhongMaterial (SPEC §12 hand-off; the T14 unified asset
    // store provides the material kind this resolves through).
    inst.material = *material;
    inst.model = app.transform;
    return data::makeValue<render::MeshInstance>(inst);
}

data::Result<render::MeshInstance> MeshObjectMapper::mapCached(
    const scene::MeshObject& app, const scene::TranslateContext& ctx) {
    // Fast path: same mesh shared_ptr (same contentHash) → reuse handle, bump only generation.
    // This ensures SceneStore::meshAssets_ AssetId handle minted at loadMeshAsset is reused
    // and mapCached hit does not re-hash the full vertex buffer on every setTransform.
    auto it = cache_.find(app.id);
    auto mit = meshPtrCache_.find(app.id);
    const bool meshSame = (it != cache_.end() && mit != meshPtrCache_.end() && mit->second == app.mesh.get() && app.mesh != nullptr);
    if (meshSame) {
        // Check material still same via direct value compare (avoids re-hash when only transform changed).
        const auto* cachedMat = dynamic_cast<const render::PhongMaterial*>(it->second.instance.material.get());
        bool matSame = false;
        if (cachedMat) {
            const auto& cur = app.presentation.phong;
            matSame = (cachedMat->baseColor() == cur.baseColor && cachedMat->specular == cur.specular &&
                       cachedMat->shininess == cur.shininess);
            // doubleSided is dropped, so not part of comparison.
        } else if (!it->second.instance.material) {
            matSame = false;
        } else {
            matSame = false;
        }
        if (matSame) {
            // Hit: bump generation, update model, no re-hash.
            it->second.instance.model = app.transform;
            it->second.generation = app.generation;
            return data::makeValue<render::MeshInstance>(it->second.instance);
        }
    }
    // Miss: full translation (hashes) and cache.
    auto r = map(app, ctx);
    if (r.ok()) {
        Entry e;
        e.generation = app.generation;
        e.instance = *r;
        cache_[app.id] = std::move(e);
        if (app.mesh) meshPtrCache_[app.id] = app.mesh.get();
        else meshPtrCache_.erase(app.id);
    } else {
        cache_.erase(app.id);
        meshPtrCache_.erase(app.id);
    }
    return r;
}

} // namespace re::broker
