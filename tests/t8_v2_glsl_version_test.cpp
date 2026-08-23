// tests/t8_v2_glsl_version_test.cpp — T8 gate (SPEC §9 V2.7, RE_GLSL_VERSION).
//
// Gate asserts (explainable):
//   In the gate env (llvmpipe) the macro expands to `#version 450` — a
//   static_assert on the macro's version value plus compiling a fixture shader
//   whose `#version` line is produced by the macro on llvmpipe.
//   The 460/hardware compile is a manual sample verification, not a gate
//   assertion (llvmpipe caps at GLSL 4.50, SPEC §8).
//
// Evidence rule (R4): every value is an explainable constant — 450 is the
// portable floor (explanation above), the compiled program id is non-zero (GL
// reserves 0), and the fixture shader's source line is the exact string
// "#version 450 core" produced by RE_GLSL_VERSION_LINE.

#include <gtest/gtest.h>

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

#include "core/glsl_version.hpp"
#include "core/shader_program.hpp"

// ---------------------------------------------------------------------------
// Compile-time gate: the macro must be the portable floor in the gate env.
// llvmpipe's GLSL compiler caps at 4.50; MESA_GL_VERSION_OVERRIDE=4.6 only
// raises the reported context version, never the GLSL level (SPEC §8).
// ---------------------------------------------------------------------------
static_assert(RE_GLSL_VERSION == 450,
              "gate env RE_GLSL_VERSION must be 450 (portable llvmpipe floor)");
static_assert(RE_GLSL_VERSION == 450 || RE_GLSL_VERSION == 460,
              "RE_GLSL_VERSION must be 450 or 460");

// RE_GLSL_VERSION_LINE must be exactly "#version 450 core" in the gate env.
static_assert(std::string_view(RE_GLSL_VERSION_LINE) == std::string_view("#version 450 core"),
              "RE_GLSL_VERSION_LINE must expand to \"#version 450 core\" in gate env");
static_assert(std::string_view(RE_GLSL_VERSION_STRING) == std::string_view("450"),
              "RE_GLSL_VERSION_STRING must be \"450\" in gate env");

namespace re::tests {

namespace {

// Minimal vertex/fragment bodies that compile under the macro-generated
// #version line. No uniforms vary with the version — the test is purely that
// the line produced by the macro is accepted by the llvmpipe GLSL compiler.
constexpr std::string_view kVertexBody = R"(
layout(location = 0) in vec3 aPos;
void main() {
    gl_Position = vec4(aPos, 1.0);
}
)";

constexpr std::string_view kFragmentBody = R"(
layout(location = 0) out vec4 oColor;
void main() {
    oColor = vec4(0.2, 0.4, 0.8, 1.0);
}
)";

// The exact string the macro must produce in the gate env. Explainable:
// 450 = portable floor (SPEC §8, §9 V2.7).
constexpr std::string_view kExpectedVersionLine = "#version 450 core";
constexpr int kExpectedVersionInt = 450;

} // namespace

TEST(T8GlslVersion, MacroExpandsToPortableFloor) {
    // Explainable constants: 450 is the portable floor; the line is the full
    // #version directive for a core profile shader at that level.
    EXPECT_EQ(RE_GLSL_VERSION, kExpectedVersionInt);
    EXPECT_EQ(std::string_view(RE_GLSL_VERSION_LINE), kExpectedVersionLine);
    EXPECT_EQ(std::string_view(RE_GLSL_VERSION_STRING), std::string_view("450"));
}

TEST(T8GlslVersion, FixtureShaderViaMacroCompilesOnLlvmpipe) {
    // Build vertex/fragment sources whose #version line is produced by the
    // macro (string literal concatenation at compile time + runtime body).
    // Compiling them on the llvmpipe context must succeed because llvmpipe
    // supports GLSL 4.50 and the context is 4.6 core (which accepts 4.50).
    const std::string vertexSource =
        std::string(RE_GLSL_VERSION_LINE) + std::string(kVertexBody);
    const std::string fragmentSource =
        std::string(RE_GLSL_VERSION_LINE) + std::string(kFragmentBody);

    // Sanity: the sources really do start with the expected line.
    EXPECT_EQ(vertexSource.rfind(kExpectedVersionLine, 0), 0u);
    EXPECT_EQ(fragmentSource.rfind(kExpectedVersionLine, 0), 0u);

    auto program = core::ShaderProgram::create(vertexSource, fragmentSource);
    ASSERT_TRUE(program.ok()) << program.error().message;
    // GL reserves name 0; a live program name is non-zero (explainable).
    EXPECT_NE(program->id(), 0u);
    EXPECT_TRUE(program->valid());

    // Also exercise the geometry path with the same macro-generated line.
    constexpr std::string_view kGeomBody = R"(
layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;
void main() {
    for (int i = 0; i < 3; ++i) {
        gl_Position = gl_in[i].gl_Position;
        EmitVertex();
    }
    EndPrimitive();
}
)";
    const std::string geomSource =
        std::string(RE_GLSL_VERSION_LINE) + std::string(kGeomBody);
    EXPECT_EQ(geomSource.rfind(kExpectedVersionLine, 0), 0u);
    auto geomProgram = core::ShaderProgram::createWithGeometry(vertexSource, geomSource, fragmentSource);
    ASSERT_TRUE(geomProgram.ok()) << geomProgram.error().message;
    EXPECT_NE(geomProgram->id(), 0u);
}

TEST(T8GlslVersion, ShaderFilesHeadMatchesMacro) {
    // Every .glsl file under render/shaders/ must head with the same line the
    // macro produces — otherwise the "single #version concern" is violated.
    // Check every shader file explicitly; each first line must equal
    // RE_GLSL_VERSION_LINE ("#version 450 core" in the gate env).
    const std::string_view kFiles[] = {
        "render/shaders/mesh_opaque.vert.glsl",
        "render/shaders/mesh_opaque.frag.glsl",
        "render/shaders/plane.vert.glsl",
        "render/shaders/plane.frag.glsl",
        "render/shaders/volume_raycast.vert.glsl",
        "render/shaders/volume_raycast.frag.glsl",
        "render/shaders/oit_capture.vert.glsl",
        "render/shaders/oit_capture.frag.glsl",
        "render/shaders/oit_composite.vert.glsl",
        "render/shaders/oit_composite.frag.glsl",
        "render/shaders/slice.vert.glsl",
        "render/shaders/slice_clip.geom.glsl",
        "render/shaders/slice_clip.frag.glsl",
        "render/shaders/slice_capture.geom.glsl",
        "render/shaders/slice_capture.frag.glsl",
        "render/shaders/contour.vert.glsl",
        "render/shaders/contour.geom.glsl",
        "render/shaders/contour.frag.glsl",
        "render/shaders/fixtures/malformed.vert.glsl",
        "render/shaders/fixtures/malformed.frag.glsl",
    };
    for (const std::string_view rel : kFiles) {
        const std::string path = std::string(TEST_SOURCE_DIR) + "/" + std::string(rel);
        auto source = core::ShaderProgram::loadSourceFile(path);
        ASSERT_TRUE(source.ok()) << rel << ": " << source.error().message;
        const std::size_t nl = source->find('\n');
        ASSERT_NE(nl, std::string::npos) << rel;
        const std::string firstLine = source->substr(0, nl);
        EXPECT_EQ(firstLine, std::string(kExpectedVersionLine))
            << rel << " first line must equal RE_GLSL_VERSION_LINE (single concern)";
        EXPECT_EQ(std::string_view(RE_GLSL_VERSION_LINE), std::string_view(firstLine)) << rel;
    }
}

} // namespace re::tests
