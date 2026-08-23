#version 450 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vWorldPos;
void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vWorldPos = world.xyz;
    // The geometry shader recomputes clip positions from vWorldPos (the
    // crossing points need per-segment projection); this write keeps the
    // vertex stage well-formed.
    gl_Position = uProj * uView * world;
}
