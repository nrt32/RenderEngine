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
    if (!registry_) {
        return data::makeError<render::VolumeInstance>(
            2, "VolumeObjectMapper: null AssetRegistry");
    }
    auto h = registry_->registerVolume(app.volume);
    if (h.failed()) {
        return data::makeError<render::VolumeInstance>(h.error().code,
                                                      h.error().message);
    }
    render::VolumeInstance out;
    out.handle = *h;
    out.dataset = app.volume; // retained for CPU size/uniforms, GPU identity is handle
    out.transferFunction = app.transferFunction;
    out.model = app.transform;
    return data::makeValue<render::VolumeInstance>(out);
}

// mapCached, invalidate and clear are provided by CachedMapperBase (T16 dedup — the single cache definition owns unordered_map<uint64_t,Entry> cache_ plus the generation short-circuit and per-id eviction; this VolumeObjectMapper implements only map() and inherits the shared cache, while PlaneMapper and PlaneObjectMapper stay stateless IMapper per ISP segregation, so no per-file hand copy remains).

} // namespace re::broker
