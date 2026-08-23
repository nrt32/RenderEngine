#version 450 core
// contour.geom.glsl — ContourRenderer's GPU plane-intersection outline
// (FR-app.3, V3.8b T11; clip pattern of slice_clip.geom.glsl).
//
// Each input triangle is classified against the world-space clip plane
// (kept-side convention identical to slice_clip.geom.glsl: signed distance
// d = dot(uPlaneNormal, P - uPlanePoint)). A triangle strictly straddling the
// plane contributes ONE outline segment: the segment between its two edge
// crossing points, computed exactly on the GPU (t = d[i] / (d[i] - d[j])).
// Tangent / coplanar triangles contribute nothing, so the emitted primitive
// set is exactly the plane-intersection OUTLINE of the mesh.
//
// The two crossing points are then drawn as a THICK LINE: OpenGL 4.6 core
// caps glLineWidth at 1.0, and the FR-app.3 acceptance band is +/-2 px around
// the analytic curve, so the geometry shader emits the classic screen-space
// thick-line primitive instead — one quad (triangle_strip, 4 vertices) per
// segment, expanded by uHalfWidthPx perpendicular to the projected segment
// and extended by uHalfWidthPx beyond each endpoint (square caps), so every
// pixel center within uHalfWidthPx of the analytic segment lies inside the
// quad. This is the standard GPU replacement for deprecated wide lines.

layout(triangles) in;
layout(triangle_strip, max_vertices = 4) out;

in vec3 vWorldPos[];

uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uPlaneNormal;
uniform vec3 uPlanePoint;
uniform vec2 uViewport;     // current viewport size in pixels (w, h)
uniform float uHalfWidthPx; // half stroke width in pixels (FR-app.3 band = 2)

void main() {
    vec3 P[3] = vec3[](vWorldPos[0], vWorldPos[1], vWorldPos[2]);

    // Signed distances + sign class (-1 / 0 / +1), as in slice_clip.geom.glsl.
    float d[3];
    int s[3];
    int posCount = 0;
    int negCount = 0;
    for (int i = 0; i < 3; ++i) {
        d[i] = dot(uPlaneNormal, P[i] - uPlanePoint);
        s[i] = (d[i] > 0.0) ? 1 : ((d[i] < 0.0) ? -1 : 0);
        if (s[i] > 0) {
            ++posCount;
        } else if (s[i] < 0) {
            ++negCount;
        }
    }
    // Fully on one side, tangent, or coplanar: no outline segment.
    if (posCount == 0 || negCount == 0) {
        return;
    }

    // Collect the crossing points of every edge whose endpoints have different
    // sign classes; deduplicate near-coincident points (a plane through a
    // vertex yields coincident crossings at that vertex).
    vec3 cp[3];
    int k = 0;
    for (int i = 0; i < 3; ++i) {
        int j = (i + 1) % 3;
        if (s[i] != s[j]) {
            float t = d[i] / (d[i] - d[j]); // signs differ => d[i] - d[j] != 0
            vec3 c = P[i] + t * (P[j] - P[i]);
            bool duplicate = false;
            for (int q = 0; q < k; ++q) {
                if (distance(c, cp[q]) < 1.0e-6) {
                    duplicate = true;
                }
            }
            if (!duplicate && k < 3) {
                cp[k++] = c;
            }
        }
    }
    if (k != 2) {
        return; // degenerate (single distinct crossing): no segment
    }

    // Project both endpoints to continuous pixel coordinates (pixel (px, py)
    // spans [px, px+1]; centers at half-integers). NDC (-1,-1) is the
    // viewport's bottom-left corner.
    vec4 ca = uProj * uView * vec4(cp[0], 1.0);
    vec4 cb = uProj * uView * vec4(cp[1], 1.0);
    if (ca.w <= 0.0 || cb.w <= 0.0) {
        return; // behind the eye: deterministic skip (no fragment)
    }
    vec2 pa = (ca.xy / ca.w * 0.5 + 0.5) * uViewport;
    vec2 pb = (cb.xy / cb.w * 0.5 + 0.5) * uViewport;

    // Screen-space thick line: perpendicular half-width offset + square caps
    // extending each end by the same half-width.
    vec2 e = pb - pa;
    float len = length(e);
    if (len < 1.0e-6) {
        return; // degenerate projection: no visible segment
    }
    vec2 dir = e / len;
    vec2 nrm = vec2(-dir.y, dir.x) * uHalfWidthPx;
    vec2 capDir = dir * uHalfWidthPx;

    vec2 q0 = pa - capDir + nrm;
    vec2 q1 = pa - capDir - nrm;
    vec2 q2 = pb + capDir + nrm;
    vec2 q3 = pb + capDir - nrm;

    // Back to clip space; keep each endpoint's depth (depth test is off in
    // v1, so z only has to stay inside the clip volume).
    gl_Position = vec4((q0 / uViewport * 2.0 - 1.0) * ca.w, ca.z, ca.w);
    EmitVertex();
    gl_Position = vec4((q1 / uViewport * 2.0 - 1.0) * ca.w, ca.z, ca.w);
    EmitVertex();
    gl_Position = vec4((q2 / uViewport * 2.0 - 1.0) * cb.w, cb.z, cb.w);
    EmitVertex();
    gl_Position = vec4((q3 / uViewport * 2.0 - 1.0) * cb.w, cb.z, cb.w);
    EmitVertex();
    EndPrimitive();
}
