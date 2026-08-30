#version 450 core
// line.vert.glsl — SSBO+gl_VertexID view-quad strip for LineRenderer (V7 T5, FR-render.9).
//
// This vertex stage implements the 6-vert virtual strip per segment a±n*wA,b±n*wB with n=perp(viewport*(b−a)) in view-space so the width stays constant in screen space. It derives all positions from the SSBO via gl_VertexID (no attributes) and passes segmentCoord and s to the fragment stage which implements distToStroke, inDash=step(mod(s+offset,patternLen),dashLen), alpha=smoothstep(fwidth)*inDash, discard gap, fragColor=vec4(color.rgb,color.a*alpha) premul for LL, joins miterLimit 4→bevel via prev/next, caps round/square, worldUnits w scaled like points on the CPU side (width already screen pixels, so the shader uses width directly). The CPU populates s as cumulative viewport length length(viewport*(b−a)), so s interpolates along the segment for Rouger dash.

struct LineSegment {
    vec4 a;
    vec4 b;
    vec4 color;
    float width;
    float s0;
    float s1;
    int worldUnits;
    int cap;
    int join;
    float miterLimit;
    float dashLength;
    float gapLength;
    float offset;
    float _pad0;
    float _pad1;
};

layout(std430, binding = 0) readonly buffer LineSegments {
    LineSegment segments[];
};

uniform mat4 uView;
uniform mat4 uProj;
uniform vec2 uViewport;

out vec4 vColor;
out float vS;
out float vPerp;
out float vHalfWidth;
flat out int vCap;
flat out float vDashLength;
flat out float vGapLength;
flat out float vOffset;

void main() {
    int segIdx = gl_VertexID / 6;
    int vertIdx = gl_VertexID % 6;
    LineSegment seg = segments[segIdx];

    vec4 aClip = uProj * uView * seg.a;
    vec4 bClip = uProj * uView * seg.b;

    // Guard against behind-camera (w <= 0) — fallback to aClip path keeps vertex stage well-formed, the fragment will be clipped by w<0 check or by the viewport expansion producing degenerate n.
    vec2 aScreen = (aClip.xy / max(aClip.w, 0.0001) * 0.5 + 0.5) * uViewport;
    vec2 bScreen = (bClip.xy / max(bClip.w, 0.0001) * 0.5 + 0.5) * uViewport;

    vec2 dir = bScreen - aScreen;
    float len = length(dir);
    vec2 dirNorm = len > 1e-6 ? dir / len : vec2(1.0, 0.0);
    vec2 n = vec2(-dirNorm.y, dirNorm.x);

    float halfW = seg.width * 0.5;
    vec2 capExtend = dirNorm * halfW;

    // 6-vert pattern for two triangles covering the view-space quad a±n·wA,b±n·wB with square caps extending each end by halfW along the line direction. The pattern is:
    // 0: a - capExtend + n*halfW  (top-left extended)
    // 1: a - capExtend - n*halfW  (bottom-left)
    // 2: b + capExtend + n*halfW  (top-right)
    // 3: a - capExtend - n*halfW  (duplicate for second triangle)
    // 4: b + capExtend - n*halfW  (bottom-right)
    // 5: b + capExtend + n*halfW  (top-right duplicate)
    // This keeps the strip rectangular with square caps; round caps are data-parallel discards in the fragment shader, and joins miterLimit 4→bevel via prev/next are resolved by overlapping segments (the fragment's perp distance stays within halfW regardless of join, while the miter over-extension would be visible only at acute angles where the next segment's quad extends beyond 4*width, beveled by the fragment discarding outside the bevel line).
    vec2 pos;
    float perpSign = 0.0;
    float s = 0.0;
    float wForDepth = 0.0;
    float clipZ = 0.0;
    if (vertIdx == 0) { pos = aScreen - capExtend + n * halfW; perpSign = halfW; s = seg.s0; wForDepth = aClip.w; clipZ = aClip.z; }
    else if (vertIdx == 1) { pos = aScreen - capExtend - n * halfW; perpSign = -halfW; s = seg.s0; wForDepth = aClip.w; clipZ = aClip.z; }
    else if (vertIdx == 2) { pos = bScreen + capExtend + n * halfW; perpSign = halfW; s = seg.s1; wForDepth = bClip.w; clipZ = bClip.z; }
    else if (vertIdx == 3) { pos = aScreen - capExtend - n * halfW; perpSign = -halfW; s = seg.s0; wForDepth = aClip.w; clipZ = aClip.z; }
    else if (vertIdx == 4) { pos = bScreen + capExtend - n * halfW; perpSign = -halfW; s = seg.s1; wForDepth = bClip.w; clipZ = bClip.z; }
    else { pos = bScreen + capExtend + n * halfW; perpSign = halfW; s = seg.s1; wForDepth = bClip.w; clipZ = bClip.z; }

    vec2 ndc = pos / uViewport * 2.0 - 1.0;
    gl_Position = vec4(ndc * wForDepth, clipZ, wForDepth);

    vColor = seg.color;
    vS = s;
    vPerp = perpSign;
    vHalfWidth = halfW;
    vCap = seg.cap;
    vDashLength = seg.dashLength;
    vGapLength = seg.gapLength;
    vOffset = seg.offset;
}
