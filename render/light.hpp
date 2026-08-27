#pragma once

// render/light.hpp — RE-side ReLight (uniform-ready) for render::View: the RE View holds a vector<ReLight> that LightMapper produces from scene::Light before the drawLayer loop, world-space per the LightMapper decision, empty vector keeps the existing fixed headlight fallback so FR gates stay byte-identical, SPEC §12.3 and T19 stretch.
//
// ReLight is the derived uniform-ready RE type that LightMapper produces from
// scene::Light. Stored world-space per SPEC §12.5 world-space decision
// (posWS/dirWS forwarded, view-space conversion belongs to shader's uView).
// View holds vector<ReLight> values (variant values in spec, here one struct
// with LightType discriminant for stretch minimal) and uploads per view before
// drawLayer loop; empty vector = unlit as before (existing headlight fallback
// preserves FR gates byte-identical).

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace re::render {

/// RE light type — mirrors scene::LightType without including scene/ (render
/// never includes scene/ per disposition guardrail; broker is the only lib that
/// may include both, so RE keeps its own discriminant and LightMapper converts
/// explicitly — one enum per side keeps the layering pure and the audit green).
enum class ReLightType : uint8_t {
    Directional = 0,
    Point = 1,
    Spot = 2,
};

/// RE-side light — uniform-ready counterpart of scene::Light.
///
/// Fields mirror scene::Light but are the RE-persistent, uniform-upload form
/// (ReLight in TASK wording). World-space positions/directions as spec'd.
/// Type is ReLightType (RE-local) so this header stays scene-free; the broker
/// LightMapper converts scene::LightType ↔ ReLightType explicitly.
struct ReLight {
    ReLightType type{ReLightType::Directional};
    glm::vec3 posWS{0.0f, 0.0f, 5.0f};
    glm::vec3 dirWS{0.0f, 0.0f, -1.0f};
    glm::vec3 color{1.0f, 1.0f, 1.0f}; ///< RGB (alpha stripped)
    float intensity{1.0f};
    float radius{10.0f};
    float innerCone{0.2618f};
    float outerCone{0.7854f};
    bool castShadows{false};

    bool operator==(const ReLight& o) const noexcept {
        return type == o.type && posWS == o.posWS && dirWS == o.dirWS && color == o.color &&
               intensity == o.intensity && radius == o.radius && innerCone == o.innerCone &&
               outerCone == o.outerCone && castShadows == o.castShadows;
    }
    bool operator!=(const ReLight& o) const noexcept { return !(*this == o); }
};

} // namespace re::render
