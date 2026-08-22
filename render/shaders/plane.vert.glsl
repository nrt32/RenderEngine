#version 450 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec2 vUV;
out vec3 vNormal;
void main() {
    vUV = aUV;
    vNormal = mat3(uModel) * aNormal;
    vec4 worldPos = uModel * vec4(aPos, 1.0);
    gl_Position = uProj * uView * worldPos;
}
