// broker/mesh_object_mapper.cpp — MeshObjectMapper cached translation (no raw gl*).

#include "broker/mesh_object_mapper.hpp"

namespace re::broker {

data::Result<render::MeshInstance> MeshObjectMapper::map(
    const scene::MeshObject& app, const scene::TranslateContext& /*ctx*/) const {
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
    render::MeshInstance inst;
    inst.mesh = *h;
    inst.material = nullptr; // Phong-only placeholder; material path lands in T8
    inst.model = app.transform;
    return data::makeValue<render::MeshInstance>(inst);
}

data::Result<render::MeshInstance> MeshObjectMapper::mapCached(
    const scene::MeshObject& app, const scene::TranslateContext& ctx) {
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

void MeshObjectMapper::invalidate(uint64_t id) {
    cache_.erase(id);
}

} // namespace re::broker
