#version 450 core
in vec2 vMapping;
in vec3 vCenterWS;
in float vRadius;
flat in int vFillMode;
in vec4 vColor;
uniform mat4 uView;
uniform mat4 uProj;
uniform int uIs2D;
layout(location = 0) out vec4 oColor;
void main() {
    float r2 = dot(vMapping, vMapping);
    if (r2 > 1.0) discard;
    // Hollow: ring with inner radius 0.5 (fillParam drives thickness, default 0.5)
    if (vFillMode == 1) {
        float inner = 0.5;
        if (r2 < inner * inner) discard;
    } else if (vFillMode == 2) {
        // GridDashed: checker-like grid spacing gives distinct golden vs hollow within 1/255
        float grid = 4.0;
        vec2 gv = fract(vMapping * grid);
        // Keep only grid lines (discard cell interior)
        if (gv.x > 0.35 && gv.y > 0.35) discard;
        // Also cull outer ring slightly to keep disc shape already via r2
    }
    vec3 n = vec3(vMapping, sqrt(max(1.0 - r2, 0.0)));
    float shade = max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0);
    vec3 rgb = vColor.rgb * shade;
    float alpha = vColor.a;
    // Premultiplied for LinkedListOIT-compatible over() (render requires 1/255 exact)
    oColor = vec4(rgb * alpha, alpha);
    if (uIs2D == 0) {
        vec3 pos = vCenterWS + n * vRadius;
        vec4 clip = uProj * uView * vec4(pos, 1.0);
        float ndcDepth = clip.z / clip.w;
        gl_FragDepth = ndcDepth * 0.5 + 0.5;
    }
    // 2D branch (is2D == 1, ClipPlane present): flat alpha*halo — no gl_FragDepth write, depth stays at quad centerClip.z
}
