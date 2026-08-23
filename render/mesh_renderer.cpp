// render/mesh_renderer.cpp — MeshRenderer implementation (SPEC §3,
// FR-render.1).

#include "render/mesh_renderer.hpp"

#include <filesystem>
#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

#include "core/draw.hpp"
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

data::Result<MeshGeometry*> MeshRenderer::geometryFor(
    const AssetHandle& handle) {
    if (!registry_) {
        // Typed error (code 4), never a null dereference: a renderer built
        // with a null registry (possible only by explicit request — member
        // init order can never produce one, T13) fails loudly per draw.
        return data::makeError<MeshGeometry*>(
            4, "MeshRenderer: no asset registry injected");
    }
    // The shared AssetRegistry is the single owner of GPU geometry (SPEC §9
    // V2.5): resolving the handle returns the one GPU object registered for
    // this CPU mesh, shared with every other mesh-family renderer.
    return registry_->resolve(handle);
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
    core::ShaderProgram* program = *programResult;

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
        if (instance.material->isTransparent()) {
            // Transparent meshes are composited by the OIT pipeline, not the
            // opaque forward pass (FR-render.2/3). They are skipped here.
            continue;
        }
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
        auto geometry = geometryFor(instance.mesh);
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

    // Determine whether any mesh is transparent (SPEC §3: OIT is a
    // characteristic of the scene; engaged only when transparency is present).
    bool anyTransparent = false;
    for (const MeshInstance& instance : scene.meshes) {
        if (instance.material && instance.material->isTransparent()) {
            anyTransparent = true;
            break;
        }
    }

    // Bind the target and prepare draw state. A null framebuffer means the
    // window's on-screen default framebuffer (T12); otherwise bind the
    // offscreen FBO. v1 FBOs are color-only (no depth attachment, SPEC §6 /
    // docs/core.md), so the depth test is left off.
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

data::Result<void> MeshRenderer::drawLayer(const MeshScene& scene, const Camera& camera,
                                           core::DrawContext& ctx) {
    // ReView already bind+viewport+clear via ctx; single-item render() keeps clear.
    // This layer draws without clearing — second layer must not clear away the first.
    (void)ctx;
    auto programResult = opaqueProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code, programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;
    program->use();
    program->setUniformMat4("uView", camera.view);
    program->setUniformMat4("uProj", camera.proj);
    for (const MeshInstance& instance : scene.meshes) {
        if (!instance.material || instance.mesh.isNull()) {
            continue;
        }
        // For View compositing we draw every instance (transparent handling via
        // OIT is orchestrated by View if needed; the gate's opaque mesh is drawn
        // here). The single-item render() path handles OIT separately.
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

} // namespace re::render
