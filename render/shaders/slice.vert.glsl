#version 450 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
out vec3 vWorldPos;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
}
