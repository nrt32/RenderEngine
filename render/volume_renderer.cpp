// render/volume_renderer.cpp — VolumeRenderer implementation: GPU ray-cast
// through the uploaded 3D texture (front-to-back compositing under a transfer
// function). Sampling runs in texture space so the math is independent of the
// dataset's world placement; the model matrix only positions the slab
// intersection. Center-pixel output is deterministic for fixed cameras, which
// is what the analytic readback gates rely on.

#include "render/volume_renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "core/re_context.hpp"
#include "core/shader_program.hpp"
#include "volume/color.hpp"

namespace re::render {

VolumeRenderer::VolumeRenderer(std::shared_ptr<AssetRegistry> assets)
    : assets_(std::move(assets)) {}

// Ray-cast shaders live as .glsl files under render/shaders/ (SPEC §9 V2.6)
// and are loaded via core::ShaderProgram's file helpers. The fragment shader
// mirrors the pure volume/ math (FR-vol.1/2/3).

// Maximum transfer-function control points the shader accepts (uniform array
// size in the GLSL). A volume::TransferFunction with more points is
// rejected with a typed error.
constexpr std::size_t kMaxTfPoints = 8u;

std::pair<glm::vec3, glm::vec3> VolumeRenderer::worldAabb(
    const VolumeInstance& instance) {
    // The dataset occupies [0,1]^3 in model space; transform the 8 corners by
    // the model matrix and take the axis-aligned bounding box. Exact for
    // axis-aligned scaling/translation (the v1 case); conservative for rotated
    // models.
    glm::vec3 minv(std::numeric_limits<float>::max());
    glm::vec3 maxv(std::numeric_limits<float>::lowest());
    for (std::uint32_t i = 0u; i < 8u; ++i) {
        const glm::vec3 corner{static_cast<float>((i & 1u) ? 1u : 0u),
                               static_cast<float>((i & 2u) ? 1u : 0u),
                               static_cast<float>((i & 4u) ? 1u : 0u)};
        const glm::vec4 world = instance.model * glm::vec4(corner, 1.0f);
        minv = glm::min(minv, glm::vec3(world));
        maxv = glm::max(maxv, glm::vec3(world));
    }
    return {minv, maxv};
}

data::Result<core::ShaderProgram*> VolumeRenderer::rayCastProgram() {
    if (rayCastProgram_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*rayCastProgram_);
    }
    const std::filesystem::path dir = RE_SHADER_DIR;
    auto program = core::ShaderProgram::createFromFiles(
        dir / "volume_raycast.vert.glsl", dir / "volume_raycast.frag.glsl");
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    rayCastProgram_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*rayCastProgram_);
}

data::Result<core::VertexArray*> VolumeRenderer::screenQuad() {
    if (!screenQuad_.has_value()) {
        auto quad = ScreenQuad::create();
        if (quad.failed()) {
            return data::makeError<core::VertexArray*>(quad.error().code,
                                                       quad.error().message);
        }
        screenQuad_ = std::move(*quad);
    }
    return data::makeValue<core::VertexArray*>(&screenQuad_->vao());
}

data::Result<core::Texture3D*> VolumeRenderer::textureFor(
    const VolumeTextureHandle& handle) {
    // Owner-driven T7: hashed at register time, O(1) handle resolve, never
    // per-frame FNV-1a (data/content_hash.hpp:31). The lookupVolume lazy
    // path is deleted — content-hash IS identity via the handle's
    // contentHash field, no pointer-key shim, no pinned refs==0 slots.
    if (handle.isNull()) {
        return data::makeError<core::Texture3D*>(
            4, "VolumeRenderer: null volume handle (register via AssetRegistry)");
    }
    return assets_->resolveVolume(handle);
}

void VolumeRenderer::uploadTransferFunction(
    const volume::TransferFunction& tf) const {
    const std::size_t count = tf.size();
    std::vector<float> values(count);
    std::vector<glm::vec4> colors(count);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& cp = tf.controlPoints()[i];
        values[i] = cp.value;
        colors[i] = glm::vec4(cp.color.r, cp.color.g, cp.color.b, cp.color.a);
    }
    rayCastProgram_->setUniformInt("uTfCount",
                                   static_cast<std::int32_t>(count));
    rayCastProgram_->setUniformFloatArray("uTfValues", values.data(), count);
    rayCastProgram_->setUniformVec4Array("uTfColors", colors.data(), count);
}

