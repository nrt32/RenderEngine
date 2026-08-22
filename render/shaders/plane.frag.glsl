#version 450 core
in vec2 vUV;
uniform sampler2D uTex;
layout(location = 0) out vec4 oColor;
void main() {
    oColor = texture(uTex, vUV);
}
