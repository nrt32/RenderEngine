#pragma once

// view_mapper.hpp — ViewMapper facade that composes per-View field mappers including LightMapper for the lights vector: each View's vector<Light> is forwarded element-wise to vector<ReLight> and uploaded once before the drawLayer loop, empty stays unlit with the existing headlight fallback, SPEC §11.4 and T19 stretch.
//
// Facade that composes per-field mappers for a View (CameraMapper + PlaneMapper
// + LightMapper[] + per-item mappers). This iteration's ViewMapper is the
// ViewSynchronizer itself (which already composes the field mappers); this
// header documents the composition and re-exports LightMapper so broker
// consumers can register it at the composition root without including the
// concrete LightMapper header directly (DIP: depend on abstraction).
//
// T19 adds lights: ViewMapper composes LightMapper (vector<Light> -> vector<ReLight>)
// and ViewSynchronizer calls it per-View before drawLayer (one upload per view).
// Empty vector = unlit fallback (existing headlight preserved).

#include <vector>

#include "broker/light_mapper.hpp"

namespace re::broker {

/// ViewMapper — per-View field composition (light subset).
///
/// Owns no state; delegates per-field translation. Light mapping is
/// vector<Light> -> vector<ReLight> via LightMapper::map per element.
struct ViewMapper {
    /// Translate app lights to RE lights via mapper.
    static data::Result<std::vector<render::ReLight>> mapLights(
        const std::vector<scene::Light>& appLights,
        IMapper<scene::Light, render::ReLight>* mapper,
        const scene::TranslateContext& ctx);
};

} // namespace re::broker
