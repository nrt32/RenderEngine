// broker/line_object_mapper.cpp — LineObjectMapper cached translation (no raw gl*).

#include "broker/line_object_mapper.hpp"

namespace re::broker {

data::Result<render::ReLineObject> LineObjectMapper::map(
    const scene::LineObject& app, const scene::TranslateContext& /*ctx*/) const {
    render::ReLineObject out;
    out.segments.reserve(app.segments.size());
    for (const auto& seg : app.segments) {
        render::LineInstance inst;
        glm::vec4 wa = app.transform * glm::vec4(seg.a, 1.0f);
        glm::vec4 wb = app.transform * glm::vec4(seg.b, 1.0f);
        if (wa.w != 0.0f) wa /= wa.w;
        if (wb.w != 0.0f) wb /= wb.w;
        inst.a = glm::vec3(wa);
        inst.b = glm::vec3(wb);
        inst.color = app.color;
        inst.width = app.width;
        inst.worldUnits = app.worldUnits;
        switch (app.cap) {
            case scene::LineCap::Round: inst.cap = render::LineCap::Round; break;
            case scene::LineCap::Square: inst.cap = render::LineCap::Square; break;
            default: inst.cap = render::LineCap::Square; break;
        }
        switch (app.join) {
            case scene::LineJoin::Miter: inst.join = render::LineJoin::Miter; break;
            case scene::LineJoin::Bevel: inst.join = render::LineJoin::Bevel; break;
            default: inst.join = render::LineJoin::Miter; break;
        }
        inst.miterLimit = app.miterLimit;
        inst.dashLength = app.dash.dashLength;
        inst.gapLength = app.dash.gapLength;
        inst.dashOffset = app.dash.offset;
        inst.dashed = !app.dash.isSolid();
        out.segments.push_back(std::move(inst));
    }
    return data::makeValue<render::ReLineObject>(std::move(out));
}

} // namespace re::broker
