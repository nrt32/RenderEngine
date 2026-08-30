// broker/point_cloud_mapper.cpp — PointCloudMapper cached translation (no raw gl*).

#include "broker/point_cloud_mapper.hpp"

namespace re::broker {

data::Result<render::RePointCloudObject> PointCloudMapper::map(
    const scene::PointCloudObject& app, const scene::TranslateContext& /*ctx*/) const {
    render::RePointCloudObject out;
    out.points.reserve(app.points.size());
    for (const auto& pd : app.points) {
        render::PointInstance inst;
        glm::vec4 wp = app.transform * glm::vec4(pd.pos, 1.0f);
        if (wp.w != 0.0f) wp /= wp.w;
        inst.pos = glm::vec3(wp);
        inst.radius = pd.radius;
        inst.worldUnits = app.worldUnits;
        inst.color = pd.color;
        // Decode fillBits → PointFill (0 Solid, 1 Hollow, 2 GridDashed)
        auto sf = pd.fill();
        switch (sf) {
            case scene::PointFill::Solid: inst.fill = render::PointFill::Solid; break;
            case scene::PointFill::Hollow: inst.fill = render::PointFill::Hollow; break;
            case scene::PointFill::GridDashed: inst.fill = render::PointFill::GridDashed; break;
            default: inst.fill = render::PointFill::Solid; break;
        }
        inst.fillParam = 0.0f;
        out.points.push_back(std::move(inst));
    }
    return data::makeValue<render::RePointCloudObject>(std::move(out));
}

} // namespace re::broker
