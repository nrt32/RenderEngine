// broker/contour_mapper.cpp — ContourMapper pure translation (no raw gl*).

#include "broker/contour_mapper.hpp"

namespace re::broker {

data::Result<render::ContourObject> ContourMapper::map(
    const scene::ContourObject& app,
    const scene::TranslateContext& /*ctx*/) const {
    if (app.mesh == nullptr) {
        return data::makeError<render::ContourObject>(
            1, "ContourMapper: null mesh pointer");
    }
    if (registry_ == nullptr) {
        return data::makeError<render::ContourObject>(
            2, "ContourMapper: null AssetRegistry");
    }
    if (app.plane.space == scene::Space::VoxelIndex) {
        return data::makeError<render::ContourObject>(
            3, "ContourMapper: Space::VoxelIndex planes need the volume-"
               "context voxel-to-world conversion, which ContourMapper does "
               "not perform; supply a world-space PlaneDesc");
    }

    auto handle = registry_->registerAsset(*app.mesh);
    if (handle.failed()) {
        return data::makeError<render::ContourObject>(handle.error().code,
                                                      handle.error().message);
    }
    render::ContourObject out;
    out.mesh = *handle;
    out.plane.normal = app.plane.normal;
    out.plane.point = app.plane.point;
    out.color = app.color;
    out.model = app.transform;
    return data::makeValue<render::ContourObject>(out);
}

} // namespace re::broker
