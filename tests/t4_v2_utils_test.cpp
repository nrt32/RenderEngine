// tests/t4_v2_utils_test.cpp — T4 gate tests (SPEC §9 V2.1: utils/ module).
//
// Asserts the V2.1 requirements from TASKS.md T4:
//   (1) tools/env.sh sets AUDIT_SOURCE_DIRS including `utils` — the literal
//       export line is asserted from the committed file (golden corpus hit,
//       R4) so the audit keeps scanning the utils/ sources
//       (guardrails gpu_api_ownership / no_production_readback);
//   (2) the offscreen GL context moved to utils/: the shared fixture context
//       is a re::utils::OffscreenContext and still reports the SPEC §2
//       GL 4.6 core invariants (major == 4, minor == 6, core profile bit) —
//       tests keep passing via re::utils::*;
//   (3) re::utils::PixelReader reads back a known solid color exactly, and
//       agrees byte-for-byte with the core/ raw-GL anchor core::readRgba8
//       (the anchor stays under core/; no_production_readback intact).
//
// Per the GL-ownership guardrail, this file uses ONLY core/ wrappers and the
// utils/ facades — no raw glXxx calls.

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "core/draw.hpp"
#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/read_pixels.hpp"
#include "core/texture2d.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/offscreen_context.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {

namespace {

// ---------------------------------------------------------------------------
// Explainable constants (SPEC §2 / FR-core, R4).
// ---------------------------------------------------------------------------

// The committed tools/env.sh export line for AUDIT_SOURCE_DIRS (TASKS.md
// R15 + the V3 DoD line: "io data volume scene core broker render app utils tests").
constexpr const char* kAuditSourceDirsLiteral =
    "export AUDIT_SOURCE_DIRS=\"io data volume scene core broker render app utils tests\"";

// 8x8 RGBA8 color target.
constexpr std::uint32_t kTargetWidth = 8u;
constexpr std::uint32_t kTargetHeight = 8u;
constexpr std::uint32_t kCenterX = 4u;
constexpr std::uint32_t kCenterY = 4u;

// Known clear color: opaque red -> exact bytes (255, 0, 0, 255).
constexpr float kClearR = 1.0f;
constexpr float kClearG = 0.0f;
constexpr float kClearB = 0.0f;
constexpr float kClearA = 1.0f;
constexpr std::uint8_t kExpectedRed = 255u;
constexpr std::uint8_t kExpectedGreen = 0u;
constexpr std::uint8_t kExpectedBlue = 0u;
constexpr std::uint8_t kExpectedAlpha = 255u;

} // namespace

// ---------------------------------------------------------------------------
// (1) tools/env.sh sets AUDIT_SOURCE_DIRS including utils.
// ---------------------------------------------------------------------------

TEST(T4V2Utils, EnvShSetsAuditSourceDirsIncludingUtils) {
    // Golden-corpus hit: the committed tools/env.sh must carry the utils/
    // module in its AUDIT_SOURCE_DIRS export (TASKS.md T4 D: "Add utils to
    // AUDIT_SOURCE_DIRS").
    const std::string path = std::string(TEST_SOURCE_DIR) + "/tools/env.sh";
    std::ifstream in(path);
    ASSERT_TRUE(in.good()) << "cannot read " << path;
    std::ostringstream contents;
    contents << in.rdbuf();
    const std::string text = contents.str();

    EXPECT_NE(text.find(kAuditSourceDirsLiteral), std::string::npos)
        << "tools/env.sh must contain the literal:\n"
        << kAuditSourceDirsLiteral << "\nfile:\n"
        << text;

    // The running environment must have been sourced with it (R15).
    const char* auditDirs = std::getenv("AUDIT_SOURCE_DIRS");
    ASSERT_NE(auditDirs, nullptr) << "AUDIT_SOURCE_DIRS is unset. Launch with: "
                                     "source tools/env.sh (SPEC S8, R15).";
    EXPECT_NE(std::string(auditDirs).find("utils"), std::string::npos);
}

// ---------------------------------------------------------------------------
// (2) The offscreen context lives in utils/ and keeps the GL 4.6 core
//     invariants.
// ---------------------------------------------------------------------------

