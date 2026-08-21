// render/mesh_renderer.cpp — MeshRenderer implementation (SPEC §3,
// FR-render.1).

#include "render/mesh_renderer.hpp"

#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

#include "core/draw.hpp"

namespace re::render {

namespace {

// Opaque forward-pass shader, GLSL 450 (SPEC §8: gate/test shaders compile on
// llvmpipe which caps at 4.50).
//
// v1's opaque pass evaluates a deliberately deterministic Phong configuration
// (docs/render.md): a fixed head-on directional light from +Z with ambient=0,
// diffuse=1, specular=0. The resulting shade is
//   color = baseColor.rgb * max(dot(N, (0,0,1)), 0)
// so a front-facing surface (normal aligned with +Z) renders at exactly the
// material's base color, and a back-facing surface is black. The material's
// alpha is passed straight through, so an opaque material writes alpha 1.0.
// This makes the FR-render.1 center-pixel acceptance fully analytic. The
// PhongMaterial's ambient/diffuse/specular/shininess parameters are reserved
// for the future lit path (samples, T12); the v1 gate pass is intentionally
// unlit-flat for determinism.

constexpr char kOpaqueVertexShader[] =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec3 aNormal;\n"
    "uniform mat4 uModel;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "out vec3 vNormal;\n"
    "void main() {\n"
    "    vNormal = mat3(uModel) * aNormal;\n"
    "    vec4 worldPos = uModel * vec4(aPos, 1.0);\n"
    "    gl_Position = uProj * uView * worldPos;\n"
    "}\n";

constexpr char kOpaqueFragmentShader[] =
    "#version 450 core\n"
    "in vec3 vNormal;\n"
    "uniform vec4 uBaseColor;\n"
    "layout(location = 0) out vec4 oColor;\n"
    "void main() {\n"
    "    vec3 n = normalize(vNormal);\n"
    "    float shade = max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0);\n"
    "    oColor = vec4(uBaseColor.rgb * shade, uBaseColor.a);\n"
    "}\n";

} // namespace

MeshRenderer::MeshRenderer(ITransparencyPipeline* transparency)
    : transparency_(transparency) {}

data::Result<core::ShaderProgram*> MeshRenderer::opaqueProgram() {
    if (opaqueProgram_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*opaqueProgram_);
    }
    auto program =
        core::ShaderProgram::create(kOpaqueVertexShader, kOpaqueFragmentShader);
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    opaqueProgram_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*opaqueProgram_);
}

data::Result<MeshGeometry*> MeshRenderer::geometryFor(const data::Mesh& mesh) {
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
        if (instance.material == nullptr || instance.mesh == nullptr) {
            continue;
        }
        if (instance.material->isTransparent()) {
            // Transparent meshes are composited by the OIT pipeline, not the
            // opaque forward pass (FR-render.2/3). They are skipped here.
            continue;
        }
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

data::Result<void> MeshRenderer::drawTransparent(const MeshScene& scene,
                                                 const Camera& camera) {
    if (transparency_ == nullptr) {
        return data::Result<void>(data::value);
    }
    for (const MeshInstance& instance : scene.meshes) {
        if (instance.material == nullptr || instance.mesh == nullptr) {
            continue;
        }
        if (!instance.material->isTransparent()) {
            continue;
        }
        auto geometry = geometryFor(*instance.mesh);
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
    if (target.framebuffer == nullptr) {
        return data::makeError<void>(1,
                                     "MeshRenderer: null target framebuffer");
    }

    // Determine whether any mesh is transparent (SPEC §3: OIT is a
    // characteristic of the scene; engaged only when transparency is present).
    bool anyTransparent = false;
    for (const MeshInstance& instance : scene.meshes) {
        if (instance.material != nullptr &&
            instance.material->isTransparent()) {
            anyTransparent = true;
            break;
        }
    }

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

} // namespace re::render
