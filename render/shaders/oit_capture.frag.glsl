#version 450 core
struct OITNode {
    vec4 color;
    float depth;
    uint next;
};
layout(std430, binding = 0) coherent buffer NodeBuffer { OITNode nodes[]; };
layout(std430, binding = 1) coherent buffer CounterBuffer { uint counter; };
layout(r32ui, binding = 2) uniform coherent uimage2D uHead;
uniform vec4 uBaseColor;
uniform int uCapacity;
void main() {
    uint idx = atomicAdd(counter, 1u);
    if (idx >= uint(uCapacity)) {
        return;
    }
    uint prev = imageAtomicExchange(uHead, ivec2(gl_FragCoord.xy), idx);
    vec4 c = vec4(uBaseColor.rgb * uBaseColor.a, uBaseColor.a);
    nodes[idx].color = c;
    nodes[idx].depth = gl_FragCoord.z;
    nodes[idx].next = prev;
}
