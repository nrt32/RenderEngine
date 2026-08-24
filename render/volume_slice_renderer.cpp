// render/volume_slice_renderer.cpp — VolumeSliceRenderer implementation: GPU
// volume-plane extraction through one full-screen quad per instance. The
// fragment shader (volume_slice.frag.glsl) does the per-pixel work — ray
// reconstruction, plane intersection, model-space conversion, trilinear 3D
// texture fetch, transfer-function evaluation — so this file only wires
// uniforms, textures, and the draw call. The extraction consumes the SAME
// shared-store texture path as the ray-cast VolumeRenderer (one R32F Texture3D
// per dataset content), and a slice-index change is purely a uniform change:
// no CPU voxel loop and no intermediate image exists on this path.

#include "render/volume_slice_renderer.hpp"

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/draw.hpp"
#include "core/shader_program.hpp"
#include "volume/color.hpp"

namespace re::render {

VolumeSliceRenderer::VolumeSliceRenderer(std::shared_ptr<AssetRegistry> assets)
    : assets_(std::move(assets)) {}

// The vertex stage is shared with the ray-cast program
// (volume_raycast.vert.glsl emits the same vNdc passthrough), so only the
// fragment stage is specific to plane extraction. The full-screen quad comes
// from the shared ScreenQuad provider (render/screen_quad.*): one NDC vertex
// table + build sequence for every whole-viewport technique.

data::Result<core::ShaderProgram*> VolumeSliceRenderer::sliceProgram() {
    if (program_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*program_);
    }
    const std::filesystem::path dir = RE_SHADER_DIR;
    auto program = core::ShaderProgram::createFromFiles(
        dir / "volume_raycast.vert.glsl", dir / "volume_slice.frag.glsl");
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    program_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*program_);
}

