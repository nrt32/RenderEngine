#version 450 core
flat in vec3 fNormal;
uniform vec4 uBaseColor;
layout(location = 0) out vec4 oColor;
void main() {
    vec3 n = normalize(fNormal);
    float shade = max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0);
    oColor = vec4(uBaseColor.rgb * shade, uBaseColor.a);
}
