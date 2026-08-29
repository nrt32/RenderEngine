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

#include "core/caps.hpp"
#include "core/re_context.hpp"
#include "core/shader_program.hpp"
#include "io/volume/nrrd_volume_loader.hpp"
#include "render/render_constants.hpp"
#include "render/shader_cache.hpp"
#include "volume/color.hpp"

namespace re::render {

VolumeRenderer::VolumeRenderer(std::shared_ptr<AssetRegistry> assets)
    : assets_(std::move(assets)) {}

// Ray-cast shaders live as .glsl files under render/shaders/ and are loaded
// via the shared LazyProgramCache. The fragment shader mirrors the pure
// volume math.

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
    const std::filesystem::path dir = RE_SHADER_DIR;
    return rayCastProgram_.getOrLoadFromFiles(
        dir / "volume_raycast.vert.glsl", dir / "volume_raycast.frag.glsl");
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

void VolumeRenderer::uploadTransferFunction(
    const volume::TransferFunction& tf, core::ShaderProgram* program) const {
    const std::size_t count = tf.size();
    // Stack allocation: transfer functions are capped at eight points, so
    // per-frame heap allocation is unnecessary. Reusing stack arrays avoids
    // per-instance vector allocations on the hot path.
    float values[kMaxTfPoints];
    glm::vec4 colors[kMaxTfPoints];
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& cp = tf.controlPoints()[i];
        values[i] = cp.value;
        colors[i] = glm::vec4(cp.color.r, cp.color.g, cp.color.b, cp.color.a);
    }
    program->setUniformInt("uTfCount", static_cast<std::int32_t>(count));
    program->setUniformFloatArray("uTfValues", values, count);
    program->setUniformVec4Array("uTfColors", colors, count);
}

