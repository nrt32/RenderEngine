#version 450 core
struct CsgNode {
    uint colorU32;
    float depth;
    int facing;
    uint matId;
};
layout(std430, binding = 0) coherent buffer NodeBuffer { CsgNode nodes[]; };
layout(std430, binding = 6) coherent buffer HeadCounts { uint headCounts[]; };
layout(std430, binding = 1) coherent buffer CounterBuffer { uint counter; };
layout(std430, binding = 2) coherent buffer HeadBuffer { uint heads[]; };
uniform vec2 uResolution;
uniform int uColorU32;
uniform int uMatId;
uniform int uCapacity;
uniform int uMaxFpp;
layout(location = 0) out vec4 oColor;
void main() {
    ivec2 pix = ivec2(gl_FragCoord.xy);
    ivec2 res = ivec2(uResolution);
    if (pix.x < 0 || pix.y < 0 || pix.x >= res.x || pix.y >= res.y) {
        return;
    }
    int pixIdx = pix.y * res.x + pix.x;
    uint local = atomicAdd(headCounts[pixIdx], 1u);
    if (local >= uint(uMaxFpp)) {
        return;
    }
    uint idx = uint(pixIdx) * uint(uMaxFpp) + local;
    atomicAdd(counter, 1u);
    if (idx >= uint(uCapacity)) {
        return;
    }
    atomicExchange(heads[pixIdx], idx);
    nodes[idx].colorU32 = uint(uColorU32);
    nodes[idx].depth = gl_FragCoord.z;
    nodes[idx].facing = gl_FrontFacing ? 1 : -1;
    nodes[idx].matId = uint(uMatId);
    float r = float(uint(uColorU32) & 0xFFu) / 255.0;
    float g = float((uint(uColorU32) >> 8) & 0xFFu) / 255.0;
    float b = float((uint(uColorU32) >> 16) & 0xFFu) / 255.0;
    float a = float((uint(uColorU32) >> 24) & 0xFFu) / 255.0;
    oColor = vec4(r, g, b, a);
}
