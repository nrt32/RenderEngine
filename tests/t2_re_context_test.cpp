// tests/t2_re_context_test.cpp — T2 gate: REContext global per GL context (formerly DrawContext).
//
// Asserts (T2 D/T):
//  (1) cross-pass dedup spy proves 2 layers sharing state issue 1 glViewport —
//      two setViewport via REContext::current() with same rect on the same GL
//      context must be a cache hit (exactly 1 glViewport, not 2);
//  (2) REContext::current() switches correctly after makeContextCurrent to a
//      second offscreen context — each GLFWwindow* owns its own mirror (viewport
//      etc.), fresh second context starts cold (0), first context's cache is
//      preserved after switch back (no bleed), and explicit invalidate() is
//      public for tests / SampleHarness post-ImGui boundary;
//  (3) regression R3 byte-identical — a simple clear via REContext still
//      produces the analytic pixel (51,102,204) within 1/255, proving the rename
//      and per-context mirror did not change output.
//
// Explainable constants: viewport rects 64x64 vs 128x128 are distinct; spy counts
// are analytic 0/1, not >0; pixel bytes are round(color*255) as in the draw-cache
// gate (0.2,0.4,0.8 -> 51,102,204).

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/re_context.hpp"
#include "core/texture2d.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/offscreen_context.hpp"
#include "utils/pixel_reader.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace re::tests {

// ---------------------------------------------------------------------------
// (1) Cross-pass dedup: 2 layers sharing REContext::current() issue 1 glViewport.
// ---------------------------------------------------------------------------

TEST(T2REContext, CrossPassDedupOneViewport) {
    auto& ctx = core::REContext::current();
    ctx.invalidate();
    EXPECT_EQ(ctx.getSpyCounts().viewport, 0) << "after invalidate spy 0 (explainable cold)";

    // First layer sets viewport 0,0,64,64 -> 1 glViewport.
    ctx.setViewport(0, 0, 64, 64);
    EXPECT_EQ(ctx.getSpyCounts().viewport, 1) << "first setViewport must be 1 glViewport";

    // Second layer (compositor's second View, same GL context) sets same rect -> cache hit, still 1.
    ctx.setViewport(0, 0, 64, 64);
    EXPECT_EQ(ctx.getSpyCounts().viewport, 1)
        << "duplicate setViewport via same REContext::current() must stay 1 (cross-pass dedup, 2 layers sharing state issue 1 glViewport, not 2)";

    // Different rect is a miss -> 2.
    ctx.setViewport(0, 0, 128, 128);
    EXPECT_EQ(ctx.getSpyCounts().viewport, 2) << "distinct viewport must be 2 glViewport (miss)";

    // Invalidate is explicit at boundaries (SampleHarness post-ImGui, test public).
    ctx.invalidate();
    EXPECT_EQ(ctx.getSpyCounts().viewport, 0) << "invalidate must reset spy to 0";
    ctx.setViewport(0, 0, 64, 64);
    EXPECT_EQ(ctx.getSpyCounts().viewport, 1) << "after invalidate same rect must re-issue 1";
}

// ---------------------------------------------------------------------------
// (2) REContext::current() switches correctly after makeContextCurrent to second context.
// ---------------------------------------------------------------------------