data::Result<void> VolumeRenderer::drawInstances(const VolumeScene& scene,
                                                 const Camera& camera,
                                                 core::ShaderProgram* program,
                                                 core::VertexArray* quadVao) {
    program->use();
    program->setUniformMat4("uViewProj", camera.proj * camera.view);
    program->setUniformInt("uVolume", 0); // sampler reads texture unit 0

    for (const VolumeInstance& instance : scene.volumes) {
        // T7 owner-driven: prefer explicit handle; fallback to legacy
        // dataset→handle cache for pre-T7 direct tests (hashed once at
        // first use, then O(1) cache hit — no per-frame FNV-1a).
        VolumeTextureHandle handle = instance.handle;
        if (handle.isNull()) {
            if (!instance.dataset) {
                return data::makeError<void>(
                    1, "VolumeRenderer: volume instance carries null handle and null dataset");
            }
            auto it = legacyHandleCache_.find(instance.dataset.get());
            if (it != legacyHandleCache_.end()) {
                handle = it->second;
            } else {
                auto reg = assets_->registerVolume(instance.dataset);
                if (reg.failed()) {
                    return data::makeError<void>(reg.error().code, reg.error().message);
                }
                handle = *reg;
                legacyHandleCache_[instance.dataset.get()] = handle;
            }
        }
        if (!instance.dataset) {
            return data::makeError<void>(
                1, "VolumeRenderer: volume instance dataset is null "
                   "(required for size/uniforms)");
        }
        if (instance.transferFunction.size() > kMaxTfPoints) {
            return data::makeError<void>(
                1, "VolumeRenderer: transfer function has more than " +
                       std::to_string(kMaxTfPoints) + " control points");
        }

        auto texture = textureFor(handle);
        if (texture.failed()) {
            return data::makeError<void>(texture.error().code,
                                         texture.error().message);
        }
        core::Texture3D* texPtr = *texture;
        texPtr->bind(0u);

        const auto [boxMin, boxMax] = worldAabb(instance);
        const glm::mat4 invModel = glm::inverse(instance.model);
        const glm::vec3 size(static_cast<float>(instance.dataset->sizeX()),
                             static_cast<float>(instance.dataset->sizeY()),
                             static_cast<float>(instance.dataset->sizeZ()));

        program->setUniformVec3("uBoxMin", boxMin);
        program->setUniformVec3("uBoxMax", boxMax);
        program->setUniformMat4("uInvModel", invModel);
        program->setUniformVec3("uSize", size);
        program->setUniformFloat("uStepLength", kDefaultStepLength);
        uploadTransferFunction(instance.transferFunction);

        auto draw =
            core::drawElements(*quadVao, kQuadTriangleIndices.size());
        if (draw.failed()) {
            return draw;
        }
    }
    return data::Result<void>(data::value);
}

data::Result<void> VolumeRenderer::render(const VolumeScene& scene,
                                          const Camera& camera,
                                          const RenderTarget& target) {
    if (assets_ == nullptr) {
        // Constructed with a null store (member-init-order safety): fail with
        // a typed error instead of dereferencing (mirrors MeshRenderer).
        return data::makeError<void>(4, "VolumeRenderer: no shared asset store");
    }
    if (target.width == 0u || target.height == 0u) {
        return data::makeError<void>(1, "VolumeRenderer: invalid target size");
    }

    auto programResult = rayCastProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;

    auto quadResult = screenQuad();
    if (quadResult.failed()) {
        return data::makeError<void>(quadResult.error().code,
                                     quadResult.error().message);
    }
    core::VertexArray* quadVao = *quadResult;

    // Begin the pass through the ONE shared prologue (bind target → viewport
    // → clear → depth state → blend off). A null framebuffer selects the
    // window's on-screen default framebuffer; otherwise the offscreen FBO is
    // bound. Direct single-scene renders keep the deterministic depth-off
    // painter's-order pass (a target's optional depth attachment is consumed
    // only via the per-view opt-in), so the depth test is left off; blending
    // is off because the shader already writes the final premultiplied
    // composited color.
    auto& ctx = core::REContext::current();
    ctx.beginPass(target.framebuffer, target.width, target.height,
                  target.clearColor.r, target.clearColor.g,
                  target.clearColor.b, target.clearColor.a);

    return drawInstances(scene, camera, program, quadVao);
}

data::Result<void> VolumeRenderer::render(const Scene& scene,
                                           const Camera& camera,
                                           const RenderTarget& target) {
    const VolumeScene* const* volumeScene =
        std::get_if<const VolumeScene*>(&scene);
    if (volumeScene == nullptr || *volumeScene == nullptr) {
        // The dispatch contract (SPEC §9 V2.3) rejects a scene of a different
        // technique — or the null "no scene" payload (render/types.hpp) — with
        // a typed error instead of throwing or crashing (SPEC §5).
        return data::makeError<void>(
            2, "VolumeRenderer: scene does not hold a VolumeScene");
    }
    return render(**volumeScene, camera, target);
}

data::Result<void> VolumeRenderer::drawLayer(const VolumeScene& scene, const Camera& camera) {
    // ReView already bind+viewport+clear via ctx; does not clear between layers.
    // T2: (void)ctx removed — REContext::current() is the global per-GL-context single writer
    if (assets_ == nullptr) {
        return data::makeError<void>(4, "VolumeRenderer: no shared asset store");
    }
    auto programResult = rayCastProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code, programResult.error().message);
    }
    auto quadResult = screenQuad();
    if (quadResult.failed()) {
        return data::makeError<void>(quadResult.error().code, quadResult.error().message);
    }
    return drawInstances(scene, camera, *programResult, *quadResult);
}

} // namespace re::render
