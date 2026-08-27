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
    if (opaqueProgram_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*opaqueProgram_);
    }
    const std::filesystem::path dir = RE_SHADER_DIR;
    auto program = core::ShaderProgram::createFromFiles(
        dir / "mesh_opaque.vert.glsl", dir / "mesh_opaque.frag.glsl");
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    opaqueProgram_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*opaqueProgram_);
}

data::Result<void> MeshRenderer::drawInstances(const MeshScene& scene,
                                               const Camera& camera,
                                               core::ShaderProgram* program,
                                               bool skipTransparent) {
    program->use();
    program->setUniformMat4("uView", camera.view);
    program->setUniformMat4("uProj", camera.proj);

    for (const MeshInstance& instance : scene.meshes) {
        if (!instance.material || instance.mesh.isNull()) {
            // The null AssetHandle {0,0} is the "no mesh" instance (reserved,
            // render/asset_registry.hpp); it is skipped like the pre-V2 null
            // mesh pointer.
            continue;
        }
        if (skipTransparent && instance.material->isTransparent()) {
            // Opaque path only: transparent meshes are composited by the OIT
            // pipeline, not this pass (FR-render.2/3). The drawLayer path
            // passes skipTransparent=false — see drawInstances.
            continue;
        }
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

data::Result<void> MeshRenderer::drawOpaque(const MeshScene& scene,
                                            const Camera& camera,
                                            const RenderTarget& target) {
    (void)target;
    auto programResult = opaqueProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    // The direct opaque pass leaves transparent instances to the OIT pipeline.
    return drawInstances(scene, camera, *programResult,
                         /*skipTransparent=*/true);
}

data::Result<void> MeshRenderer::drawTransparent(const MeshScene& scene,
                                                 const Camera& camera) {
    if (transparency_ == nullptr) {
        return data::Result<void>(data::value);
    }
    for (const MeshInstance& instance : scene.meshes) {
        if (!instance.material || instance.mesh.isNull()) {
            continue;
        }
        if (!instance.material->isTransparent()) {
            continue;
        }
        auto geometry = resolveMeshGeometry(registry_, instance.mesh,
                                            "MeshRenderer");
        if (geometry.failed()) {
            return data::makeError<void>(geometry.error().code,
                                         geometry.error().message);
        }
        MeshGeometry* geometryPtr = *geometry;
        const data::Result<void> capture = transparency_->drawTransparent(
            *geometryPtr, instance.material->baseColor(), instance.model,
            camera);
        if (capture.failed()) {
            return capture;
        }
    }
    return data::Result<void>(data::value);
}

data::Result<void> MeshRenderer::render(const MeshScene& scene,
                                        const Camera& camera,
                                        const RenderTarget& target) {
    if (target.width == 0u || target.height == 0u) {
        return data::makeError<void>(1, "MeshRenderer: invalid target size");
    }

    // OIT is a scene-level characteristic, not a renderer setting: scan the
    // instances once and engage the transparency pipeline only when at least
    // one material is actually transparent. Pure-opaque scenes therefore keep
    // the cheap direct path (and its deterministic pixel output), while
    // blended scenes pay for the linked-list composite.
    bool anyTransparent = false;
    for (const MeshInstance& instance : scene.meshes) {
        if (instance.material && instance.material->isTransparent()) {
            anyTransparent = true;
            break;
        }
    }

    // Begin the pass through the ONE shared prologue (bind target → viewport
    // → clear → depth state → blend off). A null framebuffer selects the
    // window's on-screen default framebuffer; otherwise the offscreen FBO is
    // bound. Direct single-scene renders keep the deterministic depth-off
    // painter's-order pass — true occlusion via a depth attachment + enabled
    // depth test is a per-view opt-in (render::View::setDepthTest), applied by
    // the View's own prologue call when a composition needs it. The OIT passes
    // below therefore always run with the depth test OFF exactly as before:
    // the capture draws immediately after this depth-off prologue, and end()
    // issues its own explicit core::disableDepthTest() — so both behave
    // identically on color-only and depth-enabled targets.
    auto& ctx = core::REContext::current();
    ctx.beginPass(target.framebuffer, target.width, target.height,
                  target.clearColor.r, target.clearColor.g,
                  target.clearColor.b, target.clearColor.a);

    if (anyTransparent && transparency_ != nullptr) {
        // Engage the OIT pipeline (capture -> depth-sort -> composite): draw
        // the opaque meshes first, then capture the transparent meshes through
        // the pipeline, then let end() composite (FR-render.2/3).
        const data::Result<void> begin = transparency_->begin(camera, target);
        if (begin.failed()) {
            // The pipeline could not be engaged: abort the frame and surface
            // the typed error to the caller (SPEC §5, never silent). The
            // target was already cleared and is left unmodified.
            return begin;
        }
        const data::Result<void> opaque = drawOpaque(scene, camera, target);
        if (opaque.failed()) {
            transparency_->end(camera, target);
            return opaque;
        }
        const data::Result<void> transparent = drawTransparent(scene, camera);
        if (transparent.failed()) {
            transparency_->end(camera, target);
            return transparent;
        }
        return transparency_->end(camera, target);
    }

    return drawOpaque(scene, camera, target);
}

data::Result<void> MeshRenderer::render(const Scene& scene,
                                         const Camera& camera,
                                         const RenderTarget& target) {
    const MeshScene* const* meshScene = std::get_if<const MeshScene*>(&scene);
    if (meshScene == nullptr || *meshScene == nullptr) {
        // The dispatch contract (SPEC §9 V2.3) rejects a scene of a different
        // technique — or the null "no scene" payload (render/types.hpp) — with
        // a typed error instead of throwing or crashing (SPEC §5).
        return data::makeError<void>(
            2, "MeshRenderer: scene does not hold a MeshScene");
    }
    return render(**meshScene, camera, target);
}

data::Result<void> MeshRenderer::drawLayer(const MeshScene& scene, const Camera& camera) {
    // ReView already bind+viewport+clear via ctx; single-item render() keeps clear.
    // This layer draws without clearing — second layer must not clear away the first.
    // T2: (void)ctx removed — REContext::current() is the global per-GL-context single writer
    auto programResult = opaqueProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code, programResult.error().message);
    }
    // drawLayer draws EVERY resolvable instance unconditionally: it is one
    // layer of a multi-layer view composition, so transparency handling (OIT)
    // is orchestrated by the View, not re-decided per layer here. The direct
    // single-item render() path owns its own opaque/OIT split via
    // skipTransparent=true in drawOpaque — that split is not duplicated here.
    return drawInstances(scene, camera, *programResult,
                         /*skipTransparent=*/false);
}

} // namespace re::render
