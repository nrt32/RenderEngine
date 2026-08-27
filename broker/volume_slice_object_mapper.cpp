// broker/volume_slice_object_mapper.cpp — VolumeSliceObjectMapper contextual
// translation: the view's plane is converted through the ONE PlaneMapper rule
// against a volume context derived from the mapped object itself.

#include "broker/volume_slice_object_mapper.hpp"

#include "broker/plane_mapper.hpp"

namespace re::broker {

data::Result<render::VolumeSliceInstance> VolumeSliceObjectMapper::map(
    const scene::VolumeSliceObject& app,
    const scene::TranslateContext& ctx) const {
    if (!app.volume) {
        return data::makeError<render::VolumeSliceInstance>(
            1, "VolumeSliceObjectMapper: null volume dataset reference");
    }
    if (!registry_) {
        return data::makeError<render::VolumeSliceInstance>(
            4, "VolumeSliceObjectMapper: null AssetRegistry");
    }
    if (!ctx.view.hasPlane()) {
        return data::makeError<render::VolumeSliceInstance>(
            2, "VolumeSliceObjectMapper: the view carries no plane — a "
               "volume slice has no geometry without one (the plane lives on "
               "the View per the broker contextual rule)");
    }

    // Volume context FROM THE MAPPED OBJECT (not from a view-global volume):
    // its dims define the voxel grid and its transform places it in world —
    // recovered into the INDEX-space placement the plane conversion consumes
    // (broker::indexPlacementFromModel; for the standard display models this
    // is exactly the pure axis permutation), so each slice item converts the
    // shared view plane correctly for its own display frame.
    scene::VolumeContext volume;
    volume.volumeModel = indexPlacementFromModel(
        app.transform,
        glm::ivec3{static_cast<int>(app.volume->sizeX()),
                   static_cast<int>(app.volume->sizeY()),
                   static_cast<int>(app.volume->sizeZ())});
    volume.dims = glm::ivec3{static_cast<int>(app.volume->sizeX()),
                             static_cast<int>(app.volume->sizeY()),
                             static_cast<int>(app.volume->sizeZ())};
    volume.voxelSpacing = 1.0f;
    auto clip = convertViewPlaneToClipPlane(*ctx.view.viewPlane, volume);
    if (clip.failed()) {
        return data::makeError<render::VolumeSliceInstance>(
            3, std::string{"VolumeSliceObjectMapper: view plane conversion "
                           "failed: "} +
                   clip.error().message);
    }

    auto h = registry_->registerVolume(app.volume);
    if (h.failed()) {
        return data::makeError<render::VolumeSliceInstance>(h.error().code,
                                                           h.error().message);
    }
    render::VolumeSliceInstance out;
    out.handle = *h;
    out.dataset = app.volume; // retained for CPU dims, GPU identity is handle
    out.transferFunction = app.transferFunction;
    out.model = app.transform;
    out.plane = *clip;
    return data::makeValue<render::VolumeSliceInstance>(out);
}

data::Result<render::VolumeSliceInstance> VolumeSliceObjectMapper::mapCached(
    const scene::VolumeSliceObject& app, const scene::TranslateContext& ctx) {
    auto it = cache_.find(app.id);
    if (it != cache_.end()) {
        const Entry& e = it->second;
        const bool samePlane =
            e.hasPlane == ctx.view.hasPlane() &&
            (!e.hasPlane || e.plane == *ctx.view.viewPlane);
        if (samePlane && e.generation == app.generation) {
            return data::makeValue<render::VolumeSliceInstance>(e.instance);
        }
    }
    auto r = map(app, ctx);
    if (r.ok()) {
        Entry entry;
        entry.generation = app.generation;
        entry.hasPlane = ctx.view.hasPlane();
        entry.plane = entry.hasPlane ? *ctx.view.viewPlane : scene::PlaneDesc{};
        entry.instance = *r;
        cache_[app.id] = std::move(entry);
    } else {
        cache_.erase(app.id);
    }
    return r;
}

void VolumeSliceObjectMapper::invalidate(uint64_t id) {
    cache_.erase(id);
}

} // namespace re::broker
