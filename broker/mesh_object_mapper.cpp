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

// mapCached, invalidate and clear are provided by CachedMapperBase (T16 dedup — the single cache definition owns unordered_map<uint64_t,Entry> cache_ plus the generation short-circuit and per-id eviction; this MeshObjectMapper implements only map() and inherits the shared cache, while PlaneMapper and PlaneObjectMapper stay stateless IMapper per ISP segregation, so no per-file hand copy remains).

} // namespace re::broker
