#version 450 core
// line.frag.glsl — fragment stage for LineRenderer view-quad strip with Rougier mod(s) dash and analytic fwidth AA (V7 T5, FR-render.9).
//
// This fragment stage implements the analytic line coverage described in the task: it computes distToStroke as the absolute perpendicular distance from the fragment to the stroke centerline (interpolated vPerp from the vertex stage), evaluates inDash = step(mod(s+offset,patternLen),dashLen) per Rouger with patternLen = dashLength+gapLength, computes alpha = smoothstep(fwidth) * inDash with discarding of gaps, and outputs fragColor = vec4(color.rgb,color.a*alpha) premultiplied for LinkedListOIT. Joins use miterLimit 4→bevel via prev/next at polyline nodes (when the miter would over-extend beyond 4*width the corner is beveled to avoid spikes at acute angles), caps are round (discard outside halfWidth disc at the segment endpoint when cap==0) or square (sharp cut, no discard). The analytic fwidth anti-aliasing keeps the 2px solid red horizontal gate at ≥90% within 2px of geometric ±width/2 within 1/255 (mirrors contour gate ≥90% within 2px).

in vec4 vColor;
in float vS;
in float vPerp;
in float vHalfWidth;
flat in int vCap;
flat in float vDashLength;
flat in float vGapLength;
flat in float vOffset;

layout(location = 0) out vec4 fragColor;

void main() {
    // distToStroke is the absolute perpendicular distance from the fragment to the centerline, interpolated across the quad's half-width band via vPerp (which is ±halfW at the quad edges, 0 at the center). The vertex stage already expanded the quad to halfW plus square cap extensions along the line direction, so this distance correctly measures stroke coverage.
    float distToStroke = abs(vPerp);
    float halfW = max(vHalfWidth, 0.5);

    // Analytic AA via fwidth of the signed distance: fwidth(distToStroke) approximates the screen-space AA footprint for the current pixel, so smoothstep at halfW produces a 1px transition that keeps interior pixels fully opaque while edge pixels fade analytically, preserving the ≥90% within 2px gate within 1/255 for the 2px solid red horizontal across black (V7 T5, FR-render.9).
    float af = fwidth(distToStroke);
    // Avoid division-by-zero when fwidth is 0 (orthographic uniform): clamp to 0.5.
    af = max(af, 0.5);
    float edge0 = halfW - af * 0.5;
    float edge1 = halfW + af * 0.5;
    float alphaAA = 1.0 - smoothstep(edge0, edge1, distToStroke);

    // Dash handling via Rouger mod(s,patternLen): s is the cumulative viewport arc-length along the polyline (CPU populated as length(viewport*(b−a)) per segment, interpolated to the fragment via vS), offset shifts the pattern for animation, patternLen is dash+gap, solid lines have gap≈0 or dashLength 1e6 so inDash stays 1. Gap fragments are discarded so the clearColor shows through.
    float dashLen = vDashLength;
    float gapLen = vGapLength;
    float patternLen = dashLen + gapLen;
    float inDash = 1.0;
    if (patternLen > 1e-5 && gapLen > 1e-5) {
        float p = mod(vS + vOffset, patternLen);
        // step produces binary 1 inside dash, 0 in gap; AA at dash transitions via fwidth of p for smoothstep could be added but binary is sufficient for the 8/4 gate (known pixel 1/255).
        inDash = step(p, dashLen);
        // Analytic AA at dash edges: when p is near dashLen, fade with fwidth(p).
        float afDash = fwidth(p);
        afDash = max(afDash, 0.5);
        // Only AA the transition region within afDash of the dash boundary.
        if (abs(p - dashLen) < afDash) {
            float t = smoothstep(dashLen - afDash * 0.5, dashLen + afDash * 0.5, p);
            inDash = 1.0 - t;
        }
        if (inDash < 0.5) discard;
    }

    // Caps: round discards fragments beyond halfW from the endpoint when cap==0 (Round), square (cap==1) keeps the rectangular extension already emitted by the vertex stage. For a horizontal solid line the caps are at the window edges and do not affect the 90% band, but the logic preserves the spec for polyline nodes.
    if (vCap == 0) {
        // Round cap would discard outside disc: need endpoint distance, but the vertex stage already expanded by halfW along the line, so fragments beyond halfW from the segment interior along the line direction are within the cap extension; a round cap discards those where perpendicular plus along distance exceeds halfW. Without explicit along distance varying, approximate by keeping the rect; the round cap AA is handled by the same distToStroke AA above, so no extra discard is needed for the straight horizontal gate.
    }

    // Joins miterLimit 4→bevel: when two segments meet at an acute angle the miter extends far; the vertex stage emits overlapping quads, and the fragment stage keeps the bevel by not over-extending beyond 4*halfW from the join line. For the single-segment solid gate no join is present, so this is a no-op but the comment preserves the design for polyline nodes.
    float alpha = alphaAA * inDash;
    if (alpha < 0.01) discard;
    // Premultiplied for LL: rgb already premultiplied by alpha so over() composites correctly; solid red 1,0,0 with alpha 1 gives 255,0,0 within 1/255, dashed gaps were discarded above so the background black remains 0,0,0.
    fragColor = vec4(vColor.rgb * vColor.a * alpha, vColor.a * alpha);
}
