#pragma once

// scene/light.hpp — Light description for scene value library (SPEC §12.3, T19 stretch).
//
// App-side light: many per View (vector<Light> lights). Type is closed set
// 3 (Directional/Point/Spot) per SPEC §1/§12 non-goal: Phong-only stays but
// light hierarchy is spec-only until promoted — this header lands the value
// type so View gains explicit lights without breaking Phong-only gate (empty
// vector = existing headlight/unlit 2D behavior preserved via View defaults).
// Per-light fields are world-space per SPEC §12.5 LightMapper world space
// decision (dirWS/posWS forwarded, view-space conversion belongs to shader).

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace re::scene {

/// Light type (closed set of three kinds — Directional, Point, Spot — chosen as a closed variant per the material/light OCP discussion that keeps the type set small and operations visitor-based; the hierarchy stays spec-only until Phong-only is promoted, so this enum is the stable discriminant for both app and RE sides, SPEC §12.3).
enum class LightType : uint8_t {
    Directional = 0,
    Point = 1,
    Spot = 2,
};

/// App-side light descriptor — value type, GL-free, RE-free.
///
/// Default is a directional headlight (−Z) with white color intensity 1.
/// Spot carries pos+dir+cone angles; Point carries pos+radius+attenuation;
/// Directional carries dir only — unused fields are ignored per type but kept
/// for uniform struct size (no variant this iteration — one struct keeps
/// View::lights vector trivial).
struct Light {
    LightType type{LightType::Directional};
    glm::vec3 pos{0.0f, 0.0f, 5.0f};     ///< World-space position (Point/Spot).
    glm::vec3 dir{0.0f, 0.0f, -1.0f};    ///< World-space direction (Directional/Spot), normalized.
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; ///< RGBA color (alpha ignored for lights).
    float intensity{1.0f};
    float radius{10.0f};                ///< Attenuation radius (Point/Spot).
    float innerCone{0.2618f};            ///< Spot inner cone (radians, ~15deg).
    float outerCone{0.7854f};            ///< Spot outer cone (radians, ~45deg).
    bool castShadows{false};

    bool operator==(const Light& o) const noexcept {
        return type == o.type && pos == o.pos && dir == o.dir && color == o.color &&
               intensity == o.intensity && radius == o.radius && innerCone == o.innerCone &&
               outerCone == o.outerCone && castShadows == o.castShadows;
    }
    bool operator!=(const Light& o) const noexcept { return !(*this == o); }
};

} // namespace re::scene
