// render/slice_renderer.cpp — SliceRenderer implementation (SPEC §3,
// FR-render.4).

#include "render/slice_renderer.hpp"

#include <glad/gl.h>

#include <cstddef>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/draw.hpp"

namespace re::render {

namespace {

// ---------------------------------------------------------------------------
// Shaders, GLSL 450 (SPEC §8: gate/test shaders compile on llvmpipe which caps
// at 4.50).
//
// All positions are transformed to WORLD space in the vertex shader. The plane
// is defined in world space by a unit normal + point; the kept side is
// `dot(normal, p - point) >= 0`.
// ---------------------------------------------------------------------------

// Vertex shader shared by the clip (render) and capture programs: passes the
// world-space position + world-space normal through to the geometry stage.
constexpr char kSliceVertexShader[] =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "uniform mat4 uModel;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "void main() {\n"
    "    vec4 world = uModel * vec4(aPos, 1.0);\n"
    "    vWorldPos = world.xyz;\n"
    "    vNormal = mat3(uModel) * aNormal;\n"
    "}\n";

// Clip (render) geometry shader: Sutherland-Hodgman clip of each triangle
// against the plane, keeping the half-space `d >= 0`, and emit the resulting
// 3- or 4-vertex polygon as a triangle fan. The fragment shader shades with
// the triangle's geometric (face) normal (computed from the world-space
// winding, so it is exact for a flat face regardless of smooth per-vertex
// normals) under the deterministic v1 flat lighting (docs/render.md). A kept
// surface whose geometric normal is +Z therefore renders at exactly the
// material's base color.
constexpr char kClipGeometryShader[] =
    "#version 450 core\n"
    "layout(triangles) in;\n"
    "layout(triangle_strip, max_vertices = 6) out;\n"
    "in vec3 vWorldPos[];\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "uniform vec3 uPlaneNormal;\n"
    "uniform vec3 uPlanePoint;\n"
    "flat out vec3 fNormal;\n"
    "void emitVertex(vec3 pos) {\n"
    "    gl_Position = uProj * uView * vec4(pos, 1.0);\n"
    "    EmitVertex();\n"
    "}\n"
    "void main() {\n"
    "    vec3 P[3] = vec3[](vWorldPos[0], vWorldPos[1], vWorldPos[2]);\n"
    "    fNormal = normalize(cross(P[1] - P[0], P[2] - P[0]));\n"
    "    float d[3];\n"
    "    d[0] = dot(uPlaneNormal, P[0] - uPlanePoint);\n"
    "    d[1] = dot(uPlaneNormal, P[1] - uPlanePoint);\n"
    "    d[2] = dot(uPlaneNormal, P[2] - uPlanePoint);\n"
    "    vec3 CP[4];\n"
    "    int k = 0;\n"
    "    for (int i = 0; i < 3; ++i) {\n"
    "        int j = (i + 1) % 3;\n"
    "        bool keepI = d[i] >= 0.0;\n"
    "        bool keepJ = d[j] >= 0.0;\n"
    "        if (keepI) {\n"
    "            CP[k++] = P[i];\n"
    "        }\n"
    "        if (keepI != keepJ) {\n"
    "            float t = d[i] / (d[i] - d[j]);\n"
    "            CP[k++] = P[i] + t * (P[j] - P[i]);\n"
    "        }\n"
    "    }\n"
    "    if (k >= 3) {\n"
    "        for (int i = 1; i + 1 < k; ++i) {\n"
    "            emitVertex(CP[0]);\n"
    "            emitVertex(CP[i]);\n"
    "            emitVertex(CP[i + 1]);\n"
    "        }\n"
    "        EndPrimitive();\n"
    "    }\n"
    "}\n";

// Fragment shader of the clip (render) program: deterministic v1 flat lighting
// (identical to the MeshRenderer opaque pass, docs/render.md): a fixed head-on
// light from +Z with ambient=0, diffuse=1, specular=0, so a surface whose
// normal is aligned with +Z renders at exactly the material's base color.
constexpr char kClipFragmentShader[] =
    "#version 450 core\n"
    "flat in vec3 fNormal;\n"
    "uniform vec4 uBaseColor;\n"
    "layout(location = 0) out vec4 oColor;\n"
    "void main() {\n"
    "    vec3 n = normalize(fNormal);\n"
    "    float shade = max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0);\n"
    "    oColor = vec4(uBaseColor.rgb * shade, uBaseColor.a);\n"
    "}\n";

// Cross-section capture geometry shader: emits ONLY the on-plane cross-section
// polygon of each triangle (all emitted vertices lie exactly on the plane,
// FR-render.4). For a triangle whose plane intersection is a strict segment the
// polygon is degenerate (emitted as a zero-area triangle) so the emitted vertex
// count is deterministic. The world position is written to `gWorldPos`, which
// the transform-feedback capture reads back (test-consumed).
constexpr char kCaptureGeometryShader[] =
    "#version 450 core\n"
    "layout(triangles) in;\n"
    "layout(triangle_strip, max_vertices = 6) out;\n"
    "in vec3 vWorldPos[];\n"
    "uniform vec3 uPlaneNormal;\n"
    "uniform vec3 uPlanePoint;\n"
    "flat out vec3 gWorldPos;\n"
    "const float EPS = 1e-5;\n"
    "void emitVertex(vec3 p) {\n"
    "    gWorldPos = p;\n"
    "    gl_Position = vec4(p, 1.0);\n"
    "    EmitVertex();\n"
    "}\n"
    "void main() {\n"
    "    vec3 P[3] = vec3[](vWorldPos[0], vWorldPos[1], vWorldPos[2]);\n"
    "    float d[3];\n"
    "    d[0] = dot(uPlaneNormal, P[0] - uPlanePoint);\n"
    "    d[1] = dot(uPlaneNormal, P[1] - uPlanePoint);\n"
    "    d[2] = dot(uPlaneNormal, P[2] - uPlanePoint);\n"
    "    bool allPos = d[0] >= -EPS && d[1] >= -EPS && d[2] >= -EPS;\n"
    "    bool allNeg = d[0] <= EPS && d[1] <= EPS && d[2] <= EPS;\n"
    "    if (allPos || allNeg) {\n"
    "        if (abs(d[0]) <= EPS && abs(d[1]) <= EPS && abs(d[2]) <= EPS) {\n"
    "            emitVertex(P[0]); emitVertex(P[1]); emitVertex(P[2]);\n"
    "        }\n"
    "        return;\n"
    "    }\n"
    "    vec3 C[4];\n"
    "    int k = 0;\n"
    "    if (abs(d[0]) <= EPS) { C[k++] = P[0]; }\n"
    "    if (abs(d[1]) <= EPS) { C[k++] = P[1]; }\n"
    "    if (abs(d[2]) <= EPS) { C[k++] = P[2]; }\n"
    "    if (d[0] > EPS && d[1] < -EPS || d[0] < -EPS && d[1] > EPS) {\n"
    "        float t = d[0] / (d[0] - d[1]);\n"
    "        C[k++] = P[0] + t * (P[1] - P[0]);\n"
    "    }\n"
    "    if (d[1] > EPS && d[2] < -EPS || d[1] < -EPS && d[2] > EPS) {\n"
    "        float t = d[1] / (d[1] - d[2]);\n"
    "        C[k++] = P[1] + t * (P[2] - P[1]);\n"
    "    }\n"
    "    if (d[2] > EPS && d[0] < -EPS || d[2] < -EPS && d[0] > EPS) {\n"
    "        float t = d[2] / (d[2] - d[0]);\n"
    "        C[k++] = P[2] + t * (P[0] - P[2]);\n"
    "    }\n"
    "    if (k == 2) {\n"
    "        emitVertex(C[0]); emitVertex(C[1]); emitVertex(C[1]);\n"
    "    } else if (k == 3) {\n"
    "        emitVertex(C[0]); emitVertex(C[1]); emitVertex(C[2]);\n"
    "    } else if (k >= 4) {\n"
    "        emitVertex(C[0]); emitVertex(C[1]); emitVertex(C[2]);\n"
    "        emitVertex(C[0]); emitVertex(C[2]); emitVertex(C[3]);\n"
    "    }\n"
    "    EndPrimitive();\n"
    "}\n";

// Fragment shader of the capture program: discards (the capture pass must not
// write to the framebuffer; only the transform-feedback buffer is meaningful).
constexpr char kCaptureFragmentShader[] =
    "#version 450 core\n"
    "void main() { discard; }\n";

// A sentinel coordinate written into the capture buffer before capture; after
// capture, entries still equal to this sentinel were never written (the
// transform-feedback buffer was larger than the emitted vertex count). Real
// mesh coordinates never reach this magnitude, so counting non-sentinel
// triples yields the exact emitted vertex count.
constexpr float kCaptureSentinel = 1.0e30f;

// Maximum cross-section vertices a single triangle can emit (the capture
// geometry shader declares max_vertices = 6).
constexpr std::size_t kMaxVerticesPerTriangle = 6u;

} // namespace

data::Result<core::ShaderProgram*> SliceRenderer::clipProgram() {
    if (clipProgram_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*clipProgram_);
    }
    auto program = core::ShaderProgram::createWithGeometry(
        kSliceVertexShader, kClipGeometryShader, kClipFragmentShader);
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    clipProgram_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*clipProgram_);
}

