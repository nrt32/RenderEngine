#version 450 core
in vec3 vNormal;
uniform vec4 uBaseColor;
layout(location = 0) out vec4 oColor;
void main() {
    vec3 n = normalize(vNormal);
    float shade = max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0);
    oColor = vec4(uBaseColor.rgb * shade, uBaseColor.a);
}
