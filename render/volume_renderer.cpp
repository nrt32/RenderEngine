// render/volume_renderer.cpp — VolumeRenderer implementation (SPEC §3,
// FR-render.6).

#include "render/volume_renderer.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "core/draw.hpp"
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

// Full-screen quad vertices in NDC (x,y): two triangles sharing a diagonal.
constexpr std::array<float, 8> kScreenQuadVerts = {
    -1.0f, -1.0f, // corner 0
    1.0f,  -1.0f, // corner 1
    1.0f,  1.0f,  // corner 2
    -1.0f, 1.0f,  // corner 3
};

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
    if (screenQuadVao_.has_value()) {
        return data::makeValue<core::VertexArray*>(&*screenQuadVao_);
    }
    auto vao = core::VertexArray::create();
    if (vao.failed()) {
        return data::makeError<core::VertexArray*>(vao.error().code,
                                                   vao.error().message);
    }
    auto vbo = core::VertexBuffer::create();
    if (vbo.failed()) {
        return data::makeError<core::VertexArray*>(vbo.error().code,
                                                   vbo.error().message);
    }
    auto ebo = core::ElementBuffer::create();
    if (ebo.failed()) {
        return data::makeError<core::VertexArray*>(ebo.error().code,
                                                   ebo.error().message);
    }

    constexpr std::array<std::uint32_t, 6> kIndices = {0u, 1u, 2u, 0u, 2u, 3u};
    const std::size_t strideBytes = 2u * sizeof(float);

    // The VAO captures both the GL_ARRAY_BUFFER (via setAttribute) and the
    // GL_ELEMENT_ARRAY_BUFFER (via EBO bind) bindings, so both must be bound
    // while the VAO is bound (mirrors PlaneRenderer::quadGeometry). The EBO
    // must outlive the local scope because the VAO references it by name.
    vao->bind();
    vbo->bind();
    vbo->upload(kScreenQuadVerts.data(),
                kScreenQuadVerts.size() * sizeof(float),
                core::BufferUsage::StaticDraw);
    ebo->bind();
    ebo->upload(kIndices.data(), kIndices.size(),
                core::BufferUsage::StaticDraw);
    // Interleaved position-only layout: attribute 0 = 2 floats.
    vao->setAttribute(0u, 2, /*normalized=*/false, strideBytes, 0u);
    vao->unbind();

    screenQuadVbo_ = std::move(*vbo);
    screenQuadEbo_ = std::move(*ebo);
    screenQuadVao_ = std::move(*vao);
    screenQuadIndexCount_ = kIndices.size();
    return data::makeValue<core::VertexArray*>(&*screenQuadVao_);
}

data::Result<core::Texture3D*> VolumeRenderer::textureFor(
    const std::shared_ptr<const data::VolumeDataset>& dataset) {
    // The shared asset store dedups by content hash (T14): identical voxel
    // content — even through a second renderer instance or a distinct
    // allocation — resolves to ONE store-owned GL texture. The lazy lookup
    // never changes reference counts; owners manage explicit lifetimes via
    // registerVolume/unregisterVolume.
    return assets_->lookupVolume(dataset);
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

    // Bind the target and prepare draw state. A null framebuffer means the
    // window's on-screen default framebuffer (T12); otherwise bind the
    // offscreen FBO. v1 FBOs are color-only (no depth attachment), so the depth
    // test is left off; blending is off because the shader already writes the
    // final premultiplied composited color.
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
    program->setUniformMat4("uViewProj", camera.proj * camera.view);
    program->setUniformInt("uVolume", 0); // sampler reads texture unit 0

    for (const VolumeInstance& instance : scene.volumes) {
        if (!instance.dataset) {
            continue;
        }
        if (instance.transferFunction.size() > kMaxTfPoints) {
            return data::makeError<void>(
                1, "VolumeRenderer: transfer function has more than " +
                       std::to_string(kMaxTfPoints) + " control points");
        }

        auto texture = textureFor(instance.dataset);
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

        auto draw = core::drawElements(*quadVao, screenQuadIndexCount_);
        if (draw.failed()) {
            return draw;
        }
    }
    return data::Result<void>(data::value);
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

data::Result<void> VolumeRenderer::drawLayer(const VolumeScene& scene, const Camera& camera,
                                             core::DrawContext& ctx) {
    // ReView already bind+viewport+clear via ctx; does not clear between layers.
    (void)ctx;
    if (assets_ == nullptr) {
        return data::makeError<void>(4, "VolumeRenderer: no shared asset store");
    }
    auto programResult = rayCastProgram();
    if (programResult.failed()) {
        return data::makeError<void>(programResult.error().code, programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;
    auto quadResult = screenQuad();
    if (quadResult.failed()) {
        return data::makeError<void>(quadResult.error().code, quadResult.error().message);
    }
    core::VertexArray* quadVao = *quadResult;
    program->use();
    program->setUniformMat4("uViewProj", camera.proj * camera.view);
    program->setUniformInt("uVolume", 0);
    for (const VolumeInstance& instance : scene.volumes) {
        if (!instance.dataset) {
            continue;
        }
        if (instance.transferFunction.size() > kMaxTfPoints) {
            return data::makeError<void>(1, "VolumeRenderer: transfer function has more than " + std::to_string(kMaxTfPoints) + " control points");
        }
        auto texture = textureFor(instance.dataset);
        if (texture.failed()) {
            return data::makeError<void>(texture.error().code, texture.error().message);
        }
        core::Texture3D* texPtr = *texture;
        texPtr->bind(0u);
        const auto [boxMin, boxMax] = worldAabb(instance);
        const glm::mat4 invModel = glm::inverse(instance.model);
        const glm::vec3 size(static_cast<float>(instance.dataset->sizeX()), static_cast<float>(instance.dataset->sizeY()), static_cast<float>(instance.dataset->sizeZ()));
        program->setUniformVec3("uBoxMin", boxMin);
        program->setUniformVec3("uBoxMax", boxMax);
        program->setUniformMat4("uInvModel", invModel);
        program->setUniformVec3("uSize", size);
        program->setUniformFloat("uStepLength", kDefaultStepLength);
        uploadTransferFunction(instance.transferFunction);
        auto draw = core::drawElements(*quadVao, screenQuadIndexCount_);
        if (draw.failed()) {
            return draw;
        }
    }
    return data::Result<void>(data::value);
}

} // namespace re::render
