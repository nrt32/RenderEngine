#version 450 core
// GPU volume-plane extraction (one slice of the cached 3D texture at an
// arbitrary world-space plane). For every pixel the full-screen quad covers,
// this shader reconstructs the pixel's camera ray, intersects it with the
// extraction plane, converts the hit point into the dataset's model space
// (the unit cube [0,1]^3), samples the R32F density texture with hardware
// trilinear filtering, and writes the transfer-function color as STRAIGHT
// RGBA. Hits off the volume slab write transparent black, so the extracted
// slice appears exactly where the plane crosses the dataset. The texel
// mapping (idx + 0.5) / dim is the same one the volume ray-cast shader uses,
// which is what makes linear filtering reproduce the CPU trilinear interpolant
// (data::VolumeDataset::sampleTrilinear) and keeps the analytic gates within
// 1/255.
in vec2 vNdc;
uniform mat4 uInvViewProj;
uniform mat4 uInvModel;
uniform vec3 uSize;
uniform vec3 uPlaneNormal;
uniform vec3 uPlanePoint;
uniform int uTfCount;
uniform float uTfValues[8];
uniform vec4 uTfColors[8];
uniform sampler3D uVolume;
layout(location = 0) out vec4 oColor;

#include "common/tf_sample.inc.glsl"

void main() {
    // Unproject the pixel's NDC near/far points to world space (identical to
    // the ray-cast entry of this shader family; works for ortho and
    // perspective cameras alike). The direction needs no normalization: only
    // the ray-plane parameter t matters here, not its world-space length.
    vec4 nearNdc = vec4(vNdc, -1.0, 1.0);
    vec4 farNdc = vec4(vNdc, 1.0, 1.0);
    vec4 worldNear = uInvViewProj * nearNdc;
    worldNear /= worldNear.w;
    vec4 worldFar = uInvViewProj * farNdc;
    worldFar /= worldFar.w;
    vec3 ro = worldNear.xyz;
    vec3 rd = worldFar.xyz - worldNear.xyz;

    // Ray-plane intersection dot(n, ro + t*rd - p0) = 0. Rays parallel to the
    // plane (or hitting it behind the eye) leave the pixel untouched.
    float denom = dot(uPlaneNormal, rd);
    if (abs(denom) < 1e-12) {
        oColor = vec4(0.0);
        return;
    }
    float t = dot(uPlaneNormal, uPlanePoint - ro) / denom;
    if (t < 0.0) {
        oColor = vec4(0.0);
        return;
    }
    vec3 worldPos = ro + rd * t;

    // Reject hits outside the dataset's model-space unit cube. The slab is
    // widened by 1e-4 (the project's plane-geometry tolerance in normalized
    // units) purely to absorb float rounding on the outermost voxel centers:
    // without it, a boundary hit intended for model coordinate exactly 1.0
    // can land at 1.0000001 after the unproject/invert round trip and punch a
    // transparent hole into the last voxel ring. clamp-to-edge sampling turns
    // anything inside the widened band into the boundary voxel's value, so
    // the widening is invisible in the output bytes.
    vec3 modelPos = (uInvModel * vec4(worldPos, 1.0)).xyz;
    const float kSlabEps = 1e-4;
    if (any(lessThan(modelPos, vec3(-kSlabEps))) ||
        any(greaterThan(modelPos, vec3(1.0 + kSlabEps)))) {
        oColor = vec4(0.0);
        return;
    }

    // Model space [0,1]^3 -> continuous index space idx = modelPos*(dim-1)
    // -> texel-center space (idx + 0.5)/dim. At integer indices the hardware
    // trilinear filter reproduces the exact voxel value, matching the CPU
    // sampler voxel-for-voxel.
    vec3 texCoord = (modelPos * (uSize - vec3(1.0)) + vec3(0.5)) / uSize;
    float density = texture(uVolume, texCoord).r;
    oColor = tfSample(density);
}
