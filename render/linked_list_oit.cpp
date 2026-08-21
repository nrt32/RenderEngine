// render/linked_list_oit.cpp — LinkedListOIT implementation (SPEC §3,
// FR-render.2/3).

#include "render/linked_list_oit.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <string>
#include <vector>

#include "core/draw.hpp"
#include "render/mesh_geometry.hpp"
#include "render/mesh_renderer.hpp" // render::Camera / render::RenderTarget

namespace re::render {

namespace {

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

// Capture vertex shader: transforms the mesh's position (attribute 0) by
// model/view/proj. The capture pass installs this program and draws each
// transparent mesh's MeshGeometry (which declares attribute 0 = position).
constexpr char kCaptureVertexShader[] =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "uniform mat4 uModel;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProj;\n"
    "void main() {\n"
    "    gl_Position = uProj * uView * uModel * vec4(aPos, 1.0);\n"
    "}\n";

// Capture fragment shader: premultiplies the straight RGBA base color,
// atomically allocates a node, links it into the pixel's head pointer
// (imageAtomicExchange on the R32UI head-pointer image), and stores the node
// into the node buffer. It writes NO color output, so the target framebuffer's
// (opaque) contents are preserved. The capture shader writes via
// imageAtomicExchange + SSBO atomics (both supported by the gate's llvmpipe
// driver); see docs/render.md. `uCapacity` bounds the node allocator so a scene
// that exceeds the pipeline's per-pixel fragment budget cannot write out of
// bounds (the fragment is dropped instead).
constexpr char kCaptureFragmentShader[] =
    "#version 450 core\n"
    "struct OITNode {\n"
    "    vec4 color;\n" // premultiplied rgba
    "    float depth;\n"
    "    uint next;\n"
    "};\n"
    "layout(std430, binding = 0) coherent buffer NodeBuffer { OITNode nodes[]; "
    "};\n"
    "layout(std430, binding = 1) coherent buffer CounterBuffer { uint counter; "
    "};\n"
    "layout(r32ui, binding = 2) uniform coherent uimage2D uHead;\n"
    "uniform vec4 uBaseColor;\n" // straight (non-premultiplied) RGBA
    "uniform int uCapacity;\n"
    "void main() {\n"
    "    uint idx = atomicAdd(counter, 1u);\n"
    "    if (idx >= uint(uCapacity)) {\n"
    "        return;\n" // node budget exhausted: drop the fragment
    "    }\n"
    "    uint prev = imageAtomicExchange(uHead, ivec2(gl_FragCoord.xy), idx);\n"
    "    vec4 c = vec4(uBaseColor.rgb * uBaseColor.a, uBaseColor.a);\n"
    "    nodes[idx].color = c;\n"
    "    nodes[idx].depth = gl_FragCoord.z;\n"
    "    nodes[idx].next = prev;\n"
    "}\n";

// Composite vertex shader: full-screen quad in NDC.
constexpr char kCompositeVertexShader[] =
    "#version 450 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
    "}\n";

// Composite fragment shader: reads each pixel's linked list, insertion-sorts by
// depth (near -> far), composites back-to-front with the premultiplied-alpha
// "over" operator, and outputs the accumulated premultiplied color. The fixed
// blend state (ONE, ONE_MINUS_SRC_ALPHA) blends this over the target's existing
// opaque contents, so transparent is composited over opaque.
constexpr char kCompositeFragmentShader[] =
    "#version 450 core\n"
    "struct OITNode {\n"
    "    vec4 color;\n"
    "    float depth;\n"
    "    uint next;\n"
    "};\n"
    "layout(std430, binding = 0) coherent buffer NodeBuffer { OITNode nodes[]; "
    "};\n"
    "layout(r32ui, binding = 2) uniform coherent uimage2D uHead;\n"
    "uniform int uMaxNodes;\n"
    "layout(location = 0) out vec4 oColor;\n"
    "void main() {\n"
    "    OITNode list[16];\n"
    "    int count = 0;\n"
    "    uint cur = imageLoad(uHead, ivec2(gl_FragCoord.xy)).r;\n"
    "    while (cur != 0xFFFFFFFFu && count < uMaxNodes) {\n"
    "        list[count] = nodes[cur];\n"
    "        count++;\n"
    "        cur = nodes[cur].next;\n"
    "    }\n"
    "    for (int i = 1; i < count; ++i) {\n"
    "        OITNode key = list[i];\n"
    "        int j = i - 1;\n"
    "        while (j >= 0 && list[j].depth > key.depth) {\n"
    "            list[j + 1] = list[j];\n"
    "            j--;\n"
    "        }\n"
    "        list[j + 1] = key;\n"
    "    }\n"
    "    vec4 acc = vec4(0.0);\n"
    "    for (int i = count - 1; i >= 0; --i) {\n"
    "        vec4 s = list[i].color;\n"
    "        acc.rgb = s.rgb + (1.0 - s.a) * acc.rgb;\n"
    "        acc.a = s.a + (1.0 - s.a) * acc.a;\n"
    "    }\n"
    "    oColor = acc;\n"
    "}\n";

// Full-screen quad vertices in NDC (x,y): two triangles sharing a diagonal.
constexpr std::array<float, 8> kScreenQuadVerts = {
    -1.0f, -1.0f, // corner 0
    1.0f,  -1.0f, // corner 1
    1.0f,  1.0f,  // corner 2
    -1.0f, 1.0f,  // corner 3
};

} // namespace

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
    auto program = core::ShaderProgram::create(kCaptureVertexShader,
                                               kCaptureFragmentShader);
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
    auto program = core::ShaderProgram::create(kCompositeVertexShader,
                                               kCompositeFragmentShader);
    if (program.failed()) {
        return data::makeError<core::ShaderProgram*>(program.error().code,
                                                     program.error().message);
    }
    compositeProgram_ = std::move(*program);
    return data::makeValue<core::ShaderProgram*>(&*compositeProgram_);
}

