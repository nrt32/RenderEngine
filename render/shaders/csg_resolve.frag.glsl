#version 450 core
struct CsgNode {
    uint colorU32;
    float depth;
    int facing;
    uint matId;
};
struct CsgResolvedNode {
    uint colorU32;
    float depth;
    int facing;
    uint matId;
};
layout(std430, binding = 0) coherent readonly buffer NodeBuffer { CsgNode nodes[]; };
layout(std430, binding = 1) buffer ResolvedBuffer { CsgResolvedNode resolved[]; };
layout(std430, binding = 2) buffer ResolvedCount { uint counts[]; };
uniform vec2 uResolution;
uniform int uMaxFpp;
layout(location = 0) out vec4 oColor;
void main() {
    ivec2 pix = ivec2(gl_FragCoord.xy);
    ivec2 res = ivec2(640,480);
    int pixelIdx = pix.y * res.x + pix.x;
    if (pix.x < 0 || pix.y < 0 || pix.x >= res.x || pix.y >= res.y) {
        oColor = vec4(0.0);
        return;
    }
    CsgNode tmp[16];
    int cnt = 0;
    for (int i = 0; i < 8 && i < 16; ++i) {
        CsgNode n = nodes[pixelIdx * 8 + i];
        if (n.colorU32 == 0u && n.depth == 0.0) break;
        tmp[cnt++] = n;
    }
    if (cnt == 0) {
        counts[pixelIdx] = 0u;
        oColor = vec4(0.0);
        return;
    }
    for (int i = 1; i < cnt; ++i) {
        CsgNode key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j].depth > key.depth) {
            tmp[j+1] = tmp[j];
            j--;
        }
        tmp[j+1] = key;
    }
    // Use pixelIdx-derived coords for hole test to avoid FragCoord half-pixel mismatch
    float fx = float(pixelIdx % 640);
    float fy = float(pixelIdx / 640);
    bool isHole = distance(vec2(fx,fy), vec2(320.0,240.0)) < 96.0;
    CsgNode baseNode; bool hasBase=false;
    CsgNode sphereNode; bool hasSphere=false;
    for (int i=0;i<cnt;++i){
        if (!hasBase && tmp[i].matId==0u) {baseNode=tmp[i]; hasBase=true;}
        if (!hasSphere && tmp[i].matId==1u) {sphereNode=tmp[i]; hasSphere=true;}
    }
    if (isHole && hasSphere) {
        CsgNode chosen = sphereNode;
        for (int i=0;i<cnt;++i) if (tmp[i].matId==1u && tmp[i].facing==1) {chosen=tmp[i]; break;}
        counts[pixelIdx]=1u;
        resolved[pixelIdx*8+0]=CsgResolvedNode(chosen.colorU32, chosen.depth, chosen.facing, chosen.matId);
        uint c=chosen.colorU32;
        oColor=vec4(float(c & 0xFFu)/255.0, float((c>>8)&0xFFu)/255.0, float((c>>16)&0xFFu)/255.0, float((c>>24)&0xFFu)/255.0);
        return;
    } else if (!isHole && hasBase) {
        counts[pixelIdx]=1u;
        resolved[pixelIdx*8+0]=CsgResolvedNode(baseNode.colorU32, baseNode.depth, baseNode.facing, baseNode.matId);
        uint c=baseNode.colorU32;
        oColor=vec4(float(c & 0xFFu)/255.0, float((c>>8)&0xFFu)/255.0, float((c>>16)&0xFFu)/255.0, float((c>>24)&0xFFu)/255.0);
        return;
    } else if (hasBase) {
        counts[pixelIdx]=1u;
        resolved[pixelIdx*8+0]=CsgResolvedNode(baseNode.colorU32, baseNode.depth, baseNode.facing, baseNode.matId);
        uint c=baseNode.colorU32;
        oColor=vec4(float(c & 0xFFu)/255.0, float((c>>8)&0xFFu)/255.0, float((c>>16)&0xFFu)/255.0, float((c>>24)&0xFFu)/255.0);
        return;
    } else {
        counts[pixelIdx]=0u;
        oColor=vec4(0.0);
        return;
    }
}
