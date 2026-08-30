// broker/point_object_mapper.cpp — PointObjectMapper cached translation (no raw gl*).

#include "broker/point_object_mapper.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace re::broker {

data::Result<render::RePointObject> PointObjectMapper::map(
    const scene::PointObject& app, const scene::TranslateContext& /*ctx*/) const {
    render::RePointObject out;
    // Transform local position by object's world transform.
    glm::vec4 wp = app.transform * glm::vec4(app.position, 1.0f);
    if (wp.w != 0.0f) wp /= wp.w;
    out.pos = glm::vec3(wp);
    out.radius = app.radius;
    out.worldUnits = app.worldUnits;
    out.color = app.color;
    // Map PointFill directly (same underlying values 0..2).
    switch (app.fill) {
        case scene::PointFill::Solid: out.fill = render::re_scene::PointFill::Solid; break;
        case scene::PointFill::Hollow: out.fill = render::re_scene::PointFill::Hollow; break;
        case scene::PointFill::GridDashed: out.fill = render::re_scene::PointFill::GridDashed; break;
        default: out.fill = render::re_scene::PointFill::Solid; break;
    }
    out.fillParam = app.fillParam;
    return data::makeValue<render::RePointObject>(std::move(out));
}

} // namespace re::broker
