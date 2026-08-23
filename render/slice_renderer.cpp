// render/slice_renderer.cpp — SliceRenderer implementation (SPEC §3,
// FR-render.4).

#include "render/slice_renderer.hpp"

#include <glad/gl.h>

#include <cstddef>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/draw.hpp"
#include "core/shader_program.hpp"

namespace re::render {

// Shaders live as .glsl files under render/shaders/ (SPEC §9 V2.6) and are
// loaded via core::ShaderProgram's file helpers. The plane is defined in
// world space by a unit normal + point; kept side is dot(normal, p-point)>=0.

// A sentinel coordinate written into the capture buffer before capture; after
// capture, entries still equal to this sentinel were never written (the
// transform-feedback buffer was larger than the emitted vertex count). Real
// mesh coordinates never reach this magnitude, so counting non-sentinel
// triples yields the exact emitted vertex count.
constexpr float kCaptureSentinel = 1.0e30f;

// Maximum cross-section vertices a single triangle can emit (the capture
// geometry shader declares max_vertices = 6).
constexpr std::size_t kMaxVerticesPerTriangle = 6u;

SliceRenderer::SliceRenderer(std::shared_ptr<AssetRegistry> registry)
    : registry_(std::move(registry)) {}

data::Result<core::ShaderProgram*> SliceRenderer::clipProgram() {
    if (clipProgram_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*clipProgram_);
    }
    const std::filesystem::path dir = RE_SHADER_DIR;
    auto program = core::ShaderProgram::createWithGeometryFromFiles(
        dir / "slice.vert.glsl", dir / "slice_clip.geom.glsl",
        dir / "slice_clip.frag.glsl");
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
    const std::filesystem::path dir = RE_SHADER_DIR;
    auto program = core::ShaderProgram::createWithTransformFeedbackFromFiles(
        dir / "slice.vert.glsl", dir / "slice_capture.geom.glsl",
        dir / "slice_capture.frag.glsl", varyings);
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

data::Result<MeshGeometry*> SliceRenderer::geometryFor(
    const AssetHandle& handle) {
    if (!registry_) {
        return data::makeError<MeshGeometry*>(
            4, "SliceRenderer: no asset registry injected");
    }
    // The shared AssetRegistry is the single owner of GPU geometry (SPEC §9
    // V2.5): resolving the handle returns the one GPU object registered for
    // this CPU mesh, shared with MeshRenderer.
    return registry_->resolve(handle);
}

data::Result<void> SliceRenderer::render(const SliceScene& scene,
                                         const Camera& camera,
                                         const ClipPlane& plane,
                                         const RenderTarget& target) {
    if (target.width == 0u || target.height == 0u) {
        return data::makeError<void>(1, "SliceRenderer: invalid target size");
    }

    auto programResult = clipProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;

    // Bind the target and prepare draw state. A null framebuffer means the
    // window's on-screen default framebuffer (T12/T13 interactive samples);
    // otherwise bind the offscreen FBO. v1 FBOs are color-only (no depth
    // attachment, SPEC §6 / docs/core.md), so the depth test is left off.
    if (target.framebuffer == nullptr) {
        core::bindDefaultFramebuffer();
    } else {
        target.framebuffer->bind();
    }
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
        if (!instance.material || instance.mesh.isNull()) {
            continue;
        }
        // Slicing does not use OIT in v1 (SPEC §3): every instance is clipped
        // and drawn through the clip pass regardless of material transparency.
        auto geometry = geometryFor(instance.mesh);
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

data::Result<void> SliceRenderer::render(const Scene& scene,
                                          const Camera& camera,
                                          const RenderTarget& target) {
    const SliceScene* const* sliceScene = std::get_if<const SliceScene*>(&scene);
    if (sliceScene == nullptr || *sliceScene == nullptr) {
        // The dispatch contract (SPEC §9 V2.3) rejects a scene of a different
        // technique — or the null "no scene" payload (render/types.hpp) — with
        // a typed error instead of throwing or crashing (SPEC §5).
        return data::makeError<void>(
            2, "SliceRenderer: scene does not hold a SliceScene");
    }
    const SliceScene& slice = **sliceScene;
    return render(slice, camera, slice.plane, target);
}

data::Result<void> SliceRenderer::drawLayer(const SliceScene& scene, const Camera& camera,
                                            core::DrawContext& ctx) {
    return drawLayer(scene, camera, scene.plane, ctx);
}

data::Result<void> SliceRenderer::drawLayer(const SliceScene& scene, const Camera& camera,
                                            const ClipPlane& plane, core::DrawContext& ctx) {
    // ReView already bind+viewport+clear via ctx; does not clear between layers.
    (void)ctx;
    auto programResult = clipProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code, programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;
    program->use();
    program->setUniformMat4("uView", camera.view);
    program->setUniformMat4("uProj", camera.proj);
    program->setUniformVec3("uPlaneNormal", plane.normal);
    program->setUniformVec3("uPlanePoint", plane.point);
    for (const MeshInstance& instance : scene.meshes) {
        if (!instance.material || instance.mesh.isNull()) {
            continue;
        }
        auto geometry = geometryFor(instance.mesh);
        if (geometry.failed()) {
            return data::makeError<void>(geometry.error().code, geometry.error().message);
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
    // kMaxVerticesPerTriangle vertices (each 3 floats). The per-instance
    // triangle count comes from the GPU geometry resolved through the shared
    // registry (SPEC §9 V2.5; scenes carry handles, not CPU pointers).
    std::size_t totalCapacityVertices = 0u;
    for (const MeshInstance& instance : scene.meshes) {
        if (instance.mesh.isNull()) {
            continue;
        }
        auto geometry = geometryFor(instance.mesh);
        if (geometry.failed()) {
            return data::makeError<void>(geometry.error().code,
                                         geometry.error().message);
        }
        totalCapacityVertices +=
            (*geometry)->triangleCount() * kMaxVerticesPerTriangle;
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
        if (instance.mesh.isNull()) {
            continue;
        }
        auto geometry = geometryFor(instance.mesh);
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
