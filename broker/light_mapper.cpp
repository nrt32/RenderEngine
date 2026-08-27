// broker/light_mapper.cpp — LightMapper implementation that provides stateless world-space forwarding from app Light to RE ReLight: positions and directions are forwarded in world space and the direction is normalized for uniform readiness, the view-space conversion stays in the shader's view matrix, not in the mapper, SPEC §12.3 and T19 stretch.
//
// Stateless world-space forwarding (SPEC §12.5): AppLight pos/dir are world-space
// (dirWS/posWS in ReLight); mapper just forwards. Normalizes dir for uniform
// readiness.

#include "broker/light_mapper.hpp"

#include <glm/geometric.hpp>

namespace re::broker {

data::Result<render::ReLight> LightMapper::map(const scene::Light& app,
                                               const scene::TranslateContext& /*ctx*/) const {
    render::ReLight out;
    out.type = static_cast<render::ReLightType>(static_cast<uint8_t>(app.type));
    out.posWS = app.pos;
    // Normalize direction for uniform readiness (zero dir kept as is).
    const float len = glm::length(app.dir);
    out.dirWS = (len > 1e-6f) ? (app.dir / len) : app.dir;
    out.color = glm::vec3(app.color.r, app.color.g, app.color.b);
    out.intensity = app.intensity;
    out.radius = app.radius;
    out.innerCone = app.innerCone;
    out.outerCone = app.outerCone;
    out.castShadows = app.castShadows;
    return data::makeValue<render::ReLight>(out);
}

} // namespace re::broker
