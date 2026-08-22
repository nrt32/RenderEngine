#version 450 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 6) out;
in vec3 vWorldPos[];
uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uPlaneNormal;
uniform vec3 uPlanePoint;
flat out vec3 fNormal;
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
        bool keepI = d[i] >= 0.0;
        bool keepJ = d[j] >= 0.0;
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
