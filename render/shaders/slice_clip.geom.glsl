#version 450 core
// Clip classifier epsilon explanation: the plane distance threshold kClipEps
// equal to 1e-5 is shared across the three geometry shaders that classify
// triangles against a world-space clip plane. The value is small enough to
// preserve the analytic clip result for the golden cube gates (where vertex
// distances are at least 1.0 from the plane) while absorbing typical float
// rounding on near-coplanar edges. Contour keeps an additional dedup epsilon
// 1e-6 for merging coincident intersection points at vertices, which is a
// separate concern from the half-space classification and therefore documented
// as an intentional divergence.
layout(triangles) in;
layout(triangle_strip, max_vertices = 6) out;
in vec3 vWorldPos[];
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uPlaneNormal;
uniform vec3 uPlanePoint;
flat out vec3 fNormal;
const float kClipEps = 1e-5;
void emitVertex(vec3 pos) {
    gl_Position = uProj * uView * vec4(pos, 1.0);
    EmitVertex();
}
void main() {
    vec3 P[3] = vec3[](vWorldPos[0], vWorldPos[1], vWorldPos[2]);
    fNormal = normalize(cross(P[1] - P[0], P[2] - P[0]));
    float d[3];
    d[0] = dot(uPlaneNormal, P[0] - uPlanePoint);
    d[1] = dot(uPlaneNormal, P[1] - uPlanePoint);
    d[2] = dot(uPlaneNormal, P[2] - uPlanePoint);
    vec3 CP[4];
    int k = 0;
    for (int i = 0; i < 3; ++i) {
        int j = (i + 1) % 3;
        bool keepI = d[i] >= -kClipEps;
        bool keepJ = d[j] >= -kClipEps;
        if (keepI) {
            CP[k++] = P[i];
        }
        if (keepI != keepJ) {
            float t = d[i] / (d[i] - d[j]);
            CP[k++] = P[i] + t * (P[j] - P[i]);
        }
    }
    if (k >= 3) {
        for (int i = 1; i + 1 < k; ++i) {
            emitVertex(CP[0]);
            emitVertex(CP[i]);
            emitVertex(CP[i + 1]);
        }
        EndPrimitive();
    }
}
