// tests/t3_core_gl_test.cpp — T3 gate tests (FR-core.1/2, SPEC §4).
//
// Asserts:
//   (1) create -> bind -> destroy of each RAII GL object (VAO, VBO, EBO,
//       Texture2D, Framebuffer) produces no GL errors under the offscreen
//       fixture and is ASan/LSan clean (the suite runs under ASan+UBSan);
//   (2) a valid GLSL 450 shader compiles/links and reports no error;
//   (3) an intentionally-malformed shader (known-bad token `glibberish` on
//       line 7) returns a typed error containing that token and the golden
//       substring "ERROR: 0:7", with no crash;
//   (4) destructor order frees the GL objects (no GL errors on teardown).
//
// Shaders are written in GLSL 450, not 460 (SPEC §8): the headless gate runs
// on llvmpipe, whose GLSL compiler caps at 4.50; a GL 4.6 core context
// accepts 4.50 shaders.
//
// Per the GL-ownership guardrail this file uses ONLY core/ wrappers — no raw
// glXxx calls.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

#include "core/element_buffer.hpp"
#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/shader_program.hpp"
#include "core/texture2d.hpp"
#include "core/vertex_array.hpp"
#include "core/vertex_buffer.hpp"
#include "tests/offscreen_fixture.hpp"

