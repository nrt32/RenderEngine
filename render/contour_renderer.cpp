// render/contour_renderer.cpp — ContourRenderer implementation (FR-app.3,
// V3.8b T11).

#include "render/contour_renderer.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include "core/draw.hpp"
#include "core/shader_program.hpp"

namespace re::render {

// Shaders live as .glsl files under render/shaders/ (SPEC §9 V2.6) and are
// loaded via core::ShaderProgram's file helpers. The plane is defined in
// world space by a unit normal + point; the outline is the set of segments
// where the plane cuts the mesh surface (computed by contour.geom.glsl).

ContourRenderer::ContourRenderer(AssetRegistry* registry)
    : registry_(registry) {}

data::Result<core::ShaderProgram*> ContourRenderer::program() {
    if (program_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*program_);
    }
    const std::filesystem::path dir = RE_SHADER_DIR;
    auto program = core::ShaderProgram::createWithGeometryFromFiles(
        dir / "contour.vert.glsl", dir / "contour.geom.glsl",
        dir / "contour.frag.glsl");
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    program_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*program_);
}

data::Result<MeshGeometry*> ContourRenderer::geometryFor(
    const AssetHandle& handle) {
    if (registry_ == nullptr) {
        return data::makeError<MeshGeometry*>(
            1, "ContourRenderer: no asset registry injected");
    }
    // The shared AssetRegistry is the single owner of GPU geometry (SPEC §9
    // V2.5): resolving the handle returns the one GPU object registered for
    // this CPU mesh, shared with MeshRenderer and SliceRenderer.
    return registry_->resolve(handle);
}

data::Result<void> ContourRenderer::drawOne(const ContourObject& object,
                                            core::ShaderProgram* program) {
    if (object.mesh.isNull()) {
        return data::makeError<void>(3, "ContourRenderer: null AssetHandle");
    }
    auto geometry = geometryFor(object.mesh);
    if (geometry.failed()) {
        return data::makeError<void>(geometry.error().code,
                                     geometry.error().message);
    }
    program->setUniformMat4("uModel", object.model);
    program->setUniformVec4("uColor", object.color);
    program->setUniformFloat("uHalfWidthPx", object.halfWidthPx);
    // The draw stays GL_TRIANGLES (MeshGeometry's indexed triangle draw); the
    // geometry shader converts each crossing triangle to its outline quad.
    return (*geometry)->draw();
}

data::Result<void> ContourRenderer::render(const ContourScene& scene,
                                           const Camera& camera,
                                           const RenderTarget& target) {
    if (target.width == 0u || target.height == 0u) {
        return data::makeError<void>(1, "ContourRenderer: invalid target size");
    }

    auto programResult = program();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* programPtr = *programResult;

    // Bind the target and prepare draw state. A null framebuffer means the
    // window's on-screen default framebuffer (interactive samples); v1 FBOs
    // are color-only (no depth attachment, SPEC §6 / docs/core.md), so the
    // depth test is left off. Blending must be OFF so every stroke pixel is
    // exactly uColor (the FR-app.3 readback compares exact bytes ±1/255).
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

    programPtr->use();
    programPtr->setUniformMat4("uView", camera.view);
    programPtr->setUniformMat4("uProj", camera.proj);
    programPtr->setUniformVec2("uViewport",
                               glm::vec2(static_cast<float>(target.width),
                                         static_cast<float>(target.height)));
    for (const ContourObject& object : scene.contours) {
        programPtr->setUniformVec3("uPlaneNormal", object.plane.normal);
        programPtr->setUniformVec3("uPlanePoint", object.plane.point);
        auto drawn = drawOne(object, programPtr);
        if (drawn.failed()) {
            return drawn;
        }
    }
    return data::Result<void>(data::value);
}

data::Result<void> ContourRenderer::drawLayer(const ContourObject& object,
                                              const Camera& camera,
                                              core::DrawContext& ctx) {
    auto programResult = program();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* programPtr = *programResult;

    // The thick-line expansion works in viewport pixels; ReView already set
    // the viewport on this context (View::render), so read it from the cache
    // — render/ never issues raw GL queries (guardrail gpu_api_ownership).
    int vpX = 0;
    int vpY = 0;
    int vpW = 0;
    int vpH = 0;
    if (!ctx.viewportRect(vpX, vpY, vpW, vpH) || vpW <= 0 || vpH <= 0) {
        return data::makeError<void>(
            2, "ContourRenderer::drawLayer: DrawContext has no viewport "
               "(View::render must setViewport before layers)");
    }

    programPtr->use();
    programPtr->setUniformMat4("uView", camera.view);
    programPtr->setUniformMat4("uProj", camera.proj);
    programPtr->setUniformVec2("uViewport", glm::vec2(static_cast<float>(vpW),
                                                      static_cast<float>(vpH)));
    programPtr->setUniformVec3("uPlaneNormal", object.plane.normal);
    programPtr->setUniformVec3("uPlanePoint", object.plane.point);
    return drawOne(object, programPtr);
}

data::Result<void> ContourRenderer::drawLayer(const ContourScene& scene,
                                              const Camera& camera,
                                              core::DrawContext& ctx) {
    for (const ContourObject& object : scene.contours) {
        auto drawn = drawLayer(object, camera, ctx);
        if (drawn.failed()) {
            return drawn;
        }
    }
    return data::Result<void>(data::value);
}

} // namespace re::render