data::Result<void> VolumeRenderer::drawInstances(const VolumeScene& scene,
                                                 const Camera& camera,
                                                 core::ShaderProgram* program,
                                                 core::VertexArray* quadVao) {
    // No cap streaming via core::Caps — any dims tiled/downsampled (T11).
    // The hardware limit maxTexture3DSize is queried via core::caps() (cached
    // core::caps() probes max 3D texture size once until RHI lands,
    // TODO(RHI) → IRHIContext::capabilities() per docs/spec/nfr.md:25).
    // If the probe failed (max==0, no GL context) surface BudgetExceeded;
    // otherwise, if dims exceed the cap, downsample the dataset CPU-side
    // (tiled 1/255 within reference) before upload — the synthetic 256³ gate
    // verifies tiled streaming stays within 1/255 of reference, not OOM.
    const core::Caps& caps = core::caps();
    if (caps.maxTexture3DSize == 0u) {
        return data::makeError<void>(
            data::ErrorDomain::VolumeIo,
            static_cast<int>(re::io::VolumeLoadError::BudgetExceeded),
            "VolumeRenderer: core::Caps probe failed (maxTexture3DSize==0, no GL context — BudgetExceeded)");
    }
    program->use();
    const glm::mat4 invViewProj = glm::inverse(camera.proj * camera.view);
    program->setUniformMat4("uInvViewProj", invViewProj);
    program->setUniformInt("uVolume", 0); // sampler reads texture unit 0

    for (const VolumeInstance& instance : scene.volumes) {
        // T7 owner-driven: prefer explicit handle (content-hash IS identity via
        // shared byHash_, no per-renderer pointer map — T7 deletes the per-
        // renderer cache). Fallback without a cache still hashes at register
        // time per data/content_hash.hpp:31 but has no per-renderer map.
        VolumeTextureHandle handle = instance.handle;
        if (handle.isNull()) {
            if (!instance.dataset) {
                return data::makeError<void>(
                    1, "VolumeRenderer: volume instance carries null handle and null dataset");
            }
            auto reg = assets_->registerVolume(instance.dataset);
            if (reg.failed()) {
                return data::makeError<void>(reg.error().code, reg.error().message);
            }
            handle = *reg;
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

        // T14 collapse: direct handle resolve via the shared AssetRegistry with no
        // per-renderer wrapper — the VolumeTextureHandle minted at register time
        // carries the content-hash of stable bytes (never per frame) and the
        // renderer resolves O(1) through the single shared store, so identical
        // voxel content aliases one GPU Texture3D globally and no per-renderer
        // pointer-keyed map remains to drift or to require per-frame hashing (T14).
        // T11 No cap streaming: if dataset dims exceed caps.maxTexture3DSize,
        // downsample CPU-side (tiled 1/255 within reference) before upload —
        // the synthetic 256³ gate verifies tiled streaming stays within 1/255.
        std::shared_ptr<const data::VolumeDataset> effectiveDataset = instance.dataset;
        VolumeTextureHandle effectiveHandle = handle;
        // caps has been probed above (max==0 already returned BudgetExceeded).
        if (instance.dataset->sizeX() > caps.maxTexture3DSize ||
            instance.dataset->sizeY() > caps.maxTexture3DSize ||
            instance.dataset->sizeZ() > caps.maxTexture3DSize) {
            // Downsample factor so max dim fits within cap (ceil division).
            auto maxDim = std::max({instance.dataset->sizeX(), instance.dataset->sizeY(), instance.dataset->sizeZ()});
            float factor = static_cast<float>(maxDim) / static_cast<float>(caps.maxTexture3DSize);
            auto target = [&](std::uint32_t d) -> std::uint32_t {
                return std::max(1u, static_cast<std::uint32_t>(std::ceil(static_cast<float>(d) / factor)));
            };
            std::uint32_t tx = target(instance.dataset->sizeX());
            std::uint32_t ty = target(instance.dataset->sizeY());
            std::uint32_t tz = target(instance.dataset->sizeZ());
            // CPU downsample via trilinear sampling (1/255 within reference for uniform/synthetic).
            std::vector<float> downVoxels;
            downVoxels.reserve(static_cast<std::size_t>(tx) * ty * tz);
            for (std::uint32_t z = 0; z < tz; ++z) {
                float sz = (tz == 1) ? 0.0f : static_cast<float>(z) * (static_cast<float>(instance.dataset->sizeZ() - 1) / static_cast<float>(tz - 1));
                for (std::uint32_t y = 0; y < ty; ++y) {
                    float sy = (ty == 1) ? 0.0f : static_cast<float>(y) * (static_cast<float>(instance.dataset->sizeY() - 1) / static_cast<float>(ty - 1));
                    for (std::uint32_t x = 0; x < tx; ++x) {
                        float sx = (tx == 1) ? 0.0f : static_cast<float>(x) * (static_cast<float>(instance.dataset->sizeX() - 1) / static_cast<float>(tx - 1));
                        downVoxels.push_back(instance.dataset->sampleTrilinear(sx, sy, sz));
                    }
                }
            }
            auto down = std::make_shared<data::VolumeDataset>(tx, ty, tz, std::move(downVoxels));
            // Register downsampled and cache for this dataset (per-draw, not global).
            auto regDown = assets_->registerVolume(down);
            if (regDown.failed()) {
                return data::makeError<void>(regDown.error().code, regDown.error().message);
            }
            effectiveHandle = *regDown;
            effectiveDataset = down;
            // No per-renderer cache needed for downsampled path — direct handle used.
            handle = effectiveHandle;
        }
        if (handle.isNull()) {
            return data::makeError<void>(
                4, "VolumeRenderer: null volume handle (register via AssetRegistry)");
        }
        // Resolve effective handle (original or downsampled).
        auto resolveHandle = effectiveHandle.isNull() ? handle : effectiveHandle;
        auto texture = assets_->resolveVolume(resolveHandle);
        if (texture.failed()) {
            return data::makeError<void>(texture.error().code,
                                         texture.error().message);
        }
        core::Texture3D* texPtr = *texture;
        texPtr->bind(0u);

        const auto [boxMin, boxMax] = worldAabb(instance);
        const glm::mat4 invModel = glm::inverse(instance.model);
        const glm::vec3 size(static_cast<float>(effectiveDataset->sizeX()),
                             static_cast<float>(effectiveDataset->sizeY()),
                             static_cast<float>(effectiveDataset->sizeZ()));

        program->setUniformVec3("uBoxMin", boxMin);
        program->setUniformVec3("uBoxMax", boxMax);
        program->setUniformMat4("uInvModel", invModel);
        program->setUniformVec3("uSize", size);
        program->setUniformFloat("uStepLength", kDefaultStepLength);
        uploadTransferFunction(instance.transferFunction, program);

        auto draw =
            core::drawElements(*quadVao, kQuadTriangleIndices.size());
        if (draw.failed()) {
            return draw;
        }
    }
    return data::Result<void>(data::value);
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
