#version 450 core
layout(location = 0) in vec2 aPos;
uniform vec3 uCenterWS;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec2 uViewport;
uniform float uRadiusScreen;
uniform float uRadiusWorld;
uniform vec4 uColor;
uniform int uFillMode;
uniform float uFillParam;
out vec2 vMapping;
out vec3 vCenterWS;
out float vRadius;
flat out int vFillMode;
out vec4 vColor;
void main() {
    vMapping = aPos;
    vCenterWS = uCenterWS;
    vRadius = uRadiusWorld;
    vFillMode = uFillMode;
    vColor = uColor;
    vec4 centerClip = uProj * uView * vec4(uCenterWS, 1.0);
    // Guard against behind-camera (w <= 0) — clamp to near plane by keeping original clip
    vec2 centerNDC = centerClip.xy / max(centerClip.w, 0.0001);
    vec2 centerScreen = (centerNDC * 0.5 + 0.5) * uViewport;
    vec2 screenPos = centerScreen + aPos * uRadiusScreen;
    vec2 ndcPos = (screenPos / uViewport * 2.0 - 1.0);
    gl_Position = vec4(ndcPos * centerClip.w, centerClip.z, centerClip.w);
}
