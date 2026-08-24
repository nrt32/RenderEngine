// broker/volume_object_mapper.cpp — VolumeObjectMapper cached translation
// (no raw gl*; the GPU texture upload happens inside the renderer's shared
// asset-store lookup, never here).

#include "broker/volume_object_mapper.hpp"

namespace re::broker {

data::Result<render::VolumeInstance> VolumeObjectMapper::map(
    const scene::VolumeObject& app, const scene::TranslateContext& /*ctx*/) const {
    if (!app.volume) {
        return data::makeError<render::VolumeInstance>(
            1, "VolumeObjectMapper: null volume dataset reference");
    }
    render::VolumeInstance out;
    out.dataset = app.volume; // shared ownership — instance co-owns the bytes
    out.transferFunction = app.transferFunction;
    out.model = app.transform;
    return data::makeValue<render::VolumeInstance>(out);
}

data::Result<render::VolumeInstance> VolumeObjectMapper::mapCached(
    const scene::VolumeObject& app, const scene::TranslateContext& ctx) {
    auto it = cache_.find(app.id);
    if (it != cache_.end() && it->second.generation == app.generation) {
        return data::makeValue<render::VolumeInstance>(it->second.instance);
    }
    auto r = map(app, ctx);
    if (r.ok()) {
        cache_[app.id] = Entry{app.generation, *r};
    }
    return r;
}

void VolumeObjectMapper::invalidate(uint64_t id) {
    cache_.erase(id);
}

} // namespace re::broker
