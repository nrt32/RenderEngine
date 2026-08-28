// broker/mesh_slice_object_mapper.cpp — MeshSliceObjectMapper contextual
// translation: view plane + mesh handle + canonical material → SliceScene.

#include "broker/mesh_slice_object_mapper.hpp"

#include "broker/plane_mapper.hpp"

namespace re::broker {

MeshSliceObjectMapper::MeshSliceObjectMapper(
    std::shared_ptr<render::AssetRegistry> registry,
    std::shared_ptr<MaterialMapper> materials)
    : registry_(std::move(registry)),
      materials_(materials ? std::move(materials)
                           : registry_ ? std::make_shared<MaterialMapper>(registry_)
                                       : nullptr) {}

data::Result<render::SliceScene> MeshSliceObjectMapper::map(
    const scene::MeshSliceObject& app, const scene::TranslateContext& ctx) const {
    if (!app.mesh) {
        return data::makeError<render::SliceScene>(
            1, "MeshSliceObjectMapper: null mesh asset reference");
    }
    if (!registry_) {
        return data::makeError<render::SliceScene>(
            4, "MeshSliceObjectMapper: null AssetRegistry");
    }
    if (!ctx.view.hasPlane()) {
        return data::makeError<render::SliceScene>(
            2, "MeshSliceObjectMapper: the view carries no plane — a slice "
               "has no clip geometry without one (the plane lives on the View "
               "per the broker contextual rule)");
    }

    // Plane conversion through the ONE PlaneMapper rule. A World-space plane
    // passes through even with an absent VolumeContext (LSP: preconditions
    // stay weak); a VoxelIndex plane needs the volume role.
    scene::VolumeContext emptyVolume{};
    auto clip = convertViewPlaneToClipPlane(*ctx.view.viewPlane,
                                            ctx.hasVolume() ? *ctx.volume : emptyVolume);
    if (clip.failed()) {
        return data::makeError<render::SliceScene>(
            3, std::string{"MeshSliceObjectMapper: view plane conversion "
                           "failed: "} +
                   clip.error().message);
    }
    if (!materials_) {
        return data::makeError<render::SliceScene>(
            4, "MeshSliceObjectMapper: null MaterialMapper");
    }
    auto material = materials_->map(app.presentation, ctx);
    if (material.failed()) {
        return data::makeError<render::SliceScene>(material.error().code,
                                                   material.error().message);
    }

    auto h = registry_->registerAsset(*app.mesh);
    if (h.failed()) {
        return data::makeError<render::SliceScene>(h.error().code,
                                                   h.error().message);
    }

    render::SliceScene out;
    render::MeshInstance inst;
    inst.mesh = *h;
    inst.material = *material;
    inst.model = app.transform;
    out.meshes.push_back(inst);
    out.plane = *clip; // consumed by SliceRenderer's geometry-shader clip
    return data::makeValue<render::SliceScene>(out);
}

bool MeshSliceObjectMapper::isCacheHit(const AppType& app,
                                       const scene::TranslateContext& ctx,
                                       const Entry& e) const {
    const bool samePlane = e.hasPlane == ctx.view.hasPlane() &&
                           (!e.hasPlane || e.plane == *ctx.view.viewPlane);
    return samePlane && e.generation == app.generation;
}

void MeshSliceObjectMapper::fillEntry(Entry& e, const AppType& app,
                                      const scene::TranslateContext& ctx,
                                      const ReType& instance) const {
    e.generation = app.generation;
    e.instance = instance;
    e.hasPlane = ctx.view.hasPlane();
    e.plane = e.hasPlane ? *ctx.view.viewPlane : scene::PlaneDesc{};
}

} // namespace re::broker