data::Result<core::VertexArray*> VolumeSliceRenderer::screenQuad() {
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

data::Result<core::Texture3D*> VolumeSliceRenderer::textureFor(
    const std::shared_ptr<const data::VolumeDataset>& dataset) {
    // The shared asset store dedups by content hash: identical voxel content
    // — even through a second renderer instance or a distinct allocation —
    // resolves to ONE store-owned GL texture, shared with the ray-cast path.
    // The lazy lookup never changes reference counts; owners manage explicit
    // lifetimes via registerVolume/unregisterVolume.
    return assets_->lookupVolume(dataset);
}

void VolumeSliceRenderer::uploadTransferFunction(
    const volume::TransferFunction& tf) const {
    const std::size_t count = tf.size();
    std::vector<float> values(count);
    std::vector<glm::vec4> colors(count);
    for (std::size_t i = 0u; i < count; ++i) {
        const auto& cp = tf.controlPoints()[i];
        values[i] = cp.value;
        colors[i] = glm::vec4(cp.color.r, cp.color.g, cp.color.b, cp.color.a);
    }
    program_->setUniformInt("uTfCount", static_cast<std::int32_t>(count));
    program_->setUniformFloatArray("uTfValues", values.data(), count);
    program_->setUniformVec4Array("uTfColors", colors.data(), count);
}

data::Result<void> VolumeSliceRenderer::drawOne(
    const VolumeSliceInstance& instance, const Camera& camera,
    core::ShaderProgram* program) {
    auto texture = textureFor(instance.dataset);
    if (texture.failed()) {
        return data::makeError<void>(texture.error().code,
                                     texture.error().message);
    }
    core::Texture3D* texPtr = *texture;
    texPtr->bind(0u);

    const glm::mat4 invModel = glm::inverse(instance.model);
    const glm::vec3 size(static_cast<float>(instance.dataset->sizeX()),
                         static_cast<float>(instance.dataset->sizeY()),
                         static_cast<float>(instance.dataset->sizeZ()));

    program->setUniformMat4("uViewProj", camera.proj * camera.view);
    program->setUniformMat4("uInvModel", invModel);
    program->setUniformVec3("uSize", size);
    program->setUniformVec3("uPlaneNormal", instance.plane.normal);
    program->setUniformVec3("uPlanePoint", instance.plane.point);
    program->setUniformInt("uVolume", 0); // sampler reads texture unit 0
    uploadTransferFunction(instance.transferFunction);

    auto draw = core::drawElements(screenQuad_->vao(),
                                   kQuadTriangleIndices.size());
    if (draw.failed()) {
        return draw;
    }
    return data::Result<void>(data::value);
}

data::Result<void> VolumeSliceRenderer::render(const VolumeSliceScene& scene,
                                               const Camera& camera,
                                               const RenderTarget& target) {
    if (assets_ == nullptr) {
        // Constructed with a null store (member-init-order safety): fail with
        // a typed error instead of dereferencing.
        return data::makeError<void>(4,
                                     "VolumeSliceRenderer: no shared asset "
                                     "store");
    }
    if (target.width == 0u || target.height == 0u) {
        return data::makeError<void>(1,
                                     "VolumeSliceRenderer: invalid target "
                                     "size");
    }

    for (const VolumeSliceInstance& instance : scene.slices) {
        if (!instance.dataset) {
            // A null dataset would silently render an empty layer — visually
            // indistinguishable from an empty viewport — so it is rejected
            // instead of skipped (typed error code 1, SPEC-style no-crash
            // diagnostics).
            return data::makeError<void>(
                1, "VolumeSliceRenderer: null dataset in slice instance");
        }
        if (instance.transferFunction.size() > kMaxVolumeSliceTfPoints) {
            return data::makeError<void>(
                1, "VolumeSliceRenderer: transfer function has more than " +
                       std::to_string(kMaxVolumeSliceTfPoints) +
                       " control points");
        }
    }

    auto programResult = sliceProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;

    // Ensure the shared full-screen quad exists; drawOne issues the indexed
    // draw through the renderer-owned vertex array.
    auto quadResult = screenQuad();
    if (quadResult.failed()) {
        return data::makeError<void>(quadResult.error().code,
                                     quadResult.error().message);
    }

    // Begin the pass through the ONE shared prologue (bind target → viewport
    // → clear → depth state → blend off). A null framebuffer selects the
    // window's on-screen default framebuffer; otherwise the offscreen FBO is
    // bound. Direct single-scene renders keep the deterministic depth-off
    // painter's-order pass (a target's optional depth attachment is consumed
    // only via the per-view opt-in), so the depth test stays off; blending
    // stays off because the shader already writes the final straight-RGBA
    // slice color (and transparent black where the plane misses the volume).
    core::DrawContext ctx;
    ctx.beginPass(target.framebuffer, target.width, target.height,
                  target.clearColor.r, target.clearColor.g,
                  target.clearColor.b, target.clearColor.a);

    program->use();
    for (const VolumeSliceInstance& instance : scene.slices) {
        auto drawn = drawOne(instance, camera, program);
        if (drawn.failed()) {
            return drawn;
        }
    }
    return data::Result<void>(data::value);
}

data::Result<void> VolumeSliceRenderer::drawLayer(const VolumeSliceScene& scene,
                                                  const Camera& camera,
                                                  core::DrawContext& ctx) {
    // ReView already bind+viewport+clear via ctx; layers must not clear
    // between each other, so the context is intentionally untouched here.
    (void)ctx;
    if (assets_ == nullptr) {
        return data::makeError<void>(4,
                                     "VolumeSliceRenderer: no shared asset "
                                     "store");
    }
    for (const VolumeSliceInstance& instance : scene.slices) {
        if (!instance.dataset) {
            return data::makeError<void>(
                1, "VolumeSliceRenderer: null dataset in slice instance");
        }
        if (instance.transferFunction.size() > kMaxVolumeSliceTfPoints) {
            return data::makeError<void>(
                1, "VolumeSliceRenderer: transfer function has more than " +
                       std::to_string(kMaxVolumeSliceTfPoints) +
                       " control points");
        }
    }

    auto programResult = sliceProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;

    // Ensure the shared full-screen quad exists; drawOne issues the indexed
    // draw through the renderer-owned vertex array.
    auto quadResult = screenQuad();
    if (quadResult.failed()) {
        return data::makeError<void>(quadResult.error().code,
                                     quadResult.error().message);
    }

    program->use();
    for (const VolumeSliceInstance& instance : scene.slices) {
        auto drawn = drawOne(instance, camera, program);
        if (drawn.failed()) {
            return drawn;
        }
    }
    return data::Result<void>(data::value);
}

} // namespace re::render
