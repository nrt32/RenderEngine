// view_mapper.cpp — ViewMapper implementation that composes LightMapper for per-View lights: the View owns many lights (vector<Light>), each element is forwarded via LightMapper world-space handling before the RE View uploads the uniform-ready ReLight array once before its drawLayer loop; empty input keeps the existing headlight fallback so empty lights stay byte-identical to the Phong-only gate, T19 stretch.

#include "broker/view_mapper.hpp"

#include <glm/geometric.hpp>

#include <vector>

#include "broker/light_mapper.hpp"

namespace re::broker {

data::Result<std::vector<render::ReLight>> ViewMapper::mapLights(
    const std::vector<scene::Light>& appLights,
    IMapper<scene::Light, render::ReLight>* mapper,
    const scene::TranslateContext& ctx) {
    std::vector<render::ReLight> out;
    out.reserve(appLights.size());
    for (const auto& l : appLights) {
        if (mapper) {
            auto r = mapper->map(l, ctx);
            if (r.failed()) return data::makeError<std::vector<render::ReLight>>(r.error().code, r.error().message);
            out.push_back(*r);
        } else {
            // No mapper registered → delegate to a stateless LightMapper instance
            // so the world-space forwarding logic stays single-sourced (LightMapper
            // is the sole definition of dir normalization and field mapping; this
            // fallback just keeps the suite green when the composition root hasn't
            // registered LightMapper yet without duplicating its body).
            LightMapper fallback;
            auto r = fallback.map(l, ctx);
            if (r.failed()) return data::makeError<std::vector<render::ReLight>>(r.error().code, r.error().message);
            out.push_back(*r);
        }
    }
    return data::makeValue<std::vector<render::ReLight>>(std::move(out));
}

} // namespace re::broker