data::Result<core::ShaderProgram*> SliceRenderer::captureProgram() {
    if (captureProgram_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*captureProgram_);
    }
    // Capture the single `gWorldPos` varying (world-space cross-section
    // vertex). Capture is begun with GL_TRIANGLES (the geometry stage outputs
    // triangle_strip) via core::TransformFeedback::begin at draw time.
    const std::vector<std::string> varyings = {"gWorldPos"};
    auto program = core::ShaderProgram::createWithTransformFeedback(
        kSliceVertexShader, kCaptureGeometryShader, kCaptureFragmentShader,
        varyings);
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    captureProgram_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*captureProgram_);
}

data::Result<core::TransformFeedback*> SliceRenderer::captureFeedback() {
    if (captureFeedback_.has_value()) {
        return data::makeValue<core::TransformFeedback*>(&*captureFeedback_);
    }
    auto feedback = core::TransformFeedback::create();
    if (feedback.failed()) {
        return data::makeError<core::TransformFeedback*>(
            feedback.error().code, feedback.error().message);
    }
    captureFeedback_ = std::move(*feedback);
    return data::makeValue<core::TransformFeedback*>(&*captureFeedback_);
}

data::Result<MeshGeometry*> SliceRenderer::geometryFor(const data::Mesh& mesh) {
    const auto it = geometries_.find(&mesh);
    if (it != geometries_.end()) {
        return data::makeValue<MeshGeometry*>(&it->second);
    }
    auto geometry = MeshGeometry::create(mesh);
    if (geometry.failed()) {
        return data::makeError<MeshGeometry*>(geometry.error().code,
                                              geometry.error().message);
    }
    auto inserted = geometries_.emplace(&mesh, std::move(*geometry));
    return data::makeValue<MeshGeometry*>(&inserted.first->second);
}

