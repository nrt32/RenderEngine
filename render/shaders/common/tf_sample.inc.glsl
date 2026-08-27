// tf_sample.inc.glsl — shared transfer-function sampler for volume shaders
// This file provides the single definition of vec4 tfSample(float) used by
// both volume_raycast.frag.glsl and volume_slice.frag.glsl. The uniforms
// uTfValues[8], uTfColors[8] and uTfCount are declared by the host shader;
// this include only defines the sampling logic so the C++ limit
// render::kMaxTfPoints stays consistent with the GLSL array size.
vec4 tfSample(float value) {
    if (value <= uTfValues[0]) {
        return uTfColors[0];
    }
    if (value >= uTfValues[uTfCount - 1]) {
        return uTfColors[uTfCount - 1];
    }
    int lo = 0;
    int hi = uTfCount - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (uTfValues[mid] <= value) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    float t = (value - uTfValues[lo]) / (uTfValues[hi] - uTfValues[lo]);
    return mix(uTfColors[lo], uTfColors[hi], t);
}