TEST(T4V2Utils, FixtureContextIsUtilsOffscreenContextGl46Core) {
    // The shared fixture context is now a utils/ component (V2.1 moved it out
    // of core/); the SPEC §2 invariants it reports are unchanged.
    utils::OffscreenContext* ctx = OffscreenEnvironment::context();
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->majorVersion(), 4);
    EXPECT_EQ(ctx->minorVersion(), 6);
    EXPECT_TRUE(ctx->isCoreProfile());
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (3) utils::PixelReader reads a known color exactly and matches the core/
//     raw-GL anchor.
// ---------------------------------------------------------------------------

namespace {

/// An 8x8 color-only FBO target. Keeps the color texture (the FBO's
/// attachment) alive for the lifetime of the target, mirroring the pattern in
/// the render tests (t1_v2_ir_dispatch_test.cpp).
struct ClearedTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;

    ClearedTarget(core::Texture2D color, core::Framebuffer framebuffer)
        : color(std::move(color)), framebuffer(std::move(framebuffer)) {}
};

/// Build an 8x8 color-only FBO cleared to the known opaque-red clear color.
ClearedTarget makeClearedTarget() {
    auto color = core::Texture2D::create();
    auto framebuffer = core::Framebuffer::create();
    EXPECT_TRUE(color.ok()) << color.error().message;
    EXPECT_TRUE(framebuffer.ok()) << framebuffer.error().message;
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(kTargetWidth) *
                                        kTargetHeight * 4u,
                                    0u);
    color->bind(0u);
    color->upload(kTargetWidth, kTargetHeight, zeros.data());
    color->unbind(0u);
    framebuffer->bind();
    framebuffer->attachColor(*color);
    EXPECT_TRUE(framebuffer->isComplete());

    core::setViewport(0, 0, static_cast<int>(kTargetWidth),
                      static_cast<int>(kTargetHeight));
    core::setClearColor(kClearR, kClearG, kClearB, kClearA);
    core::clearColor();
    return ClearedTarget(std::move(*color), std::move(*framebuffer));
}

} // namespace

TEST(T4V2Utils, PixelReaderReadsKnownClearColor) {
    auto target = makeClearedTarget();
    ASSERT_TRUE(target.framebuffer.valid());
    target.framebuffer.bind();

    // Read the center pixel through the utils/ facade.
    utils::PixelReader reader;
    std::vector<std::uint8_t> pixels;
    auto read = reader.read(kCenterX, kCenterY, 1u, 1u, pixels);
    ASSERT_TRUE(read.ok()) << read.error().message;
    ASSERT_EQ(pixels.size(), 4u);

    // Exact bytes of the opaque-red clear color (analytic constant).
    EXPECT_EQ(pixels[0], kExpectedRed) << "R channel";
    EXPECT_EQ(pixels[1], kExpectedGreen) << "G channel";
    EXPECT_EQ(pixels[2], kExpectedBlue) << "B channel";
    EXPECT_EQ(pixels[3], kExpectedAlpha) << "A channel";
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T4V2Utils, PixelReaderMatchesCoreReadRgba8Anchor) {
    auto target = makeClearedTarget();
    ASSERT_TRUE(target.framebuffer.valid());
    target.framebuffer.bind();

    // The same pixel read through the utils/ facade and through the core/
    // raw-GL anchor (core::readRgba8, kept under core/ by V2.1) must agree
    // byte-for-byte: PixelReader delegates to the anchor.
    utils::PixelReader reader;
    std::vector<std::uint8_t> viaUtils;
    std::vector<std::uint8_t> viaCore;
    auto readUtils = reader.read(kCenterX, kCenterY, 1u, 1u, viaUtils);
    auto readCore = core::readRgba8(kCenterX, kCenterY, 1u, 1u, viaCore);
    ASSERT_TRUE(readUtils.ok()) << readUtils.error().message;
    ASSERT_TRUE(readCore.ok()) << readCore.error().message;

    ASSERT_EQ(viaUtils.size(), 4u);
    ASSERT_EQ(viaCore.size(), 4u);
    EXPECT_EQ(viaUtils, viaCore) << "utils::PixelReader must delegate to the "
                                    "core/ raw-GL anchor";
    // And both carry the known clear color.
    EXPECT_EQ(viaCore[0], kExpectedRed);
    EXPECT_EQ(viaCore[3], kExpectedAlpha);
    EXPECT_FALSE(core::hasPendingGlError());
}

} // namespace re::tests