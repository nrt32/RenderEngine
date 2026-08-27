#version 450 core
in vec2 vNdc;
uniform mat4 uInvViewProj;
uniform vec3 uBoxMin;
uniform vec3 uBoxMax;
uniform mat4 uInvModel;
uniform vec3 uSize;
uniform float uStepLength;
uniform int uTfCount;
uniform float uTfValues[8];
uniform vec4 uTfColors[8];
uniform sampler3D uVolume;
layout(location = 0) out vec4 oColor;

bool intersectRayAabb(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax,
                      out float tEntry, out float tExit) {
    float tNear = -1e30;
    float tFar = 1e30;
    for (int axis = 0; axis < 3; ++axis) {
        float dir = rd[axis];
        float orig = ro[axis];
        if (abs(dir) < 1e-7) {
            if (orig < bmin[axis] || orig > bmax[axis]) {
                return false;
            }
            continue;
        }
        float t1 = (bmin[axis] - orig) / dir;
        float t2 = (bmax[axis] - orig) / dir;
        if (t1 > t2) {
            float tmp = t1; t1 = t2; t2 = tmp;
        }
        tNear = max(tNear, t1);
        tFar = min(tFar, t2);
        if (tNear > tFar) {
            return false;
        }
    }
    if (tFar < 0.0) {
        return false;
    }
    tEntry = max(tNear, 0.0);
    tExit = tFar;
    return true;
}

#include "common/tf_sample.inc.glsl"

void main() {
    vec4 nearNdc = vec4(vNdc, -1.0, 1.0);
    vec4 farNdc = vec4(vNdc, 1.0, 1.0);
    vec4 worldNear = uInvViewProj * nearNdc;
    worldNear /= worldNear.w;
    vec4 worldFar = uInvViewProj * farNdc;
    worldFar /= worldFar.w;
    vec3 ro = worldNear.xyz;
    vec3 rd = normalize(worldFar.xyz - worldNear.xyz);

    float tEntry = 0.0;
    float tExit = 0.0;
    if (!intersectRayAabb(ro, rd, uBoxMin, uBoxMax, tEntry, tExit)) {
        oColor = vec4(0.0);
        return;
    }
    float span = tExit - tEntry;
    int count = int(floor(span / uStepLength));
    if (count < 1) {
        oColor = vec4(0.0);
        return;
    }

    vec3 rgb = vec3(0.0);
    float alpha = 0.0;
    for (int k = 0; k < count; ++k) {
        float t = tEntry + (float(k) + 0.5) * uStepLength;
        vec3 worldPos = ro + rd * t;
        vec3 modelPos = (uInvModel * vec4(worldPos, 1.0)).xyz;
        vec3 texCoord = (modelPos * (uSize - vec3(1.0)) + vec3(0.5)) / uSize;
        float density = texture(uVolume, texCoord).r;
        vec4 tf = tfSample(density);
        float w = (1.0 - alpha) * tf.a;
        rgb += w * tf.rgb;
        alpha += w;
    }
    oColor = vec4(rgb, alpha);
}
