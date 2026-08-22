// tests/t6_v2_draw_cache_test.cpp — V2 T6 gate tests (SPEC §9 V2.10).
//
// Asserts the internal dirty-flag draw-state cache in core/draw.cpp:
//
//   (1) setClearColor(red); setClearColor(red) issues exactly 1 glClearColor
//       (second is a cache hit, no GL call), and different values are a miss;
//   (2) setViewport(x,y,w,h) repeated with identical args is a single
//       glViewport;
//   (3) enable*/disable* repeated with identical state is a single glEnable/
//       glDisable (depth test + blend, plus the premultiplied blend variant);
//   (4) output pixels are unchanged within 1/255 when the cache is exercised
//       (readback via the core/ wrapper only, through utils::PixelReader).
//
// Explainable constants: the clear colors are chosen so the expected RGBA8
// bytes are exact round(color*255) — e.g. {0.2,0.4,0.8} -> {51,102,204}.
// Tolerance is 1/255 per SPEC §4. Spy counts are analytic: duplicate calls
// must be exactly 1, distinct calls exactly 2, etc. (R4 — never bare >0).

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/draw.hpp"
#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/texture2d.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Helpers.
// ---------------------------------------------------------------------------

constexpr std::uint32_t kTargetW = 64u;
constexpr std::uint32_t kTargetH = 64u;
constexpr std::uint32_t kCenterX = kTargetW / 2u;
constexpr std::uint32_t kCenterY = kTargetH / 2u;

struct FboTarget {
    core::Texture2D color;
    core::Framebuffer fbo;
    FboTarget(core::Texture2D c, core::Framebuffer f)
        : color(std::move(c)), fbo(std::move(f)) {}
};

FboTarget makeTarget(std::uint32_t w, std::uint32_t h) {
    auto color = core::Texture2D::create();
    auto fb = core::Framebuffer::create();
    EXPECT_TRUE(color.ok()) << color.error().message;
    EXPECT_TRUE(fb.ok()) << fb.error().message;
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(w) * h * 4u, 0u);
    color->bind(0u);
    color->upload(w, h, zeros.data());
    color->unbind(0u);
    fb->bind();
    fb->attachColor(*color);
    EXPECT_TRUE(fb->isComplete());
    fb->unbind();
    return FboTarget(std::move(*color), std::move(*fb));
}

std::vector<std::uint8_t> readCenter(FboTarget& target) {
    target.fbo.bind();
    std::vector<std::uint8_t> pixels;
    utils::PixelReader reader;
    auto r = reader.read(kCenterX, kCenterY, 1u, 1u, pixels);
    EXPECT_TRUE(r.ok()) << r.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    target.fbo.unbind();
    return pixels;
}

void expectBytesNear(const std::vector<std::uint8_t>& px,
                     std::uint8_t r, std::uint8_t g, std::uint8_t b,
                     std::uint8_t a, const char* where) {
    constexpr int kTol = 1;
    EXPECT_NEAR(px[0], r, kTol) << "R at " << where;
    EXPECT_NEAR(px[1], g, kTol) << "G at " << where;
    EXPECT_NEAR(px[2], b, kTol) << "B at " << where;
    EXPECT_NEAR(px[3], a, kTol) << "A at " << where;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) setClearColor — duplicate is a cache hit (exactly 1 glClearColor).
// ---------------------------------------------------------------------------

TEST(T6V2DrawCache, ClearColorDuplicateIsOneGlCall) {
    core::invalidateDrawCache();
    core::setClearColor(0.9f, 0.1f, 0.3f, 1.0f);
    core::setClearColor(0.9f, 0.1f, 0.3f, 1.0f);
    const auto counts = core::getDrawSpyCounts();
    // Explainable: second call identical -> cache hit -> exactly 1 glClearColor.
    EXPECT_EQ(counts.clearColor, 1) << "duplicate setClearColor must be 1 glClearColor";
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T6V2DrawCache, ClearColorDifferentValuesAreTwoCalls) {
    core::invalidateDrawCache();
    core::setClearColor(0.9f, 0.1f, 0.3f, 1.0f);
    core::setClearColor(0.1f, 0.9f, 0.3f, 1.0f);
    const auto counts = core::getDrawSpyCounts();
    EXPECT_EQ(counts.clearColor, 2) << "distinct clear colors must be 2 glClearColor";
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T6V2DrawCache, ClearColorTripleDuplicateStillOne) {
    core::invalidateDrawCache();
    core::setClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    core::setClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    core::setClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    EXPECT_EQ(core::getDrawSpyCounts().clearColor, 1);
}

// ---------------------------------------------------------------------------
// (2) setViewport — duplicate is a cache hit.
// ---------------------------------------------------------------------------

