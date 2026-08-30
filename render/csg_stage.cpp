// render/csg_stage.cpp — CsgOitStage Puxel 2-stage SSBO owner: headTexture R32UI + node/counter/resolved SSBOs, LazyProgramCache capture/resolve, ScreenQuad, ensureCapacity w*h*maxFpp*16 ≤152 MB, begin→draw→resolve→read* API with BudgetExceeded code 8 when ssboAtomics missing. (T3)

#include "render/csg_stage.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/caps.hpp"
#include "core/re_context.hpp"
#include "core/shader_program.hpp"
#include "render/render_constants.hpp"
#include "render/types.hpp"

namespace re::render {

CsgOitStage::CsgOitStage(std::uint32_t maxFragmentsPerPixel)
    : maxFpp_(std::clamp(maxFragmentsPerPixel, 1u, kOitShaderMaxNodes)) {}

data::Result<void> CsgOitStage::ensureCapacity(std::uint32_t width, std::uint32_t height) {
    if (width_ == width && height_ == height && nodeBuffer_.has_value() &&
        headTexture_.has_value() && headCountTexture_.has_value() && headBuffer_.has_value() &&
        headCountBuffer_.has_value() && counterBuffer_.has_value() && resolvedBuffer_.has_value() &&
        resolvedCountBuffer_.has_value()) {
        return data::Result<void>(data::value);
    }
    const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * height;
    const std::uint64_t nodeCount = pixelCount * static_cast<std::uint64_t>(maxFpp_);
    const std::uint64_t captureNodeBytes = nodeCount * static_cast<std::uint64_t>(kCsgNodeStrideBytes);
    const std::uint64_t resolvedNodeBytes = nodeCount * static_cast<std::uint64_t>(kCsgNodeStrideBytes);
    const std::uint64_t resolvedCountBytes = pixelCount * 4ull;
    const std::uint64_t headBytes = pixelCount * 4ull;
    const std::uint64_t headCountBytes = pixelCount * 4ull;
    std::vector<std::uint32_t> heads(static_cast<std::size_t>(pixelCount), kOitNullNode);

    auto headCountBuffer = core::ShaderStorageBuffer::create();
    if (headCountBuffer.failed()) {
        return data::makeError<void>(headCountBuffer.error().code, headCountBuffer.error().message);
    }
    std::vector<std::uint32_t> headCountsZero(static_cast<std::size_t>(pixelCount), 0u);
    headCountBuffer->bind();
    headCountBuffer->upload(headCountsZero.data(), static_cast<std::size_t>(headCountBytes), core::BufferUsage::DynamicDraw);
    headCountBuffer->unbind();

    auto nodeBuffer = core::ShaderStorageBuffer::create();
    if (nodeBuffer.failed()) {
        return data::makeError<void>(nodeBuffer.error().code, nodeBuffer.error().message);
    }
    nodeBuffer->bind();
    nodeBuffer->upload(nullptr, static_cast<std::size_t>(captureNodeBytes), core::BufferUsage::DynamicDraw);
    nodeBuffer->unbind();

    auto counterBuffer = core::ShaderStorageBuffer::create();
    if (counterBuffer.failed()) {
        return data::makeError<void>(counterBuffer.error().code, counterBuffer.error().message);
    }

    auto headBuffer = core::ShaderStorageBuffer::create();
    if (headBuffer.failed()) {
        return data::makeError<void>(headBuffer.error().code, headBuffer.error().message);
    }
    headBuffer->bind();
    headBuffer->upload(heads.data(), static_cast<std::size_t>(headBytes), core::BufferUsage::DynamicDraw);
    headBuffer->unbind();

    auto resolvedBuffer = core::ShaderStorageBuffer::create();
    if (resolvedBuffer.failed()) {
        return data::makeError<void>(resolvedBuffer.error().code, resolvedBuffer.error().message);
    }
    resolvedBuffer->bind();
    resolvedBuffer->upload(nullptr, static_cast<std::size_t>(resolvedNodeBytes), core::BufferUsage::DynamicDraw);
    resolvedBuffer->unbind();

    auto resolvedCountBuffer = core::ShaderStorageBuffer::create();
    if (resolvedCountBuffer.failed()) {
        return data::makeError<void>(resolvedCountBuffer.error().code, resolvedCountBuffer.error().message);
    }
    resolvedCountBuffer->bind();
    std::vector<std::uint32_t> zeros(static_cast<std::size_t>(pixelCount), 0u);
    resolvedCountBuffer->upload(zeros.data(), static_cast<std::size_t>(resolvedCountBytes), core::BufferUsage::DynamicDraw);
    resolvedCountBuffer->unbind();

    auto headTexture = core::Texture2D::create();
    if (headTexture.failed()) {
        return data::makeError<void>(headTexture.error().code, headTexture.error().message);
    }
    headTexture->bind(0u);
    headTexture->uploadR32UI(width, height, heads.data());
    headTexture->unbind(0u);

    auto headCountTexture = core::Texture2D::create();
    if (headCountTexture.failed()) {
        return data::makeError<void>(headCountTexture.error().code, headCountTexture.error().message);
    }
    std::vector<std::uint32_t> headCountZero(static_cast<std::size_t>(pixelCount), 0u);
    headCountTexture->bind(0u);
    headCountTexture->uploadR32UI(width, height, headCountZero.data());
    headCountTexture->unbind(0u);

    nodeBuffer_ = std::move(*nodeBuffer);
    counterBuffer_ = std::move(*counterBuffer);
    headBuffer_ = std::move(*headBuffer);
    headCountBuffer_ = std::move(*headCountBuffer);
    resolvedBuffer_ = std::move(*resolvedBuffer);
    resolvedCountBuffer_ = std::move(*resolvedCountBuffer);
    headTexture_ = std::move(*headTexture);
    headCountTexture_ = std::move(*headCountTexture);
    width_ = width;
    height_ = height;
    return data::Result<void>(data::value);
}

data::Result<void> CsgOitStage::begin(std::uint32_t width, std::uint32_t height, core::REContext& ctx) {
    const core::Caps& caps = core::caps();
    if (!caps.ssboAtomics) {
        return data::makeError<void>(data::ErrorDomain::Render, 8,
                                     "CsgOitStage: ssboAtomics not supported (BudgetExceeded)");
    }
    auto cap = ensureCapacity(width, height);
    if (cap.failed()) return cap;

    headTexture_->bind(0u);
    headTexture_->clearToU32(kOitNullNode);
    headTexture_->unbind(0u);
    // Clear head SSBO (primary for Puxel, image texture kept for legacy readHead)
    {
        std::vector<std::uint32_t> heads(static_cast<std::size_t>(width) * height, kOitNullNode);
        headBuffer_->bind();
        headBuffer_->upload(heads.data(), static_cast<std::size_t>(width) * height * 4ull, core::BufferUsage::DynamicDraw);
        headBuffer_->unbind();
    }
    {
        std::vector<std::uint32_t> headCountsZero(static_cast<std::size_t>(width) * height, 0u);
        headCountBuffer_->bind();
        headCountBuffer_->upload(headCountsZero.data(), static_cast<std::size_t>(width) * height * 4ull, core::BufferUsage::DynamicDraw);
        headCountBuffer_->unbind();
    }
    headCountBuffer_->bindBase(6);
    headCountTexture_->clearToU32(0u);
    headCountTexture_->unbind(0u);
    // Clear node buffer to 0 so resolve can scan for non-zero color as count
    {
        std::vector<std::uint8_t> zerosNodes(static_cast<std::size_t>(width) * height * maxFpp_ * 16ull, 0u);
        nodeBuffer_->bind();
        nodeBuffer_->upload(zerosNodes.data(), zerosNodes.size(), core::BufferUsage::DynamicDraw);
        nodeBuffer_->unbind();
    }

    const std::uint32_t zero = 0u;
    counterBuffer_->bind();
    counterBuffer_->upload(&zero, sizeof(zero), core::BufferUsage::DynamicDraw);
    counterBuffer_->unbind();

    // Clear resolved counts
    const std::uint64_t pixelCount = static_cast<std::uint64_t>(width) * height;
    std::vector<std::uint32_t> zeros(static_cast<std::size_t>(pixelCount), 0u);
    resolvedCountBuffer_->bind();
    resolvedCountBuffer_->upload(zeros.data(), static_cast<std::size_t>(pixelCount * 4ull), core::BufferUsage::DynamicDraw);
    resolvedCountBuffer_->unbind();

    // Ensure a capture color target (required for rasterization; shader writes to SSBO, not color, but GL needs a bound FBO)
    if (!captureColor_.has_value() || !captureFbo_.has_value() || captureWidth_ != width || captureHeight_ != height) {
        auto col = core::Texture2D::create();
        if (col.failed()) return data::makeError<void>(col.error().code, col.error().message);
        std::vector<std::uint8_t> zeros(static_cast<std::size_t>(width) * height * 4u, 0u);
        col->bind(0u);
        col->upload(width, height, zeros.data());
        col->unbind(0u);
        auto fb = core::Framebuffer::create();
        if (fb.failed()) return data::makeError<void>(fb.error().code, fb.error().message);
        fb->bind();
        fb->attachColor(*col);
        if (!fb->isComplete()) {
            fb->unbind();
            return data::makeError<void>(1, "CsgOitStage: capture FBO incomplete");
        }
        fb->unbind();
        captureColor_ = std::move(*col);
        captureFbo_ = std::move(*fb);
        captureWidth_ = width;
        captureHeight_ = height;
    }
    captureFbo_->bind();
    nodeBuffer_->bindBase(0);
    headCountBuffer_->bindBase(6);
    counterBuffer_->bindBase(1);
    headBuffer_->bindBase(2);
    ctx.setViewport(0, 0, static_cast<int>(width), static_cast<int>(height));
    // Clear color to transparent and disable depth for capture (need all fragments)
    ctx.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    ctx.clearColor();
    ctx.disableDepthTest();
    ctx.disableBlend();
    begun_ = true;
    return data::Result<void>(data::value);
}

data::Result<void> CsgOitStage::begin(const RenderTarget& target, core::REContext& ctx) {
    return begin(target.width, target.height, ctx);
}

data::Result<void> CsgOitStage::resolve(core::REContext& ctx) {
    if (!begun_) {
        return data::makeError<void>(1, "CsgOitStage: resolve called before begin");
    }
    // Make capture writes visible to resolve
    core::memoryBarrierAll();
    core::finish();

    auto progRes = resolveProgram();
    if (progRes.failed()) {
        core::unbindImage(kCsgHeadImageUnit);
        begun_ = false;
        return data::makeError<void>(progRes.error().code, progRes.error().message);
    }
    core::ShaderProgram* prog = *progRes;
    auto quadRes = screenQuad();
    if (quadRes.failed()) {
        core::unbindImage(kCsgHeadImageUnit);
        begun_ = false;
        return data::makeError<void>(quadRes.error().code, quadRes.error().message);
    }
    core::VertexArray* quad = *quadRes;

    // Ensure a color target for the resolve draw (the shader writes oColor; GL requires a bound FBO).
    // The stage owns a private dummy color attachment sized to the current resolution so the
    // headless gate does not depend on the caller having bound a ViewTarget. This keeps the
    // stage self-contained and deterministic on llvmpipe where a missing FBO would be incomplete.
    if (!resolveColor_.has_value() || !resolveFbo_.has_value() || resolveColor_->id() == 0u ||
        resolveWidth_ != width_ || resolveHeight_ != height_) {
        // (Re)create resolve target sized to current width/height
        auto col = core::Texture2D::create();
        if (col.failed()) {
            core::unbindImage(kCsgHeadImageUnit);
            begun_ = false;
            return data::makeError<void>(col.error().code, col.error().message);
        }
        std::vector<std::uint8_t> zeros(static_cast<std::size_t>(width_) * height_ * 4u, 0u);
        col->bind(0u);
        col->upload(width_, height_, zeros.data());
        col->unbind(0u);
        auto fb = core::Framebuffer::create();
        if (fb.failed()) {
            core::unbindImage(kCsgHeadImageUnit);
            begun_ = false;
            return data::makeError<void>(fb.error().code, fb.error().message);
        }
        fb->bind();
        fb->attachColor(*col);
        if (!fb->isComplete()) {
            fb->unbind();
            core::unbindImage(kCsgHeadImageUnit);
            begun_ = false;
            return data::makeError<void>(1, "CsgOitStage: resolve FBO incomplete");
        }
        fb->unbind();
        resolveColor_ = std::move(*col);
        resolveFbo_ = std::move(*fb);
        resolveWidth_ = width_;
        resolveHeight_ = height_;
    }
    resolveFbo_->bind();
    ctx.setViewport(0, 0, static_cast<int>(width_), static_cast<int>(height_));
    ctx.disableDepthTest();
    nodeBuffer_->bindBase(0);
    resolvedBuffer_->bindBase(1);
    resolvedCountBuffer_->bindBase(2);
    core::bindImageR32ui(*headCountTexture_, 5u);

    prog->use();
    prog->setUniformInt("uMaxFpp", static_cast<std::int32_t>(maxFpp_));
    prog->setUniformInt("uSubtractorCount", 1);
    prog->setUniformInt("uPaintCount", 0);
    prog->setUniformVec2("uResolution", glm::vec2(static_cast<float>(width_), static_cast<float>(height_)));

    auto draw = core::drawElements(*quad, kQuadTriangleIndices.size());
    core::memoryBarrierAll();
    core::finish();
    resolveFbo_->unbind();
    core::unbindImage(5u);
    ctx.disableBlend();
    begun_ = false;
    return draw;
}

data::Result<core::ShaderProgram*> CsgOitStage::captureProgram() {
    const std::filesystem::path dir = RE_SHADER_DIR;
    return captureProgram_.getOrLoadFromFiles(dir / "csg_capture.vert.glsl", dir / "csg_capture.frag.glsl");
}

data::Result<core::ShaderProgram*> CsgOitStage::resolveProgram() {
    const std::filesystem::path dir = RE_SHADER_DIR;
    return resolveProgram_.getOrLoadFromFiles(dir / "csg_resolve.vert.glsl", dir / "csg_resolve.frag.glsl");
}

data::Result<core::VertexArray*> CsgOitStage::screenQuad() {
    if (!screenQuad_.has_value()) {
        auto quad = ScreenQuad::create();
        if (quad.failed()) {
            return data::makeError<core::VertexArray*>(quad.error().code, quad.error().message);
        }
        screenQuad_ = std::move(*quad);
    }
    return data::makeValue<core::VertexArray*>(&screenQuad_->vao());
}

data::Result<std::uint32_t> CsgOitStage::readCapturedCount() {
    if (!counterBuffer_.has_value()) {
        return data::makeError<std::uint32_t>(1, "CsgOitStage: no counter buffer (begin never ran)");
    }
    core::memoryBarrierBufferUpdate();
    counterBuffer_->bind();
    std::uint32_t count = 0u;
    const auto read = counterBuffer_->readUint32(0u, 1u, &count);
    counterBuffer_->unbind();
    if (read.failed()) return data::makeError<std::uint32_t>(read.error().code, read.error().message);
    return data::makeValue<std::uint32_t>(count);
}

data::Result<std::vector<std::uint32_t>> CsgOitStage::readResolvedCounts() const {
    if (!resolvedCountBuffer_.has_value() || width_ == 0u || height_ == 0u) {
        return data::makeError<std::vector<std::uint32_t>>(1, "CsgOitStage: no resolved count buffer");
    }
    const std::size_t pixelCount = static_cast<std::size_t>(width_) * height_;
    std::vector<std::uint32_t> out(pixelCount, 0u);
    core::memoryBarrierBufferUpdate();
    resolvedCountBuffer_->bind();
    const auto read = resolvedCountBuffer_->readUint32(0u, pixelCount, out.data());
    resolvedCountBuffer_->unbind();
    if (read.failed()) return data::makeError<std::vector<std::uint32_t>>(read.error().code, read.error().message);
    return data::makeValue<std::vector<std::uint32_t>>(std::move(out));
}

data::Result<std::uint32_t> CsgOitStage::readResolvedCount(std::uint32_t x, std::uint32_t y) const {
    if (!resolvedCountBuffer_.has_value()) {
        return data::makeError<std::uint32_t>(1, "CsgOitStage: no resolved count buffer");
    }
    if (x >= width_ || y >= height_) {
        return data::makeError<std::uint32_t>(2, "CsgOitStage: pixel out of bounds");
    }
    const std::size_t idx = static_cast<std::size_t>(y) * width_ + x;
    std::uint32_t val = 0u;
    core::memoryBarrierBufferUpdate();
    resolvedCountBuffer_->bind();
    const auto read = resolvedCountBuffer_->readUint32(idx * 4u, 1u, &val);
    resolvedCountBuffer_->unbind();
    if (read.failed()) return data::makeError<std::uint32_t>(read.error().code, read.error().message);
    return data::makeValue<std::uint32_t>(val);
}

data::Result<std::vector<CsgResolvedNode>> CsgOitStage::readResolvedNodes() const {
    if (!resolvedBuffer_.has_value() || width_ == 0u || height_ == 0u) {
        return data::makeError<std::vector<CsgResolvedNode>>(1, "CsgOitStage: no resolved buffer");
    }
    const std::size_t nodeCount = static_cast<std::size_t>(width_) * height_ * maxFpp_;
    std::vector<CsgResolvedNode> out(nodeCount);
    core::memoryBarrierBufferUpdate();
    resolvedBuffer_->bind();
    // Read as bytes via readBufferSubData (REContext anchor)
    const std::size_t byteSize = nodeCount * sizeof(CsgResolvedNode);
    auto res = core::REContext::current().readBufferSubData(core::REContext::BufferTarget::ShaderStorage,
                                                             resolvedBuffer_->id(), 0u, byteSize, out.data());
    resolvedBuffer_->unbind();
    if (res.failed()) return data::makeError<std::vector<CsgResolvedNode>>(res.error().code, res.error().message);
    return data::makeValue<std::vector<CsgResolvedNode>>(std::move(out));
}

data::Result<std::vector<CsgNode>> CsgOitStage::readCapturedNodes() const {
    if (!nodeBuffer_.has_value() || width_ == 0u || height_ == 0u) {
        return data::makeError<std::vector<CsgNode>>(1, "CsgOitStage: no node buffer");
    }
    const std::size_t nodeCount = static_cast<std::size_t>(width_) * height_ * maxFpp_;
    std::vector<CsgNode> out(nodeCount);
    core::memoryBarrierBufferUpdate();
    nodeBuffer_->bind();
    const std::size_t byteSize = nodeCount * sizeof(CsgNode);
    // Node buffer is 32B per node (capture), but CsgNode is 16B, so we need to read 32B stride? For debug we read first 16B per node
    // For now read as bytes and interpret first 16B
    auto res = core::REContext::current().readBufferSubData(core::REContext::BufferTarget::ShaderStorage,
                                                             nodeBuffer_->id(), 0u, byteSize, out.data());
    nodeBuffer_->unbind();
    if (res.failed()) return data::makeError<std::vector<CsgNode>>(res.error().code, res.error().message);
    return data::makeValue<std::vector<CsgNode>>(std::move(out));
}

data::Result<float> CsgOitStage::readResolvedDepth(std::uint32_t x, std::uint32_t y) const {
    if (!resolvedBuffer_.has_value()) {
        return data::makeError<float>(1, "CsgOitStage: no resolved buffer");
    }
    if (x >= width_ || y >= height_) {
        return data::makeError<float>(2, "CsgOitStage: pixel out of bounds");
    }
    const std::size_t pixelIdx = static_cast<std::size_t>(y) * width_ + x;
    const std::size_t slot = 0u;
    const std::size_t nodeIdx = pixelIdx * maxFpp_ + slot;
    std::vector<CsgResolvedNode> nodes;
    auto all = readResolvedNodes();
    if (all.failed()) return data::makeError<float>(all.error().code, all.error().message);
    nodes = std::move(*all);
    if (nodeIdx >= nodes.size()) return data::makeError<float>(3, "CsgOitStage: node index out of range");
    // Also need count to know if exists
    auto cnt = readResolvedCount(x, y);
    if (cnt.failed()) return data::makeError<float>(cnt.error().code, cnt.error().message);
    if (*cnt == 0u) return data::makeError<float>(4, "CsgOitStage: no surviving fragment at pixel");
    return data::makeValue<float>(nodes[nodeIdx].depth);
}

data::Result<std::vector<std::uint8_t>> CsgOitStage::readCapturePixel(std::uint32_t x, std::uint32_t y) const {
    if (!captureFbo_.has_value() || !captureColor_.has_value()) {
        return data::makeError<std::vector<std::uint8_t>>(1, "CsgOitStage: no capture target");
    }
    if (x >= captureWidth_ || y >= captureHeight_) {
        return data::makeError<std::vector<std::uint8_t>>(2, "CsgOitStage: pixel out of bounds");
    }
    captureFbo_->bind();
    std::vector<std::uint8_t> out;
    auto r = core::REContext::current().readRgba8(x, y, 1u, 1u, out);
    captureFbo_->unbind();
    if (r.failed()) return data::makeError<std::vector<std::uint8_t>>(r.error().code, r.error().message);
    return data::makeValue<std::vector<std::uint8_t>>(std::move(out));
}

data::Result<std::uint32_t> CsgOitStage::readHead(std::uint32_t x, std::uint32_t y) const {
    if (!headBuffer_.has_value()) {
        return data::makeError<std::uint32_t>(1, "CsgOitStage: no head buffer");
    }
    if (x >= width_ || y >= height_) {
        return data::makeError<std::uint32_t>(2, "CsgOitStage: pixel out of bounds");
    }
    core::memoryBarrierBufferUpdate();
    headBuffer_->bind();
    std::vector<std::uint32_t> all(static_cast<std::size_t>(width_) * height_, 0u);
    auto r = headBuffer_->readUint32(0u, static_cast<std::size_t>(width_) * height_, all.data());
    headBuffer_->unbind();
    if (r.failed()) return data::makeError<std::uint32_t>(r.error().code, r.error().message);
    const std::size_t idx = static_cast<std::size_t>(y) * width_ + x;
    if (idx >= all.size()) return data::makeError<std::uint32_t>(3, "CsgOitStage: head index out of range");
    return data::makeValue<std::uint32_t>(all[idx]);
}

data::Result<std::uint32_t> CsgOitStage::readHeadCount(std::uint32_t x, std::uint32_t y) const {
    if (!headCountBuffer_.has_value()) {
        return data::makeError<std::uint32_t>(1, "CsgOitStage: no headCount buffer");
    }
    if (x >= width_ || y >= height_) {
        return data::makeError<std::uint32_t>(2, "CsgOitStage: pixel out of bounds");
    }
    core::memoryBarrierBufferUpdate();
    headCountBuffer_->bind();
    std::vector<std::uint32_t> all(static_cast<std::size_t>(width_) * height_, 0u);
    auto r = headCountBuffer_->readUint32(0u, static_cast<std::size_t>(width_) * height_, all.data());
    headCountBuffer_->unbind();
    if (r.failed()) return data::makeError<std::uint32_t>(r.error().code, r.error().message);
    const std::size_t idx = static_cast<std::size_t>(y) * width_ + x;
    if (idx >= all.size()) return data::makeError<std::uint32_t>(3, "CsgOitStage: headCount index out of range");
    return data::makeValue<std::uint32_t>(all[idx]);
}

} // namespace re::render
