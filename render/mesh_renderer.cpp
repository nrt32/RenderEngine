// render/mesh_renderer.cpp — MeshRenderer implementation: opaque Phong
// pass first, then (only when the scene actually contains transparent
// instances) the order-independent-transparency pass. Geometry is resolved by
// AssetHandle through the shared asset store, so the same CPU mesh uploaded
// twice still draws from one GPU buffer.

#include "render/mesh_renderer.hpp"

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

#include "core/re_context.hpp"
#include "core/shader_program.hpp"
#include "render/view.hpp"

#include <glm/geometric.hpp>

namespace re::render {

// Opaque forward-pass shaders live as .glsl files under render/shaders/
// (SPEC §9 V2.6) and are loaded via core::ShaderProgram's file helpers.
// The deterministic v1 flat lighting is documented in docs/render.md:
// a fixed head-on directional light from +Z with ambient=0, diffuse=1,
// specular=0, so a front-facing surface renders at exactly the base color.

MeshRenderer::MeshRenderer(std::shared_ptr<AssetRegistry> registry,
                           std::shared_ptr<ITransparencyPipeline> transparency)
    : registry_(std::move(registry)), transparency_(std::move(transparency)) {}

data::Result<core::ShaderProgram*> MeshRenderer::opaqueProgram() {
    const std::filesystem::path dir = RE_SHADER_DIR;
    return opaqueProgram_.getOrLoadFromFiles(
        dir / "mesh_opaque.vert.glsl", dir / "mesh_opaque.frag.glsl");
}

data::Result<void> MeshRenderer::drawInstances(const MeshScene& scene,
                                               const Camera& camera,
                                               core::ShaderProgram* program) {
    program->use();
    program->setUniformMat4("uView", camera.view);
    program->setUniformMat4("uProj", camera.proj);
    glm::vec3 lightDir{0.0f, 0.0f, 1.0f};
    if (auto* cur = currentViewLights(); cur && !cur->empty()) {
        glm::vec3 d = cur->front().dirWS;
        float len = glm::length(d);
        if (len > 1e-6f) lightDir = d / len;
    }
    program->setUniformVec3("uLightDir", lightDir);

    for (const MeshInstance& instance : scene.meshes) {
        if (!instance.material || instance.mesh.isNull()) {
            // The null AssetHandle {0,0} is the "no mesh" instance (reserved,
            // render/asset_registry.hpp); it is skipped like the pre-V2 null
            // mesh pointer.
            continue;
        }
        // T14: single transparent behavior — draw every resolvable instance
        // with blending off (no silent drop). OIT capture is orchestrated by
        // the ViewCompositor out-of-band; drawLayer never re-decides per layer.
        auto geometry = resolveMeshGeometry(registry_, instance.mesh,
                                            "MeshRenderer");
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

data::Result<void> MeshRenderer::drawLayer(const MeshScene& scene, const Camera& camera) {
    // ReView already bind+viewport+clear via REContext::current(); single-item
    // render() keeps clear. This layer draws without clearing — second layer
    // must not clear away the first.
    auto programResult = opaqueProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code, programResult.error().message);
    }
    // drawLayer draws every resolvable instance unconditionally with blending
    // off (single transparent behavior, no silent drop). OIT is orchestrated by
    // the ViewCompositor out-of-band when a pipeline is wired.
    return drawInstances(scene, camera, *programResult);
}

} // namespace re::render