TEST(T6V2DrawCache, ViewportDuplicateIsOneGlCall) {
    core::invalidateDrawCache();
    core::setViewport(0, 0, 64, 64);
    core::setViewport(0, 0, 64, 64);
    EXPECT_EQ(core::getDrawSpyCounts().viewport, 1) << "duplicate viewport must be 1 glViewport";
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T6V2DrawCache, ViewportDifferentValuesAreTwoCalls) {
    core::invalidateDrawCache();
    core::setViewport(0, 0, 64, 64);
    core::setViewport(0, 0, 32, 32);
    EXPECT_EQ(core::getDrawSpyCounts().viewport, 2);
}

TEST(T6V2DrawCache, ViewportTripleDuplicateStillOne) {
    core::invalidateDrawCache();
    core::setViewport(10, 20, 100, 200);
    core::setViewport(10, 20, 100, 200);
    core::setViewport(10, 20, 100, 200);
    EXPECT_EQ(core::getDrawSpyCounts().viewport, 1);
}

// ---------------------------------------------------------------------------
// (3) enable*/disable* — duplicate is a cache hit.
// ---------------------------------------------------------------------------

TEST(T6V2DrawCache, EnableDepthTestDuplicateIsOne) {
    core::invalidateDrawCache();
    core::enableDepthTest();
    core::enableDepthTest();
    const auto c = core::getDrawSpyCounts();
    EXPECT_EQ(c.enableDepthTest, 1) << "duplicate enableDepthTest must be 1 glEnable";
    EXPECT_EQ(c.disableDepthTest, 0);
}

TEST(T6V2DrawCache, DisableDepthTestDuplicateIsOne) {
    core::invalidateDrawCache();
    // First ensure depth is enabled so disable is a real transition.
    core::enableDepthTest();
    core::resetDrawSpyCounts();
    // Now test duplicate disable.
    core::disableDepthTest();
    core::disableDepthTest();
    const auto c = core::getDrawSpyCounts();
    EXPECT_EQ(c.disableDepthTest, 1) << "duplicate disableDepthTest must be 1 glDisable";
    EXPECT_EQ(c.enableDepthTest, 0);
}

TEST(T6V2DrawCache, DepthTestEnableDisableReEnable) {
    core::invalidateDrawCache();
    core::enableDepthTest();
    core::disableDepthTest();
    core::enableDepthTest();
    const auto c = core::getDrawSpyCounts();
    // Three distinct state changes -> 2 enables, 1 disable.
    EXPECT_EQ(c.enableDepthTest, 2);
    EXPECT_EQ(c.disableDepthTest, 1);
}

TEST(T6V2DrawCache, EnableBlendDuplicateIsOne) {
    core::invalidateDrawCache();
    core::enableBlend();
    core::enableBlend();
    const auto c = core::getDrawSpyCounts();
    EXPECT_EQ(c.enableBlend, 1) << "duplicate enableBlend must be 1 GL enable for blend";
}

TEST(T6V2DrawCache, DisableBlendDuplicateIsOne) {
    core::invalidateDrawCache();
    core::enableBlend();
    core::resetDrawSpyCounts();
    core::disableBlend();
    core::disableBlend();
    const auto c = core::getDrawSpyCounts();
    EXPECT_EQ(c.disableBlend, 1);
    EXPECT_EQ(c.enableBlend, 0);
}

TEST(T6V2DrawCache, BlendEnableDisableReEnable) {
    core::invalidateDrawCache();
    core::enableBlend();
    core::disableBlend();
    core::enableBlend();
    const auto c = core::getDrawSpyCounts();
    EXPECT_EQ(c.enableBlend, 2);
    EXPECT_EQ(c.disableBlend, 1);
}

TEST(T6V2DrawCache, PremultipliedBlendDuplicateIsOne) {
    core::invalidateDrawCache();
    core::enablePremultipliedOverBlend();
    core::enablePremultipliedOverBlend();
    const auto c = core::getDrawSpyCounts();
    // First call issues enableBlend + blendFunc; second is a cache hit.
    EXPECT_EQ(c.enableBlend, 1) << "duplicate premultiplied must be 1 enableBlend";
    EXPECT_EQ(c.blendFunc, 1) << "duplicate premultiplied must be 1 blendFunc";
}

TEST(T6V2DrawCache, PremultipliedBlendAfterEnableBlendCachesEnable) {
    core::invalidateDrawCache();
    core::enableBlend();
    core::resetDrawSpyCounts();
    // enableBlend already set blendEnabled=true but blendFunc not set.
    // premultiplied should only issue blendFunc, not a second enable.
    core::enablePremultipliedOverBlend();
    const auto c = core::getDrawSpyCounts();
    EXPECT_EQ(c.enableBlend, 0) << "blend already enabled, premultiplied must not re-enable";
    EXPECT_EQ(c.blendFunc, 1);
}