namespace re::tests {

namespace {

// ---------------------------------------------------------------------------
// Explainable constants (FR-core.1/2).
// ---------------------------------------------------------------------------

// Golden triangle: a unit right triangle in the XY plane, interleaved
// (x, y, z, r, g, b): 3 vertices * 6 floats = 18 floats.
constexpr float kTriangleVertices[] = {
    0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, // v0: origin, red
    1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, // v1: +X, green
    0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, // v2: +Y, blue
};
constexpr std::size_t kTriangleVertexCount = 3;
constexpr std::size_t kTriangleVertexFloats = kTriangleVertexCount * 6u; // 18
constexpr std::size_t kInterleavedStrideBytes = 6u * sizeof(float);      // 24
constexpr std::uint32_t kTriangleIndices[kTriangleVertexCount] = {0u, 1u, 2u};

// 4x4 RGBA8 image: 4 * 4 * 4 = 64 bytes.
constexpr std::uint32_t kTextureWidth = 4u;
constexpr std::uint32_t kTextureHeight = 4u;
constexpr std::size_t kTextureBytes = kTextureWidth * kTextureHeight * 4u;

// FBO color attachment: 8x8 RGBA8 -> 8 * 8 * 4 = 256 bytes.
constexpr std::uint32_t kFboWidth = 8u;
constexpr std::uint32_t kFboHeight = 8u;
constexpr std::size_t kFboTextureBytes = kFboWidth * kFboHeight * 4u;

static_assert(std::size(kTriangleVertices) == kTriangleVertexFloats);
static_assert(std::size(kTriangleIndices) == kTriangleVertexCount);

/// Deterministic RGBA8 image: byte i == (i * 7) mod 256 (content is not read
/// back; upload exercises the full glTexImage2D path).
std::vector<std::uint8_t> fillGradient(std::size_t byteCount) {
    std::vector<std::uint8_t> image(byteCount);
    for (std::size_t i = 0; i < byteCount; ++i) {
        image[i] = static_cast<std::uint8_t>((i * 7u) % 256u);
    }
    return image;
}

// ---------------------------------------------------------------------------
// GLSL 450 shader sources (SPEC §8: gate/test shaders are GLSL 450, not 460).
// Line numbers are part of the FR-core.2 golden diagnostics: in
// kMalformedVertexShader the known-bad token `glibberish` sits on line 7.
// ---------------------------------------------------------------------------

constexpr char kValidVertexShader[] =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec3 aColor;\n"
    "out vec3 vColor;\n"
    "void main() {\n"
    "    vColor = aColor;\n"
    "    gl_Position = vec4(aPos, 1.0);\n"
    "}\n";

constexpr char kValidFragmentShader[] =
    "#version 450 core\n"
    "layout(location = 0) out vec4 oColor;\n"
    "in vec3 vColor;\n"
    "void main() {\n"
    "    oColor = vec4(vColor, 1.0);\n"
    "}\n";

// Line 7 (line 1 is #version) contains the known-bad token `glibberish`.
constexpr char kMalformedVertexShader[] =
    "#version 450 core\n"
    "layout(location = 0) in vec3 aPos;\n"
    "layout(location = 1) in vec3 aColor;\n"
    "out vec3 vColor;\n"
    "void main() {\n"
    "    vColor = aColor;\n"
    "    gl_Position = vec4(aPos, glibberish);\n" // line 7
    "}\n";

// Line 5 contains the known-bad token `glibberish` (fragment-stage variant).
constexpr char kMalformedFragmentShader[] =
    "#version 450 core\n"
    "layout(location = 0) out vec4 oColor;\n"
    "in vec3 vColor;\n"
    "void main() {\n"
    "    oColor = vec4(glibberish, 1.0);\n" // line 5
    "}\n";

} // namespace

// ---------------------------------------------------------------------------
// (1) RAII object lifecycles: create -> bind -> destroy, no GL errors.
// ---------------------------------------------------------------------------

TEST(T3CoreGl, VertexBufferCreateBindUploadDestroyIsErrorFree) {
    auto buffer = core::VertexBuffer::create();
    ASSERT_TRUE(buffer.ok()) << buffer.error().message;
    // GL reserves name 0: generated object names are always non-zero.
    EXPECT_NE(buffer->id(), 0u);
    EXPECT_TRUE(buffer->valid());

    buffer->bind();
    EXPECT_FALSE(core::hasPendingGlError());
    buffer->upload(kTriangleVertices, sizeof(kTriangleVertices),
                   core::BufferUsage::StaticDraw);
    EXPECT_FALSE(core::hasPendingGlError());
    buffer->unbind();
    EXPECT_FALSE(core::hasPendingGlError());
    // Scope end: the destructor deletes the GL buffer.
}

TEST(T3CoreGl, ElementBufferCreateBindUploadDestroyIsErrorFree) {
    auto buffer = core::ElementBuffer::create();
    ASSERT_TRUE(buffer.ok()) << buffer.error().message;
    EXPECT_NE(buffer->id(), 0u);
    EXPECT_TRUE(buffer->valid());

    buffer->bind();
    EXPECT_FALSE(core::hasPendingGlError());
    buffer->upload(kTriangleIndices, kTriangleVertexCount,
                   core::BufferUsage::StaticDraw);
    EXPECT_FALSE(core::hasPendingGlError());
    buffer->unbind();
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T3CoreGl, VertexArrayCreateBindConfigureDestroyIsErrorFree) {
    auto vao = core::VertexArray::create();
    ASSERT_TRUE(vao.ok()) << vao.error().message;
    EXPECT_NE(vao->id(), 0u);

    vao->bind();
    EXPECT_FALSE(core::hasPendingGlError());

    // vertex attribute pointer requires a bound array buffer; bind a VBO with
    // the golden triangle so the attribute configuration is valid.
    auto vbo = core::VertexBuffer::create();
    ASSERT_TRUE(vbo.ok());
    vbo->bind();
    vbo->upload(kTriangleVertices, sizeof(kTriangleVertices),
                core::BufferUsage::StaticDraw);

    // Interleaved (pos3, color3): attribute 0 at offset 0, attribute 1 at
    // offset 12 bytes, stride 24 bytes.
    vao->setAttribute(0u, 3, /*normalized=*/false, kInterleavedStrideBytes, 0u);
    vao->setAttribute(1u, 3, /*normalized=*/false, kInterleavedStrideBytes,
                      3u * sizeof(float));
    EXPECT_FALSE(core::hasPendingGlError());

    vao->unbind();
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T3CoreGl, Texture2DCreateBindUploadDestroyIsErrorFree) {
    auto texture = core::Texture2D::create();
    ASSERT_TRUE(texture.ok()) << texture.error().message;
    EXPECT_NE(texture->id(), 0u);

    texture->bind(0u);
    EXPECT_FALSE(core::hasPendingGlError());
    const std::vector<std::uint8_t> image = fillGradient(kTextureBytes);
    texture->upload(kTextureWidth, kTextureHeight, image.data());
    EXPECT_FALSE(core::hasPendingGlError());
    texture->unbind(0u);
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T3CoreGl, FramebufferCreateBindAttachDestroyIsErrorFree) {
    auto framebuffer = core::Framebuffer::create();
    ASSERT_TRUE(framebuffer.ok()) << framebuffer.error().message;
    EXPECT_NE(framebuffer->id(), 0u);

    auto color = core::Texture2D::create();
    ASSERT_TRUE(color.ok());
    color->bind(0u);
    const std::vector<std::uint8_t> image = fillGradient(kFboTextureBytes);
    color->upload(kFboWidth, kFboHeight, image.data());
    color->unbind(0u);

    framebuffer->bind();
    EXPECT_FALSE(core::hasPendingGlError());
    framebuffer->attachColor(*color);
    EXPECT_FALSE(core::hasPendingGlError());
    // A color-only FBO with a complete 8x8 RGBA8 attachment is complete.
    EXPECT_TRUE(framebuffer->isComplete());
    framebuffer->unbind();
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (4) Destructor order: all five RAII objects freed, no GL errors on teardown.
// ---------------------------------------------------------------------------

TEST(T3CoreGl, DestructionOrderFreesAllRaiiObjectsWithoutErrors) {
    {
        auto vao = core::VertexArray::create();
        auto vbo = core::VertexBuffer::create();
        auto ebo = core::ElementBuffer::create();
        auto texture = core::Texture2D::create();
        auto framebuffer = core::Framebuffer::create();
        ASSERT_TRUE(vao.ok());
        ASSERT_TRUE(vbo.ok());
        ASSERT_TRUE(ebo.ok());
        ASSERT_TRUE(texture.ok());
        ASSERT_TRUE(framebuffer.ok());

        vao->bind();
        vbo->bind();
        vbo->upload(kTriangleVertices, sizeof(kTriangleVertices),
                    core::BufferUsage::StaticDraw);
        ebo->bind();
        ebo->upload(kTriangleIndices, kTriangleVertexCount,
                    core::BufferUsage::StaticDraw);
        vao->setAttribute(0u, 3, /*normalized=*/false, kInterleavedStrideBytes,
                          0u);
        vao->setAttribute(1u, 3, /*normalized=*/false, kInterleavedStrideBytes,
                          3u * sizeof(float));

        texture->bind(0u);
        const std::vector<std::uint8_t> image = fillGradient(kFboTextureBytes);
        texture->upload(kFboWidth, kFboHeight, image.data());
        texture->unbind(0u);

        framebuffer->bind();
        framebuffer->attachColor(*texture);
        EXPECT_TRUE(framebuffer->isComplete());
        EXPECT_FALSE(core::hasPendingGlError());
        framebuffer->unbind();
        vao->unbind();
        EXPECT_FALSE(core::hasPendingGlError());
    }
    // All five destructors ran (reverse declaration order). Freeing the GL
    // objects must leave no GL error state behind (FR-core.1); LeakSanitizer
    // (suite runs with ASAN_OPTIONS=detect_leaks=1) asserts no leaked names.
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (2) Valid GLSL 450 shader compiles/links with no error.
// ---------------------------------------------------------------------------

TEST(T3CoreGl, ShaderProgramValidCompileLinkNoError) {
    auto program =
        core::ShaderProgram::create(kValidVertexShader, kValidFragmentShader);
    ASSERT_TRUE(program.ok()) << program.error().message;
    EXPECT_NE(program->id(), 0u);

    program->use();
    EXPECT_FALSE(core::hasPendingGlError());
    program->unuse();
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (3) Malformed shader -> typed diagnostics with the golden substring.
// ---------------------------------------------------------------------------

TEST(T3CoreGl, ShaderProgramMalformedReturnsTypedDiagnostics) {
    // FR-core.2: the known-bad token `glibberish` on line 7 must surface in
    // the typed error together with the offending line — the golden substring
    // "ERROR: 0:7" (normalized diagnostic prefix + "0:7" location).
    auto program = core::ShaderProgram::create(kMalformedVertexShader,
                                               kValidFragmentShader);
    ASSERT_TRUE(program.failed());
    EXPECT_EQ(program.error().code,
              static_cast<int>(core::ShaderError::VertexCompile));

    const std::string& message = program.error().message;
    EXPECT_NE(message.find("glibberish"), std::string::npos) << message;
    EXPECT_NE(message.find("ERROR: 0:7"), std::string::npos) << message;

    // A failed compile must not leave GL error state behind.
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T3CoreGl, ShaderProgramMalformedFragmentReportsFragmentCode) {
    auto program = core::ShaderProgram::create(kValidVertexShader,
                                               kMalformedFragmentShader);
    ASSERT_TRUE(program.failed());
    EXPECT_EQ(program.error().code,
              static_cast<int>(core::ShaderError::FragmentCompile));

    const std::string& message = program.error().message;
    EXPECT_NE(message.find("glibberish"), std::string::npos) << message;
    EXPECT_NE(message.find("ERROR: 0:5"), std::string::npos) << message;
}

// V2.6 relocation: the same malformed vertex shader now also lives as a
// fixture file render/shaders/fixtures/malformed.vert.glsl. Loading it via
// core::ShaderProgram::loadSourceFile + create must reproduce the identical
// golden substring ERROR: 0:7 (line numbers preserved through file I/O).
TEST(T3CoreGl, ShaderProgramMalformedFixtureFileReproducesGoldenSubstring) {
    const std::string fixturePath =
        std::string(TEST_SOURCE_DIR) + "/render/shaders/fixtures/malformed.vert.glsl";
    auto source = core::ShaderProgram::loadSourceFile(fixturePath);
    ASSERT_TRUE(source.ok()) << source.error().message;
    // Line 7 still contains the known-bad token glibberish (explainable:
    // fixture is byte-for-byte the former constexpr, see docs/render.md).
    EXPECT_NE(source->find("glibberish"), std::string::npos);

    auto program =
        core::ShaderProgram::create(*source, kValidFragmentShader);
    ASSERT_TRUE(program.failed());
    EXPECT_EQ(program.error().code,
              static_cast<int>(core::ShaderError::VertexCompile));
    const std::string& message = program.error().message;
    EXPECT_NE(message.find("glibberish"), std::string::npos) << message;
    EXPECT_NE(message.find("ERROR: 0:7"), std::string::npos) << message;
    EXPECT_FALSE(core::hasPendingGlError());

    // File helper createFromFiles must also preserve the line number.
    // Both fixture files are malformed (vertex at line 7, fragment at line 5);
    // vertex fails first so the golden ERROR: 0:7 must be present.
    auto programFromFiles = core::ShaderProgram::createFromFiles(
        fixturePath,
        std::string(TEST_SOURCE_DIR) + "/render/shaders/fixtures/malformed.frag.glsl");
    ASSERT_TRUE(programFromFiles.failed());
    EXPECT_EQ(programFromFiles.error().code,
              static_cast<int>(core::ShaderError::VertexCompile));
    EXPECT_NE(programFromFiles.error().message.find("ERROR: 0:7"),
              std::string::npos)
        << programFromFiles.error().message;
    EXPECT_NE(programFromFiles.error().message.find("glibberish"),
              std::string::npos)
        << programFromFiles.error().message;
    // Re-assert via vertex+valid fragment file helper to prove the error comes
    // from the vertex stage, not the fragment fixture:
    auto vertexOnlyProgram = core::ShaderProgram::createFromFiles(
        fixturePath,
        std::string(TEST_SOURCE_DIR) + "/render/shaders/mesh_opaque.frag.glsl");
    ASSERT_TRUE(vertexOnlyProgram.failed());
    EXPECT_EQ(vertexOnlyProgram.error().code,
              static_cast<int>(core::ShaderError::VertexCompile));
    EXPECT_NE(vertexOnlyProgram.error().message.find("ERROR: 0:7"),
              std::string::npos)
        << vertexOnlyProgram.error().message;
}

} // namespace re::tests
