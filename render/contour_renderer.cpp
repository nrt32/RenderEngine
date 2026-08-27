// render/contour_renderer.cpp — ContourRenderer implementation: draw the
// plane∩mesh OUTLINE as GPU line geometry. A geometry shader clips the mesh's
// triangles against the cut plane and emits a screen-space-expanded line
// strip for each intersection edge, so the contour is exact at any window
// size and no CPU triangle/plane tests exist anywhere in the app path.

#include "render/contour_renderer.hpp"

#include <filesystem>
#include <string>
#include <utility>

#include "core/re_context.hpp"
#include "core/shader_program.hpp"

namespace re::render {

// Shaders live as .glsl files under render/shaders/ (SPEC §9 V2.6) and are
// loaded via core::ShaderProgram's file helpers. The plane is defined in
// world space by a unit normal + point; the outline is the set of segments
// where the plane cuts the mesh surface (computed by contour.geom.glsl).

ContourRenderer::ContourRenderer(std::shared_ptr<AssetRegistry> registry)
    : registry_(std::move(registry)) {}

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

data::Result<void> ContourRenderer::drawOne(const ContourObject& object,
                                            core::ShaderProgram* program) {
    if (object.mesh.isNull()) {
        return data::makeError<void>(3, "ContourRenderer: null AssetHandle");
    }
    // The shared AssetRegistry is the single owner of GPU geometry (SPEC §9
    // V2.5): resolving the handle returns the one GPU object registered for
    // this CPU mesh, shared with MeshRenderer and SliceRenderer (the shared
    // resolveMeshGeometry helper — one definition for all mesh-family
    // renderers).
    auto geometry =
        resolveMeshGeometry(registry_, object.mesh, "ContourRenderer");
    if (geometry.failed()) {
        return data::makeError<void>(geometry.error().code,
                                     geometry.error().message);
    }
    program->setUniformMat4("uModel", object.model);
    program->setUniformVec4("uColor", object.color);
    program->setUniformFloat("uHalfWidthPx", object.halfWidthPx);
    // The draw stays on triangle primitives (MeshGeometry's indexed triangle
    // draw); the geometry shader converts each crossing triangle to its
    // outline quad.
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

    // Begin the pass through the ONE shared prologue (bind target → viewport
    // → clear → depth state → blend off). A null framebuffer selects the
    // window's on-screen default framebuffer (interactive samples); direct
    // single-scene renders keep the deterministic depth-off painter's-order
    // pass (a target's optional depth attachment is consumed only via the
    // per-view opt-in), so the depth test is left off. Blending must be OFF
    // so every stroke pixel is exactly uColor (the FR-app.3 readback compares
    // exact bytes ±1/255) —
    // beginPass disables it.
    auto& ctx = core::REContext::current();
    ctx.beginPass(target.framebuffer, target.width, target.height,
                  target.clearColor.r, target.clearColor.g,
                  target.clearColor.b, target.clearColor.a);

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

data::Result<void> ContourRenderer::drawLayer(const ContourObject& object, const Camera& camera) {
    auto programResult = program();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* programPtr = *programResult;

    // The thick-line expansion works in viewport pixels; ReView already set
    // the viewport on the global REContext::current() (T2: thread_local
    // GLFWwindow* → REContextState, 2 layers sharing viewport issue 1
    // glViewport), so read it from the current's cache — render/ never issues
    // raw GL queries (guardrail gpu_api_ownership).
    int vpX = 0;
    int vpY = 0;
    int vpW = 0;
    int vpH = 0;
    if (!core::REContext::current().viewportRect(vpX, vpY, vpW, vpH) || vpW <= 0 || vpH <= 0) {
        return data::makeError<void>(
            2, "ContourRenderer::drawLayer: REContext has no viewport "
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

data::Result<void> ContourRenderer::drawLayer(const ContourScene& scene, const Camera& camera) {
    for (const ContourObject& object : scene.contours) {
        auto drawn = drawLayer(object, camera);
        if (drawn.failed()) {
            return drawn;
        }
    }
    return data::Result<void>(data::value);
}

} // namespace re::render
