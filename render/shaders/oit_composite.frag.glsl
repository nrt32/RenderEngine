#version 450 core
struct OITNode {
    vec4 color;
    float depth;
    uint next;
};
layout(std430, binding = 0) coherent buffer NodeBuffer { OITNode nodes[]; };
layout(r32ui, binding = 2) uniform coherent uimage2D uHead;
uniform int uMaxNodes;
layout(location = 0) out vec4 oColor;
void main() {
    OITNode list[16];
    int count = 0;
    uint cur = imageLoad(uHead, ivec2(gl_FragCoord.xy)).r;
    while (cur != 0xFFFFFFFFu && count < uMaxNodes) {
        list[count] = nodes[cur];
        count++;
        cur = nodes[cur].next;
    }
    for (int i = 1; i < count; ++i) {
        OITNode key = list[i];
        int j = i - 1;
        while (j >= 0 && list[j].depth > key.depth) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = key;
    }
    vec4 acc = vec4(0.0);
    for (int i = count - 1; i >= 0; --i) {
        vec4 s = list[i].color;
        acc.rgb = s.rgb + (1.0 - s.a) * acc.rgb;
        acc.a = s.a + (1.0 - s.a) * acc.a;
    }
    oColor = acc;
}