data::Result<void> SliceRenderer::render(const SliceScene& scene,
                                         const Camera& camera,
                                         const ClipPlane& plane,
                                         const RenderTarget& target) {
    if (target.framebuffer == nullptr) {
        return data::makeError<void>(1,
                                     "SliceRenderer: null target framebuffer");
    }

    auto programResult = clipProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;

    // Bind the target and prepare draw state. v1 FBOs are color-only (no depth
    // attachment, SPEC §6 / docs/core.md), so the depth test is left off.
    target.framebuffer->bind();
    core::setViewport(0, 0, static_cast<int>(target.width),
                      static_cast<int>(target.height));
    core::setClearColor(target.clearColor.r, target.clearColor.g,
                        target.clearColor.b, target.clearColor.a);
    core::clearColor();
    core::disableDepthTest();
    core::disableBlend();

    program->use();
    program->setUniformMat4("uView", camera.view);
    program->setUniformMat4("uProj", camera.proj);
    program->setUniformVec3("uPlaneNormal", plane.normal);
    program->setUniformVec3("uPlanePoint", plane.point);

    for (const MeshInstance& instance : scene.meshes) {
        if (instance.material == nullptr || instance.mesh == nullptr) {
            continue;
        }
        // Slicing does not use OIT in v1 (SPEC §3): every instance is clipped
        // and drawn through the clip pass regardless of material transparency.
        auto geometry = geometryFor(*instance.mesh);
        if (geometry.failed()) {
            return data::makeError<void>(geometry.error().code,
                                         geometry.error().message);
        }
        MeshGeometry* geometryPtr = *geometry;

        program->setUniformMat4("uModel", instance.model);
        program->setUniformVec4("uBaseColor", instance.material->baseColor());

        auto draw = geometryPtr->draw();
        if (draw.failed()) {
            return draw;
        }
    }
    return data::Result<void>(data::value);
}

