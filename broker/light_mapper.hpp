#pragma once

// light_mapper.hpp — LightMapper translates scene Light to render ReLight (implements the per-type mapper interface for lights, one file per mapper per guardrail broker_per_type; world-space forwarding, SPEC §12.3 and T19 stretch).
//
// One file per mapper (guardrail broker_per_type). Pure translation
// (ISP: IMapper only, not ICachedMapper — lights are value-mapped per View
// via ViewSynchronizer's per-field lightsGen cache, LightMapper itself is
// stateless; per-light dedup via value hash would be inside ViewSynchronizer
// if needed). World-space forwarding per SPEC §12.5 (pos/dir WS forwarded,
// view-space conversion belongs to shader's uView, not mapper).

#include "broker/i_mapper.hpp"
#include "render/light.hpp"
#include "scene/light.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Light mapper — scene::Light -> render::ReLight.
///
/// Pure translation forwarding world-space fields; no GL, no core/.
class LightMapper : public IMapper<scene::Light, render::ReLight> {
   public:
    using AppType = scene::Light;
    using ReType = render::ReLight;

    data::Result<render::ReLight> map(const scene::Light& app,
                                      const scene::TranslateContext& ctx) const override;
};

} // namespace re::broker
