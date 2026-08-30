#pragma once

// render/re_scene/line_object.hpp — ReLineObject RE-minimal handle for GPU polyline stroking (V7 T9, SPEC §12.4).
//
// Lines use the state-of-art SSBO plus gl_VertexID 6-vertex view-quad strip per segment (no geometry shader, no CPU expansion) per the locked V7 design at 2026-08-30: each segment a→b in world space is expanded on the GPU into a view-space quad a±n·wA, b±n·wB with n = normalize(perp(viewport·(b−a))) so the width stays constant in screen space while depth is interpolated from clip depths. The CPU populates an SSBO of LineSegmentSSBO{a,b,color,width,s0,s1,worldUnits,dashLength,gapLength,offset,cap,join,miterLimit} with s as cumulative arc-length length(viewport·(b−a)) so the shader evaluates inDash = step(mod(s+offset, patternLen), dashLen) via Rougier mod(s) with smoothstep(fwidth) AA at the dash transition, discarding gaps and premultiplying fragColor = vec4(color.rgb, color.a * alpha) for LinkedListOIT. Joins use miterLimit 4→bevel fallback via prev/next at polyline nodes, caps are round (analytic disc halo) vs square (sharp cut), and worldUnits width is scaled via projection delta like points. This RE type therefore keeps only RE-direct fields: a,b (derived world-space endpoints after object.transform), color (uniform-ready), width (uniform-ready), DashPattern (derived from dashLength/gapLength/offset), plus cap/join/miterLimit/worldUnits uniform-ready toggles. No verbatim data::Mesh positions copy and no scene::LineObject verbatim — only derived and uniform-ready handles, satisfying asset_indirection and RE-minimal discipline. (V7 T9)

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace re::render::re_scene {

/// RE-minimal line cap (mirrors scene::LineCap without including scene).
enum class LineCap : uint8_t { Round = 0, Square = 1 };

/// RE-minimal line join (mirrors scene::LineJoin without including scene).
enum class LineJoin : uint8_t { Miter = 0, Bevel = 1 };

/// RE-minimal dash pattern for Rougier mod(s) dash (mirrors scene::DashPattern).
struct DashPattern {
    float dashLength{8.0f}; ///< uniform-ready — dash run length in viewport px (solid when gap≈0)
    float gapLength{4.0f};  ///< uniform-ready — gap run length in viewport px
    float offset{0.0f};     ///< uniform-ready — arc-length offset for animation

    bool isSolid() const noexcept { return gapLength <= 1e-6f; }
    float patternLength() const noexcept { return dashLength + gapLength; }
};

/// RE-minimal single line segment (V7 T9).
///
/// Mirrors scene::LineObject's per-segment payload {vec3 a,b, vec4 color, float width, DashPattern}
/// with only RE-direct fields: a,b are world-space after object.transform (derived), color/width/dash/cap/join are uniform-ready for the view-quad strip shader. WorldUnits toggle selects constant-px vs world-scaled width via projection delta. DashPattern is expanded as dashLength/gapLength/offset scalars plus dashed flag for the SSBO upload (uniform-ready) to keep std430 array stride 16.
struct ReLineObject {
    glm::vec3 a{0.0f, 0.0f, 0.0f};          ///< derived — world-space start = transform * vec4(localA,1)
    glm::vec3 b{0.0f, 0.0f, 1.0f};          ///< derived — world-space end   = transform * vec4(localB,1)
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f}; ///< uniform-ready — stroke color (premul alpha for OIT)
    float width{2.0f};                      ///< uniform-ready — stroke width (world when worldUnits true, else px)
    bool worldUnits{false};                 ///< uniform-ready — world vs px toggle (like points)
    LineCap cap{LineCap::Square};           ///< uniform-ready — end shape Round vs Square
    LineJoin join{LineJoin::Miter};         ///< uniform-ready — node shape Miter vs Bevel
    float miterLimit{4.0f};                 ///< uniform-ready — miter 4→bevel fallback threshold
    float dashLength{8.0f};                 ///< uniform-ready — DashPattern dash run length (part of DashPattern)
    float gapLength{0.0f};                  ///< uniform-ready — DashPattern gap run length (part of DashPattern)
    float dashOffset{0.0f};                 ///< uniform-ready — DashPattern arc-length offset (part of DashPattern)
    bool dashed{false};                     ///< derived — whether dash pattern is active (gap > 1e-6)
    DashPattern dash() const noexcept { return DashPattern{dashLength, gapLength, dashOffset}; }
};

/// RE-minimal polyline collection for SSBO view-quad strip draws (V7 T9, SPEC §12.4, line state-of-art).
///
/// The V7 line pipeline batches polyline segments into an SSBO of LineSegmentSSBO with cumulative arc-length s for Rougier dash and fwidth AA; this collection mirrors that by holding a vector of ReLineObject where each element's a/b are world-space after transform (derived), color/width/dash/cap/join are uniform-ready scalars for the view-quad shader, and the shared stroke style is preserved without CPU expansion or geometry shader. (V7 T9)
struct ReLineScene {
    std::vector<ReLineObject> segments; ///< handle — vector of line segments (uniform-ready per element)
};

} // namespace re::render::re_scene
