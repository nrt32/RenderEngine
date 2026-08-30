#pragma once

// scene/line_style.hpp — line caps, joins, and dash pattern value types for LineObject (V7 T2).
//
// Lines use the state-of-art SSBO+gl_VertexID 6-vertex view-quad strip per segment (no geometry shader, no CPU expansion) where each segment a→b is expanded into a view-space quad a±n·w/2, b±n·w/2 with n = normalize(perp(viewport·(b−a))) so the width stays constant in screen space. The cap controls the end shape (Round: analytic fwidth alpha halo, Square: sharp cut at segment endpoint), the join controls the polyline node shape (Miter with miterLimit 4→bevel fallback when the miter over-extends at acute angles, versus unconditional Bevel), and the dash pattern controls visibility along the arc-length s (cumulative viewport length) via Rouger mod(s, patternLen) with configurable dash/gap lengths and an offset for animation. Dash alpha is combined with analytic fwidth anti-aliasing (smoothstep) before premultiplied composite into LinkedListOIT. Like all scene/ headers, these are pure values — no GL/RE — so disposition_scene and gpu_api_ownership remain satisfied, and worldUnits toggles between world-scaled width (like points) and constant pixel width without touching render. (V7 T2)

#include <cstdint>

namespace re::scene {

/// Line cap — controls the end shape of a segment's view-quad strip.
///
/// Round draws a semicircular end (distance-to-stroke with analytic fwidth AA,
/// headlight shade), Square cuts sharply at the endpoint (sharp rect, cheaper
/// but shows alias at wide widths). Both honour worldUnits toggle for width.
enum class LineCap : uint8_t {
    Round = 0,
    Square = 1
};

/// Line join — controls the polyline node between consecutive segments.
///
/// Miter extends the outer edges until they meet (sharp corner, limited by
/// miterLimit 4→bevel when the extension would be too long and produce spikes
/// at acute angles); Bevel unconditionally cuts the corner flat (no extension,
/// always bevel, cheaper). Mirrors Contour miter discipline but via SSBO
/// prev/next expand, not GS.
enum class LineJoin : uint8_t {
    Miter = 0,
    Bevel = 1
};

/// Line style — high-level stroke style tag for LineObject (V7 T2, additive per TASKS.md D).
///
/// The V7 task lists LineObject as carrying both a LineStyle style tag and a DashPattern dash; the style is the semantic solid/dashed selector while DashPattern carries the analytic run-lengths (dashLength, gapLength, offset) consumed by the Rougier mod(s,patternLen) dash in the shader with smoothstep(fwidth) AA. Keeping style as a tiny enum preserves the GL-free, RE-free disposition of scene/ and lets the broker hash style+dash together with width/worldUnits/cap/join/miterLimit for its cache key without including render headers. Solid maps to gapLength≈0 (DashPattern::isSolid()), Dashed maps to patterned dash; the two-level representation (style enum plus dash pattern) mirrors the material_desc LineMaterialDesc split and keeps the scene value additive for future stipple extensions without editing existing mappers (OCP via visitor overloads). (V7 T2)
enum class LineStyle : uint8_t {
    Solid = 0,
    Dashed = 1
};

/// Dash pattern — run-length along arc-length s for LineRenderer's Rougier mod(s) dash.
///
/// A segment contributes its viewport length to cumulative s0..s1 (CPU-populated
/// SSBO field LineSegmentSSBO{a,b,color,width,s0,s1,worldUnits}); the shader
/// evaluates inDash = step(mod(s+offset, dashLen+gapLen), dashLen) with
/// smoothstep(fwidth) AA at the transition, discarding gaps. Solid lines use
/// gapLen=0 (or length 0 pattern).
struct DashPattern {
    float dashLength{8.0f};
    float gapLength{4.0f};
    float offset{0.0f};

    bool isSolid() const noexcept { return gapLength <= 1e-6f; }
    float patternLength() const noexcept { return dashLength + gapLength; }

    bool operator==(const DashPattern& o) const noexcept {
        return dashLength == o.dashLength && gapLength == o.gapLength && offset == o.offset;
    }
    bool operator!=(const DashPattern& o) const noexcept { return !(*this == o); }
};

} // namespace re::scene
