// broker/plane_mapper.cpp — PlaneMapper: voxel-index → world plane conversion
// (the single definition of the analytic rule; see the header comment).

#include "broker/plane_mapper.hpp"

#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace re::broker {

glm::mat4 indexPlacementFromModel(const glm::mat4& model,
                                  const glm::ivec3& dims) {
    // Undo the canonical unit-cube embedding (see the binding semantics in
    // the header): model coordinate i/(dim-1) -> index coordinate i means
    // indexPlacement = model * S(1/max(dim-1,1)) * T(-0.5) — the translate is
    // applied FIRST (rightmost), then the normalization scale, then the
    // model.
    const float sx = static_cast<float>(std::max(dims.x - 1, 1));
    const float sy = static_cast<float>(std::max(dims.y - 1, 1));
    const float sz = static_cast<float>(std::max(dims.z - 1, 1));
    return model *
           glm::scale(glm::mat4(1.0f), glm::vec3(1.0f / sx, 1.0f / sy, 1.0f / sz)) *
           glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f));
}

data::Result<render::ClipPlane> convertViewPlaneToClipPlane(
    const scene::PlaneDesc& plane, const scene::VolumeContext& volume) {
    // World planes are already in the stable RE representation: they pass
    // through unchanged and never consult (or require) the volume role —
    // LSP: absent roles keep preconditions weak.
    if (plane.space == scene::Space::World) {
        render::ClipPlane out;
        out.normal = plane.normal;
        out.point = plane.point;
        return data::makeValue<render::ClipPlane>(out);
    }
    // Degenerate volume contexts cannot define a voxel grid: reject loudly.
    if (volume.dims.x <= 0 || volume.dims.y <= 0 || volume.dims.z <= 0 ||
        !(volume.voxelSpacing > 0.0f)) {
        return data::makeError<render::ClipPlane>(
            2, "PlaneMapper: invalid VolumeContext (non-positive dims or "
               "spacing) for VoxelIndex conversion");
    }
    // Voxel-center convention: layer i spans [i, i + spacing], center i + 0.5
    // (in index units), then spacing scales and the model transforms into
    // world space. With identity model + unit spacing this is point + 0.5.
    const glm::vec3 centerOffset(0.5f);
    const glm::vec3 scaled = (plane.point + centerOffset) * volume.voxelSpacing;
    render::ClipPlane out;
    out.normal = plane.normal;  // declared world-space already (see header)
    out.point = glm::vec3(volume.volumeModel * glm::vec4(scaled, 1.0f));
    return data::makeValue<render::ClipPlane>(out);
}

data::Result<render::ClipPlane> PlaneMapper::map(
    const scene::PlaneDesc& app, const scene::TranslateContext& ctx) const {
    if (app.space == scene::Space::World) {
        render::ClipPlane out;
        out.normal = app.normal;
        out.point = app.point;
        return data::makeValue<render::ClipPlane>(out);
    }
    if (!ctx.hasVolume()) {
        return data::makeError<render::ClipPlane>(
            1, "PlaneMapper: Space::VoxelIndex plane needs the translate "
               "context's VolumeContext (dims/model/spacing) for the "
               "voxel-index to world conversion");
    }
    return convertViewPlaneToClipPlane(app, *ctx.volume);
}

} // namespace re::broker