TEST(T6V2DrawCache, PremultipliedBlendDifferentSequence) {
    core::invalidateDrawCache();
    core::enablePremultipliedOverBlend();
    // Now disable and re-enable -> both need to re-issue.
    core::disableBlend();
    core::resetDrawSpyCounts();
    core::enablePremultipliedOverBlend();
    const auto c = core::getDrawSpyCounts();
    // After disable, premultiplied must re-enable and re-set func (func was
    // still cached but enable was not — our impl caches func separately, so
    // func is still cached. The second call after disable should need enable
    // but func is already cached -> only 1 enable, 0 func? Actually func is
    // cached from first call and not invalidated by disable, so second call
    // only needs enable. Verify this invariant.
    EXPECT_EQ(c.enableBlend, 1);
    // blendFunc should be 0 because it was already ONE/ONE_MINUS_SRC_ALPHA.
    EXPECT_EQ(c.blendFunc, 0) << "blendFunc still cached after disableBlend";
}

// Invalidate must force a re-issue.
TEST(T6V2DrawCache, InvalidateForcesReissue) {
    core::invalidateDrawCache();
    core::setClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    EXPECT_EQ(core::getDrawSpyCounts().clearColor, 1);
    core::invalidateDrawCache();
    core::setClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    EXPECT_EQ(core::getDrawSpyCounts().clearColor, 1)
        << "after invalidate, same value must re-issue (cache cleared)";
    core::setClearColor(0.5f, 0.5f, 0.5f, 1.0f);
    EXPECT_EQ(core::getDrawSpyCounts().clearColor, 1)
        << "second duplicate after invalidate still 1";
}

// ---------------------------------------------------------------------------
// (4) Output pixels unchanged within 1/255 when cache is exercised.
// ---------------------------------------------------------------------------

TEST(T6V2DrawCache, CachedClearColorPixelsUnchangedWithinOne255) {
    // 64x64 offscreen target, cleared to {0.2,0.4,0.8} -> bytes {51,102,204}.
    // The cache must not alter the rendered result (within 1/255).
    FboTarget target = makeTarget(kTargetW, kTargetH);
    core::invalidateDrawCache();
    target.fbo.bind();
    core::setViewport(0, 0, static_cast<int>(kTargetW), static_cast<int>(kTargetH));
    core::setViewport(0, 0, static_cast<int>(kTargetW), static_cast<int>(kTargetH));
    // Exactly 1 glViewport despite two calls.
    EXPECT_EQ(core::getDrawSpyCounts().viewport, 1);

    core::setClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    core::setClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    EXPECT_EQ(core::getDrawSpyCounts().clearColor, 1);
    core::clearColor();
    auto px1 = readCenter(target);
    expectBytesNear(px1, 51u, 102u, 204u, 255u, "first clear 0.2,0.4,0.8");

    // Second frame with identical state via cache hits — pixels must match
    // within 1/255. Re-bind the target (readCenter unbinds it).
    target.fbo.bind();
    core::setViewport(0, 0, static_cast<int>(kTargetW), static_cast<int>(kTargetH));
    core::setClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    // No additional GL calls.
    EXPECT_EQ(core::getDrawSpyCounts().viewport, 1);
    EXPECT_EQ(core::getDrawSpyCounts().clearColor, 1);
    core::clearColor();
    auto px2 = readCenter(target);
    expectBytesNear(px2, 51u, 102u, 204u, 255u, "second cached clear 0.2,0.4,0.8");
    // Pixel-for-pixel equality within 1/255.
    for (int i = 0; i < 4; ++i) {
        EXPECT_NEAR(px1[i], px2[i], 1) << "channel " << i << " must be within 1/255";
    }
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T6V2DrawCache, CachedClearThenDifferentColorChangesPixels) {
    FboTarget target = makeTarget(kTargetW, kTargetH);
    core::invalidateDrawCache();
    target.fbo.bind();
    core::setViewport(0, 0, static_cast<int>(kTargetW), static_cast<int>(kTargetH));
    // First clear: red-ish {0.9,0.1,0.3} -> {230,26,77}
    core::setClearColor(0.9f, 0.1f, 0.3f, 1.0f);
    core::clearColor();
    auto pxRed = readCenter(target);
    expectBytesNear(pxRed, 230u, 26u, 77u, 255u, "red clear 0.9,0.1,0.3");

    // Second clear: different color must actually change pixels (cache miss).
    // Re-bind the target (readCenter unbinds it to 0).
    target.fbo.bind();
    core::setClearColor(0.1f, 0.9f, 0.3f, 1.0f);
    EXPECT_EQ(core::getDrawSpyCounts().clearColor, 2);
    core::clearColor();
    auto pxGreen = readCenter(target);
    expectBytesNear(pxGreen, 26u, 230u, 77u, 255u, "green clear 0.1,0.9,0.3");

    // The two clears must be visibly different (>1/255 apart on R/G).
    EXPECT_GT(std::abs(static_cast<int>(pxRed[0]) - static_cast<int>(pxGreen[0])), 1);
    EXPECT_FALSE(core::hasPendingGlError());
}

} // namespace re::tests