TEST(T2REContext, CurrentSwitchesAfterMakeCurrent) {
    // Save original fixture context handle for restore.
    auto* fixture = OffscreenEnvironment::context();
    ASSERT_NE(fixture, nullptr) << "offscreen fixture must exist";
    // Ensure fixture's REContext is current before starting (the fixture made it current at startup).
    // Explicitly sync to fixture's window if it's GLFW.
    if (fixture->backend() == utils::ContextBackend::Glfw && fixture->glfwHandle() != nullptr) {
        core::REContext::setCurrentWindow(fixture->glfwHandle());
        glfwMakeContextCurrent(fixture->glfwHandle());
    }

    auto& ctx1 = core::REContext::current();
    ctx1.invalidate();
    ctx1.setViewport(0, 0, 64, 64);
    EXPECT_EQ(ctx1.getSpyCounts().viewport, 1) << "fixture ctx first viewport 1";

    // Create second offscreen context (GLFW hidden window).
    auto secondRes = utils::OffscreenContext::create();
    ASSERT_TRUE(secondRes.ok()) << secondRes.error().message;
    utils::OffscreenContext second = std::move(*secondRes);
    // second is now current (create makes it current); its REContext should be cold.
    auto& ctx2 = core::REContext::current();
    EXPECT_EQ(ctx2.getSpyCounts().viewport, 0)
        << "fresh second context must start cold (0 viewport calls, no cross-context bleed, explainable 0)";

    ctx2.setViewport(0, 0, 128, 128);
    EXPECT_EQ(ctx2.getSpyCounts().viewport, 1) << "second ctx distinct rect 1";

    // Switch back to fixture's context via explicit makeCurrent.
    if (fixture->backend() == utils::ContextBackend::Glfw && fixture->glfwHandle() != nullptr) {
        fixture->makeCurrent();
        auto& ctx1Again = core::REContext::current();
        // ctx1Again should retain its previous cache (1 viewport for 64x64), not reset to 0 nor incremented.
        EXPECT_EQ(ctx1Again.getSpyCounts().viewport, 1)
            << "switching back must restore first context's mirror (1, not 0 or 2, per-GL-context isolation)";
        // Duplicate on restored context is still a hit.
        ctx1Again.setViewport(0, 0, 64, 64);
        EXPECT_EQ(ctx1Again.getSpyCounts().viewport, 1)
            << "duplicate after switch back still 1 (cache hit, no bleed)";
        // Different rect on restored context is a miss -> 2, proving its cache is intact and distinct from second's.
        ctx1Again.setViewport(0, 0, 32, 32);
        EXPECT_EQ(ctx1Again.getSpyCounts().viewport, 2)
            << "distinct rect on restored fixture must be 2 (miss, proves per-context mirror preserved)";
    }

    // Restore fixture as current for subsequent tests.
    if (fixture) {
        fixture->makeCurrent();
    }
    // No need to keep second alive beyond test (RAII will destroy it and clear its map entry).
}

// ---------------------------------------------------------------------------
// (3) Regression R3 byte-identical: clear via REContext still 51,102,204 within 1/255.
// ---------------------------------------------------------------------------

TEST(T2REContext, RegressionByteIdenticalClear) {
    constexpr uint32_t kW = 64u, kH = 64u;
    // Create an offscreen FBO target.
    auto color = core::Texture2D::create();
    auto fb = core::Framebuffer::create();
    ASSERT_TRUE(color.ok()) << color.error().message;
    ASSERT_TRUE(fb.ok()) << fb.error().message;
    std::vector<uint8_t> zeros(static_cast<size_t>(kW) * kH * 4u, 0u);
    color->bind(0u);
    color->upload(kW, kH, zeros.data());
    color->unbind(0u);
    fb->bind();
    fb->attachColor(*color);
    ASSERT_TRUE(fb->isComplete());
    fb->unbind();

    auto& ctx = core::REContext::current();
    ctx.invalidate();
    // Use the global per-GL-context REContext (formerly per-frame local ctx) to clear.
    // This proves the rename and per-context mirror did not change output (regression R3).
    fb->bind();
    ctx.setViewport(0, 0, static_cast<int>(kW), static_cast<int>(kH));
    ctx.setClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    ctx.clearColor();
    // Spy must show exactly 1 viewport and 1 clearColor for this pass.
    EXPECT_EQ(ctx.getSpyCounts().viewport, 1) << "clear pass viewport 1 (explainable)";
    EXPECT_EQ(ctx.getSpyCounts().clearColor, 1) << "clearColor 1 (explainable)";

    std::vector<uint8_t> pixels;
    utils::PixelReader reader;
    auto r = reader.read(kW / 2u, kH / 2u, 1u, 1u, pixels);
    ASSERT_TRUE(r.ok()) << r.error().message;
    ASSERT_EQ(pixels.size(), 4u);
    // Analytic: 0.2*255=51, 0.4*255=102, 0.8*255=204, alpha 255.
    constexpr int kTol = 1;
    EXPECT_NEAR(pixels[0], 51u, kTol) << "R byte-identical 51 (0.2*255)";
    EXPECT_NEAR(pixels[1], 102u, kTol) << "G 102 (0.4*255)";
    EXPECT_NEAR(pixels[2], 204u, kTol) << "B 204 (0.8*255)";
    EXPECT_EQ(pixels[3], 255u) << "A 255";
    fb->unbind();
    EXPECT_FALSE(core::hasPendingGlError());
}

} // namespace re::tests
