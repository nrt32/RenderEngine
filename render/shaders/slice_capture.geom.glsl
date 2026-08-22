#version 450 core
layout(triangles) in;
layout(triangle_strip, max_vertices = 6) out;
in vec3 vWorldPos[];
uniform vec3 uPlaneNormal;
uniform vec3 uPlanePoint;
flat out vec3 gWorldPos;
const float EPS = 1e-5;
void emitVertex(vec3 p) {
    gWorldPos = p;
    gl_Position = vec4(p, 1.0);
    EmitVertex();
}
void main() {
    vec3 P[3] = vec3[](vWorldPos[0], vWorldPos[1], vWorldPos[2]);
    float d[3];
    d[0] = dot(uPlaneNormal, P[0] - uPlanePoint);
    d[1] = dot(uPlaneNormal, P[1] - uPlanePoint);
    d[2] = dot(uPlaneNormal, P[2] - uPlanePoint);
    bool allPos = d[0] >= -EPS && d[1] >= -EPS && d[2] >= -EPS;
    bool allNeg = d[0] <= EPS && d[1] <= EPS && d[2] <= EPS;
    if (allPos || allNeg) {
        if (abs(d[0]) <= EPS && abs(d[1]) <= EPS && abs(d[2]) <= EPS) {
            emitVertex(P[0]); emitVertex(P[1]); emitVertex(P[2]);
        }
        return;
    }
    vec3 C[4];
    int k = 0;
    if (abs(d[0]) <= EPS) { C[k++] = P[0]; }
    if (abs(d[1]) <= EPS) { C[k++] = P[1]; }
    if (abs(d[2]) <= EPS) { C[k++] = P[2]; }
    if (d[0] > EPS && d[1] < -EPS || d[0] < -EPS && d[1] > EPS) {
        float t = d[0] / (d[0] - d[1]);
        C[k++] = P[0] + t * (P[1] - P[0]);
    }
    if (d[1] > EPS && d[2] < -EPS || d[1] < -EPS && d[2] > EPS) {
        float t = d[1] / (d[1] - d[2]);
        C[k++] = P[1] + t * (P[2] - P[1]);
    }
    if (d[2] > EPS && d[0] < -EPS || d[2] < -EPS && d[0] > EPS) {
        float t = d[2] / (d[2] - d[0]);
        C[k++] = P[2] + t * (P[0] - P[2]);
    }
    if (k == 2) {
        emitVertex(C[0]); emitVertex(C[1]); emitVertex(C[1]);
    } else if (k == 3) {
        emitVertex(C[0]); emitVertex(C[1]); emitVertex(C[2]);
    } else if (k >= 4) {
        emitVertex(C[0]); emitVertex(C[1]); emitVertex(C[2]);
        emitVertex(C[0]); emitVertex(C[2]); emitVertex(C[3]);
    }
    EndPrimitive();
}
