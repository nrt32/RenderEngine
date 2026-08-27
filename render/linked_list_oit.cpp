// render/linked_list_oit.cpp — order-independent transparency via per-pixel
// linked lists: transparent fragments are captured (color + depth) into a GPU
// buffer during the draw pass, then composited back-to-front in a full-screen
// pass. Order independence is what makes overlapping transparent surfaces
// produce the same pixels regardless of draw order; opaque geometry is
// unaffected because it renders before capture and depth-blocks fragments.

#include "render/linked_list_oit.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <string>
#include <vector>

#include "core/re_context.hpp"
#include "core/shader_program.hpp"
#include "render/mesh_geometry.hpp"
#include "render/types.hpp" // render::Camera / render::RenderTarget

namespace re::render {

// Shaders live as .glsl files under render/shaders/ (SPEC §9 V2.6) and are
// loaded via core::ShaderProgram's file helpers. See docs/render.md and the
// .glsl files themselves for the GLSL source.

// Maximum nodes the composite shader sorts per pixel (its fixed local array
// size). The constructor enforces maxFragmentsPerPixel_ <= this.
constexpr std::uint32_t kShaderMaxNodes = 16u;

// SSBO binding points (fixed across capture + composite passes).
constexpr std::uint32_t kNodeBinding = 0u;    // layout(std430, binding=0)
constexpr std::uint32_t kCounterBinding = 1u; // layout(std430, binding=1)
constexpr std::uint32_t kHeadImageUnit = 2u;  // layout(r32ui, binding=2)

// A "null" head-pointer sentinel: no node. Cleared head pointers are set to
// this value so an empty pixel's linked list terminates immediately.
constexpr std::uint32_t kNullNode = 0xFFFFFFFFu;

// The full-screen composite quad comes from the shared ScreenQuad provider
// (render/screen_quad.*): one NDC vertex table + build sequence for every
// whole-viewport technique (previously this pipeline carried its own
// byte-identical copy).

LinkedListOIT::LinkedListOIT(std::uint32_t maxFragmentsPerPixel)
    : maxFragmentsPerPixel_(
          std::clamp(maxFragmentsPerPixel, 1u, kShaderMaxNodes)) {}

bool LinkedListOIT::isEngaged() const noexcept {
    return engaged_;
}

data::Result<std::uint32_t> LinkedListOIT::readCapturedFragmentCount() {
    if (!counterBuffer_.has_value()) {
        return data::makeError<std::uint32_t>(
            1, "LinkedListOIT: no counter buffer (begin() never ran)");
    }
    // Make the capture pass's atomic counter writes visible to this client
    // readback, per the GL memory-barrier rules (the raw call lives in
    // core/storage_buffer.cpp via readUint32; guardrail no_production_readback).
    core::memoryBarrierBufferUpdate();
    counterBuffer_->bind();
    std::uint32_t count = 0u;
    const data::Result<void> read = counterBuffer_->readUint32(0u, 1u, &count);
    counterBuffer_->unbind();
    if (read.failed()) {
        return data::makeError<std::uint32_t>(read.error().code,
                                              read.error().message);
    }
    return data::makeValue<std::uint32_t>(count);
}

data::Result<core::ShaderProgram*> LinkedListOIT::captureProgram() {
    if (captureProgram_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*captureProgram_);
    }
    const std::filesystem::path dir = RE_SHADER_DIR;
    auto program = core::ShaderProgram::createFromFiles(
        dir / "oit_capture.vert.glsl", dir / "oit_capture.frag.glsl");
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    captureProgram_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*captureProgram_);
}

data::Result<core::ShaderProgram*> LinkedListOIT::compositeProgram() {
    if (compositeProgram_.has_value()) {
        return data::makeValue<core::ShaderProgram*>(&*compositeProgram_);
    }
    const std::filesystem::path dir = RE_SHADER_DIR;
    auto program = core::ShaderProgram::createFromFiles(
        dir / "oit_composite.vert.glsl", dir / "oit_composite.frag.glsl");
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    compositeProgram_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*compositeProgram_);
}