data::Result<core::VertexArray*> LinkedListOIT::screenQuad() {
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

    vao->bind();
    vbo->bind();
    vbo->upload(kScreenQuadVerts.data(),
                kScreenQuadVerts.size() * sizeof(float),
                core::BufferUsage::StaticDraw);
    ebo->bind();
    ebo->upload(kIndices.data(), kIndices.size(),
                core::BufferUsage::StaticDraw);
    vao->setAttribute(0u, 2, /*normalized=*/false, strideBytes, 0u);
    vao->unbind();

    screenQuadVbo_ = std::move(*vbo);
    screenQuadEbo_ = std::move(*ebo);
    screenQuadVao_ = std::move(*vao);
    screenQuadIndexCount_ = kIndices.size();
    return data::makeValue<core::VertexArray*>(&*screenQuadVao_);
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
                                        const RenderTarget& target) {
    (void)camera;
    // (Re)allocate storage if the target size changed. A failure is reported
    // as a typed error (SPEC §5): the pipeline stays un-engaged and the frame
    // renders without OIT.
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
    core::bindImageR32ui(*headTexture_, kHeadImageUnit);
    nodeBuffer_->bindBase(kNodeBinding);
    counterBuffer_->bindBase(kCounterBinding);
    core::setViewport(0, 0, static_cast<int>(target.width),
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
                                      const RenderTarget& target) {
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
    // otherwise bind the offscreen FBO (mirrors MeshRenderer::render).
    if (target.framebuffer == nullptr) {
        core::bindDefaultFramebuffer();
    } else {
        target.framebuffer->bind();
    }
    core::setViewport(0, 0, static_cast<int>(target.width),
                      static_cast<int>(target.height));
    core::disableDepthTest();

    // The composite shader outputs the accumulated premultiplied transparent
    // color; blend it over the opaque contents with (ONE, ONE_MINUS_SRC_ALPHA).
    core::enablePremultipliedOverBlend();

    program->use();
    program->setUniformInt("uMaxNodes",
                           static_cast<std::int32_t>(maxFragmentsPerPixel_));
    const data::Result<void> draw =
        core::drawElements(*quadVao, screenQuadIndexCount_);

    // Restore draw state regardless of the draw result (no state leak).
    core::disableBlend();
    core::unbindImage(kHeadImageUnit);
    engaged_ = false;
    return draw;
}

} // namespace re::render