data::Result<void> SliceRenderer::captureCrossSection(
    const SliceScene& scene, const ClipPlane& plane,
    std::vector<glm::vec3>& out) {
    out.clear();

    auto programResult = captureProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;

    auto feedbackResult = captureFeedback();
    if (feedbackResult.failed()) {
        return data::makeError<void>(feedbackResult.error().code,
                                     feedbackResult.error().message);
    }
    core::TransformFeedback* feedback = *feedbackResult;

    // Total capture capacity: every triangle can emit up to
    // kMaxVerticesPerTriangle vertices (each 3 floats).
    std::size_t totalCapacityVertices = 0u;
    for (const MeshInstance& instance : scene.meshes) {
        if (instance.mesh == nullptr) {
            continue;
        }
        totalCapacityVertices +=
            instance.mesh->triangleCount() * kMaxVerticesPerTriangle;
    }

    std::vector<float> sentinel(totalCapacityVertices * 3u, kCaptureSentinel);

    // (Re)allocate the capture buffer if its size changed or it does not exist.
    const std::size_t capacityBytes = sentinel.size() * sizeof(float);
    if (!captureBuffer_.has_value()) {
        auto buffer = core::VertexBuffer::create();
        if (buffer.failed()) {
            return data::makeError<void>(buffer.error().code,
                                         buffer.error().message);
        }
        captureBuffer_ = std::move(*buffer);
    }
    core::VertexBuffer& buffer = *captureBuffer_;
    buffer.bind();
    buffer.upload(sentinel.data(), capacityBytes,
                  core::BufferUsage::StreamDraw);
    buffer.unbind();

    feedback->bind();
    feedback->bindBufferBase(0u, buffer);

    program->use();
    program->setUniformVec3("uPlaneNormal", plane.normal);
    program->setUniformVec3("uPlanePoint", plane.point);

    for (const MeshInstance& instance : scene.meshes) {
        if (instance.mesh == nullptr) {
            continue;
        }
        auto geometry = geometryFor(*instance.mesh);
        if (geometry.failed()) {
            feedback->unbind();
            return data::makeError<void>(geometry.error().code,
                                         geometry.error().message);
        }
        MeshGeometry* geometryPtr = *geometry;

        program->setUniformMat4("uModel", instance.model);
        feedback->begin(GL_TRIANGLES);
        auto draw = geometryPtr->draw();
        feedback->end();
        if (draw.failed()) {
            feedback->unbind();
            return draw;
        }
    }

    feedback->unbind();

    // Read back the whole capture buffer and keep the non-sentinel vertices.
    std::vector<float> captured(sentinel.size(), 0.0f);
    auto read =
        feedback->readFloats(buffer, 0u, captured.size(), captured.data());
    if (read.failed()) {
        return data::makeError<void>(read.error().code, read.error().message);
    }
    out.reserve(captured.size() / 3u);
    for (std::size_t i = 0u; i + 2u < captured.size(); i += 3u) {
        const glm::vec3 v(captured[i], captured[i + 1u], captured[i + 2u]);
        if (v.x != kCaptureSentinel || v.y != kCaptureSentinel ||
            v.z != kCaptureSentinel) {
            out.push_back(v);
        }
    }
    return data::Result<void>(data::value);
}

} // namespace re::render