data::Result<core::VertexArray*> LinkedListOIT::screenQuad() {
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

data::Result<void> LinkedListOIT::ensureCapacity(std::uint32_t width,
                                                 std::uint32_t height) {
    const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * height;
    if (width_ == width && height_ == height && nodeBuffer_.has_value() &&
        headTexture_.has_value() && counterBuffer_.has_value()) {
        return data::Result<void>(data::value);
    }

    // Node buffer: one node per (pixel, slot). Each OITNode is 32 bytes in
    // std430 (vec4 16 + float 4 + uint 4, rounded to vec4 alignment 16).
    const std::uint64_t nodeBytes =
        pixelCount * static_cast<std::uint64_t>(maxFragmentsPerPixel_) * 32u;
    auto nodeBuffer = core::ShaderStorageBuffer::create();
    if (nodeBuffer.failed()) {
        return data::makeError<void>(nodeBuffer.error().code,
                                     nodeBuffer.error().message);
    }
    nodeBuffer->bind();
    nodeBuffer->upload(nullptr, static_cast<std::size_t>(nodeBytes),
                       core::BufferUsage::DynamicDraw);
    nodeBuffer->unbind();

    auto counterBuffer = core::ShaderStorageBuffer::create();
    if (counterBuffer.failed()) {
        return data::makeError<void>(counterBuffer.error().code,
                                     counterBuffer.error().message);
    }

    // Head-pointer texture: R32UI, one head per pixel.
    auto headTexture = core::Texture2D::create();
    if (headTexture.failed()) {
        return data::makeError<void>(headTexture.error().code,
                                     headTexture.error().message);
    }
    std::vector<std::uint32_t> heads(static_cast<std::size_t>(pixelCount),
                                     kNullNode);
    headTexture->bind(0u);
    headTexture->uploadR32UI(width, height, heads.data());
    headTexture->unbind(0u);

    nodeBuffer_ = std::move(*nodeBuffer);
    counterBuffer_ = std::move(*counterBuffer);
    headTexture_ = std::move(*headTexture);
    width_ = width;
    height_ = height;
    return data::Result<void>(data::value);
}

data::Result<void> LinkedListOIT::begin(const Camera& camera,
                                        const RenderTarget& target,
                                        core::REContext& ctx) {
    (void)camera;
    // (Re)allocate storage if the target size changed. A failure is reported
    // as a typed error (SPEC §5): the pipeline stays un-engaged and the
    // transparent-capable mesh pass is aborted — no silent blend fallback
    // (the target is left cleared and the typed error is surfaced via the
    // bridge, SPEC §5). Unsupported or over-budget hardware therefore yields
    // opaque-only rendering for that pass.
    const data::Result<void> capacity = ensureCapacity(target.width, target.height);
    if (capacity.failed()) {
        engaged_ = false;
        return capacity;
    }

    // Reset the head pointers to the null sentinel and the allocator to 0. The
    // node buffer contents are overwritten by the GPU and need no reset.
    headTexture_->bind(0u);
    headTexture_->clearToU32(kNullNode);
    headTexture_->unbind(0u);

    const std::uint32_t zero = 0u;
    counterBuffer_->bind();
    counterBuffer_->upload(&zero, sizeof(zero), core::BufferUsage::DynamicDraw);
    counterBuffer_->unbind();

    // Bind the head-pointer image + SSBOs to their fixed binding points for the
    // capture + composite passes, and install the viewport for the frame.
    // T4: single ledger via REContext& from ViewCompositor — OIT + View prologues
    // share one viewport/depth/blend cache, so 2 layers sharing viewport issue
    // exactly 1 glViewport (no skipped-glEnable bugs).
    core::bindImageR32ui(*headTexture_, kHeadImageUnit);
    nodeBuffer_->bindBase(kNodeBinding);
    counterBuffer_->bindBase(kCounterBinding);
    ctx.setViewport(0, 0, static_cast<int>(target.width),
                   static_cast<int>(target.height));

    engaged_ = true;
    return data::Result<void>(data::value);
}

data::Result<void> LinkedListOIT::drawTransparent(const MeshGeometry& geometry,
                                                  const glm::vec4& baseColor,
                                                  const glm::mat4& model,
                                                  const Camera& camera) {
    if (!engaged_) {
        return data::Result<void>(data::value);
    }
    auto programResult = captureProgram();
    if (programResult.failed()) {
        engaged_ = false;
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;
    program->use();
    program->setUniformMat4("uModel", model);
    program->setUniformMat4("uView", camera.view);
    program->setUniformMat4("uProj", camera.proj);
    program->setUniformVec4("uBaseColor", baseColor);
    program->setUniformInt("uCapacity",
                           static_cast<std::int32_t>(nodeCapacity()));

    const auto draw = geometry.draw();
    if (draw.failed()) {
        engaged_ = false;
        return draw;
    }

    // Make this draw's atomic counter/head-pointer writes visible to the next
    // capture draw (llvmpipe requires an explicit barrier between draws that
    // perform atomic SSBO operations; see docs/render.md).
    core::memoryBarrierShaderStorage();
    return data::Result<void>(data::value);
}

data::Result<void> LinkedListOIT::end(const Camera& camera,
                                      const RenderTarget& target,
                                      core::REContext& ctx) {
    (void)camera;
    if (!engaged_) {
        return data::Result<void>(data::value);
    }

    // Make the capture pass's SSBO writes visible to the composite pass.
    core::memoryBarrierShaderStorage();

    auto programResult = compositeProgram();
    if (programResult.failed()) {
        // Restore draw state before reporting the error (no state leak).
        core::unbindImage(kHeadImageUnit);
        engaged_ = false;
        return data::makeError<void>(programResult.error().code,
                                     programResult.error().message);
    }
    core::ShaderProgram* program = *programResult;
    auto quadResult = screenQuad();
    if (quadResult.failed()) {
        core::unbindImage(kHeadImageUnit);
        engaged_ = false;
        return data::makeError<void>(quadResult.error().code,
                                     quadResult.error().message);
    }
    core::VertexArray* quadVao = *quadResult;

    // Bind the target and composite over its current (opaque) contents. A null
    // framebuffer means the window's on-screen default framebuffer (T12);
    // otherwise bind the offscreen FBO. This sequence deliberately does NOT go
    // through the shared beginPass prologue: compositing must blend OVER the
    // already-drawn opaque contents, so nothing may be cleared here — only the
    // narrower bind + viewport + depth-off + blend-on sequence is issued.
    // T4: uses explicit REContext& ledger — single writer for viewport/depth/blend.
    if (target.framebuffer == nullptr) {
        core::bindDefaultFramebuffer();
    } else {
        target.framebuffer->bind();
    }
    ctx.setViewport(0, 0, static_cast<int>(target.width),
                   static_cast<int>(target.height));
    ctx.disableDepthTest();

    // The composite shader outputs the accumulated premultiplied transparent
    // color; blend it over the opaque contents with (ONE, ONE_MINUS_SRC_ALPHA).
    ctx.enablePremultipliedOverBlend();

    program->use();
    program->setUniformInt("uMaxNodes",
                           static_cast<std::int32_t>(maxFragmentsPerPixel_));
    const data::Result<void> draw =
        core::drawElements(*quadVao, kQuadTriangleIndices.size());

    // Restore draw state regardless of the draw result (no state leak).
    ctx.disableBlend();
    core::unbindImage(kHeadImageUnit);
    engaged_ = false;
    return draw;
}

} // namespace re::render
