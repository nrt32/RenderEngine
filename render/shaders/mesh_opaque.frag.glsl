#version 450 core
in vec3 vNormal;
uniform vec4 uBaseColor;
uniform vec3 uLightDir;
layout(location = 0) out vec4 oColor;
void main() {
    vec3 n = normalize(vNormal);
    vec3 ld = normalize(uLightDir);
    if (length(uLightDir) < 0.5) ld = vec3(0.0, 0.0, 1.0);
    float shade = max(dot(n, ld), 0.0);
    oColor = vec4(uBaseColor.rgb * shade, uBaseColor.a);
}
