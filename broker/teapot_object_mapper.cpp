// broker/teapot_object_mapper.cpp — TeapotObjectMapper cached translation (T1).
//
// Shares the MeshObjectMapper's mesh-family path — one GL object per distinct
// data::Mesh pointer value-deduped via the shared AssetRegistry, plus the
// presentation's canonical material through the composed MaterialMapper (the
// SPEC §12 hand-off). No raw gl* (gpu_api_ownership — uploads via
// render::AssetRegistry→core/). T1 Phase C.

#include "broker/teapot_object_mapper.hpp"

namespace re::broker {

data::Result<render::MeshInstance> TeapotObjectMapper::map(
    const scene::TeapotObject& app, const scene::TranslateContext& ctx) const {
    if (!app.mesh) {
        return data::makeError<render::MeshInstance>(1, "TeapotObjectMapper: null mesh asset reference");
    }
    if (!registry_) {
        return data::makeError<render::MeshInstance>(2, "TeapotObjectMapper: null AssetRegistry");
    }
    auto h = registry_->registerAsset(*app.mesh);
    if (h.failed()) {
        return data::makeError<render::MeshInstance>(h.error().code, h.error().message);
    }
    if (!materials_) {
        return data::makeError<render::MeshInstance>(3, "TeapotObjectMapper: null MaterialMapper");
    }
    auto material = materials_->map(app.presentation, ctx);
    if (material.failed()) {
        return data::makeError<render::MeshInstance>(material.error().code, material.error().message);
    }
    render::MeshInstance inst;
    inst.mesh = *h;
    inst.material = *material;
    inst.model = app.transform;
    return data::makeValue<render::MeshInstance>(inst);
}

data::Result<render::MeshInstance> TeapotObjectMapper::mapCached(
    const scene::TeapotObject& app, const scene::TranslateContext& ctx) {
    auto it = cache_.find(app.id);
    if (it != cache_.end() && it->second.generation == app.generation) {
        return data::makeValue<render::MeshInstance>(it->second.instance);
    }
    auto r = map(app, ctx);
    if (r.ok()) {
        cache_[app.id] = Entry{app.generation, *r};
    }
    return r;
}

void TeapotObjectMapper::invalidate(uint64_t id) { cache_.erase(id); }

} // namespace re::broker
