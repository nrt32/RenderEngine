// tests/t2_v2_multiview_test.cpp — V2 T2 gate tests (SPEC §9 V2.4, Model B:
// per-view FBO + engine blit).
//
// The V2.4 deliverable: per-view core::Framebuffers owned by the engine
// compositor (render::ViewRenderer), the shared View/ViewRect window-section
// handles + abstract scene objects, IRenderer dispatch (SPEC §9 V2.3), and the
// new core::blit (glBlitFramebuffer under core/) that presents each view's FBO
// into its pinned window rect. This file verifies
//
//   (1) the pinned 2-view layout of a 1280x480 window: View A = (0, 0, 640,
//       480), View B = (640, 0, 640, 480) — the two rects exactly tile the
//       window;
//   (2) renderViews() renders each view's scene into its OWN 640x480 FBO,
//       dispatched through the IRenderer of the scene's technique (View A =
//       MeshScene golden quad, View B = PlaneScene solid image), verified by
//       reading back each FBO's center pixel (320, 240);
//   (3) present() blits each FBO into its pinned window rect — the window's
//       center pixels (A: (320, 240), B: (960, 240)) match that view's scene's
//       expected color within 1/255, and the boundary pixels (639, 240) /
//       (640, 240) pin the split exactly at x = 640 (no app-side viewport
//       blending: the blit is the whole present);
//   (4) a view whose scene holds a technique with no registered renderer is
//       rejected with a typed error (code 2), a view-count mismatch is
//       rejected with a typed error (code 1), and a present() call before the
//       first renderViews is rejected with a typed error (code 3) — SPEC §5,
//       no exceptions.
//
// Analytic setup — View A's scene is the FR-render.1 golden +Z-facing quad
// covering [-1,1]^2 at z=0 under the orthographic camera mapping NDC [-1,1]^2
// onto the full 640x480 viewport, so the FBO center pixel (320, 240) is the
// material's base color {0.2, 0.4, 0.8} -> bytes {51, 102, 204}. View B's
// scene is a 640x480 solid image of straight RGBA {0.9, 0.1, 0.3} -> bytes
// {round(0.9*255)=230, round(0.1*255)=26, round(0.3*255)=77} texturing the
// full-viewport unit quad, so its FBO center pixel is exactly {230, 26, 77}.
// The blit is 1:1 (FBO size == rect size, GL_NEAREST), so each view's FBO
// content lands pixel-for-pixel in its rect: window (320, 240) = view A's FBO
// (320, 240), window (960, 240) = view B's FBO (320, 240).
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including core::readRgba8 for pixel readback and core::blit via
// render::ViewRenderer) — no raw glXxx calls.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/read_pixels.hpp"
#include "core/texture2d.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/plane_renderer.hpp"
#include "render/types.hpp"
#include "render/view_renderer.hpp"
#include "render/volume_renderer.hpp"
#include "tests/offscreen_fixture.hpp"
#include "volume/color.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (SPEC §9 V2.4 gate).
// ---------------------------------------------------------------------------

// The 2-view window: 1280x480, split into two 640x480 rects.
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 480;
constexpr std::uint32_t kViewWidth = 640u;
constexpr std::uint32_t kViewHeight = 480u;

// The pinned ViewRects (the task's layout): A = (0, 0, 640, 480),
// B = (640, 0, 640, 480).
constexpr render::ViewRect kViewARect{0, 0, 640, 480};
constexpr render::ViewRect kViewBRect{640, 0, 640, 480};

// The center of each 640x480 view in its FBO's pixel space.
constexpr std::uint32_t kFboCenterX = kViewWidth / 2u;  // 320
constexpr std::uint32_t kFboCenterY = kViewHeight / 2u; // 240

// The center pixel of each view in the WINDOW's pixel space (after the blit):
// A: (320, 240), B: (960, 240).
constexpr std::uint32_t kWindowCenterAX = 320u;
constexpr std::uint32_t kWindowCenterBX = 960u;
constexpr std::uint32_t kWindowCenterY = 240u;

// View A's expected color: the FR-render.1 base color bytes {51, 102, 204}.
constexpr glm::vec4 kViewABaseColor(0.2f, 0.4f, 0.8f, 1.0f);
constexpr std::uint8_t kExpectedAR = 51u;
constexpr std::uint8_t kExpectedAG = 102u;
constexpr std::uint8_t kExpectedAB = 204u;

// View B's expected color: straight RGBA {0.9, 0.1, 0.3} -> bytes
// {round(0.9*255)=230, round(0.1*255)=26, round(0.3*255)=77}.
constexpr glm::vec4 kViewBBaseColor(0.9f, 0.1f, 0.3f, 1.0f);
constexpr std::uint8_t kExpectedBR = 230u;
constexpr std::uint8_t kExpectedBG = 26u;
constexpr std::uint8_t kExpectedBB = 77u;

// The color tolerance: 1/255 per the SPEC §4 tolerances.
constexpr int kColorTolerance = 1;

// Typed error codes (render/view_renderer.cpp): 1 = view count mismatch,
// 2 = no renderer registered for the scene's technique, 3 = present() before
// the first renderViews (per-view FBOs not created).
constexpr int kViewCountMismatchErrorCode = 1;
constexpr int kNoRendererErrorCode = 2;
constexpr int kFboNotCreatedErrorCode = 3;

// ---------------------------------------------------------------------------
// Test helpers.
// ---------------------------------------------------------------------------

/// Build a golden +Z-facing quad mesh covering [-1,1]^2 at z=0 (two triangles),
/// the FR-render.1 golden scene of View A.
data::Mesh makeQuadMesh() {
    std::vector<glm::vec3> positions = {
        glm::vec3(-1.0f, -1.0f, 0.0f), // v0
        glm::vec3(1.0f, -1.0f, 0.0f),  // v1
        glm::vec3(1.0f, 1.0f, 0.0f),   // v2
        glm::vec3(-1.0f, 1.0f, 0.0f),  // v3
    };
    std::vector<std::uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
    return data::Mesh::fromTriangles(std::move(positions), std::move(indices));
}

/// Build a 640x480 solid image of View B's color bytes {230, 26, 77, 255}
/// (the quad maps 1:1 onto the viewport, so every pixel samples the solid
/// texel).
data::Image makeSolidImage() {
    std::vector<std::uint8_t> bytes(
        static_cast<std::size_t>(kViewWidth) * kViewHeight * 4u, 0u);
    for (std::size_t i = 0u; i + 3u < bytes.size(); i += 4u) {
        bytes[i + 0u] = kExpectedBR;
        bytes[i + 1u] = kExpectedBG;
        bytes[i + 2u] = kExpectedBB;
        bytes[i + 3u] = 255u;
    }
    return data::Image(static_cast<int>(kViewWidth),
                       static_cast<int>(kViewHeight), 4, std::move(bytes));
}

/// The default camera: eye at (0,0,5) looking down -Z at the origin,
/// orthographic projection mapping NDC [-1,1]^2 onto the full viewport.
render::Camera makeCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

/// Build the 1280x480 "window" framebuffer the ViewRenderer presents into:
/// a color-only FBO whose texture is the window's pixel surface.
struct WindowTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;
    WindowTarget(core::Texture2D c, core::Framebuffer f)
        : color(std::move(c)), framebuffer(std::move(f)) {}
};

WindowTarget makeWindow() {
    auto color = core::Texture2D::create();
    auto framebuffer = core::Framebuffer::create();
    EXPECT_TRUE(color.ok()) << color.error().message;
    EXPECT_TRUE(framebuffer.ok()) << framebuffer.error().message;
    std::vector<std::uint8_t> zeros(
        static_cast<std::size_t>(kWindowWidth) * kWindowHeight * 4u, 0u);
    color->bind(0u);
    color->upload(static_cast<std::uint32_t>(kWindowWidth),
                  static_cast<std::uint32_t>(kWindowHeight), zeros.data());
    color->unbind(0u);
    framebuffer->bind();
    framebuffer->attachColor(*color);
    EXPECT_TRUE(framebuffer->isComplete());
    framebuffer->unbind();
    return WindowTarget(std::move(*color), std::move(*framebuffer));
}

/// Read the single RGBA8 pixel at (x, y) of `framebuffer` (binding it first;
/// y = 0 is the bottom scanline). The framebuffer is left unbound.
std::vector<std::uint8_t> readPixel(core::Framebuffer& framebuffer,
                                    std::uint32_t x, std::uint32_t y) {
    framebuffer.bind();
    std::vector<std::uint8_t> pixels;
    auto read = core::readRgba8(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    framebuffer.unbind();
    return pixels;
}

/// Assert the RGBA bytes of `pixel` equal the expected View A bytes.
void expectViewAColor(const std::vector<std::uint8_t>& pixel,
                      const char* where) {
    EXPECT_NEAR(pixel[0], kExpectedAR, kColorTolerance) << "R at " << where;
    EXPECT_NEAR(pixel[1], kExpectedAG, kColorTolerance) << "G at " << where;
    EXPECT_NEAR(pixel[2], kExpectedAB, kColorTolerance) << "B at " << where;
    EXPECT_EQ(pixel[3], 255u) << "A at " << where;
}

/// Assert the RGBA bytes of `pixel` equal the expected View B bytes.
void expectViewBColor(const std::vector<std::uint8_t>& pixel,
                      const char* where) {
    EXPECT_NEAR(pixel[0], kExpectedBR, kColorTolerance) << "R at " << where;
    EXPECT_NEAR(pixel[1], kExpectedBG, kColorTolerance) << "G at " << where;
    EXPECT_NEAR(pixel[2], kExpectedBB, kColorTolerance) << "B at " << where;
    EXPECT_EQ(pixel[3], 255u) << "A at " << where;
}

// ---------------------------------------------------------------------------
// The shared 2-view fixture: the golden mesh scene (View A), the solid plane
// scene (View B), the two technique renderers, and the engine compositor.
// ---------------------------------------------------------------------------
struct TwoViewFixture {
    render::PhongMaterial materialA{kViewABaseColor};
    data::Mesh quadA{makeQuadMesh()};
    render::MeshScene sceneA;

    data::Image imageB{makeSolidImage()};
    render::PlaneGeometry quadB{render::PlaneGeometry::unitQuadXY()};
    render::PlaneScene sceneB;

    render::MeshRenderer meshRenderer;
    render::PlaneRenderer planeRenderer;
    render::ViewRenderer composer{2u, kViewWidth, kViewHeight};

    TwoViewFixture() {
        sceneA.meshes.push_back(
            render::MeshInstance{&quadA, &materialA, glm::mat4(1.0f)});
        sceneB.planes.push_back(
            render::PlaneInstance{&quadB, &imageB, glm::mat4(1.0f)});
        composer.setRenderer(render::SceneKind::Mesh, &meshRenderer);
        composer.setRenderer(render::SceneKind::Plane, &planeRenderer);
    }

    /// The two views with their pinned rects.
    std::vector<render::View> views() const {
        std::vector<render::View> views(2u);
        views[0].scene = &sceneA;
        views[0].camera = makeCamera();
        views[0].clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        views[0].rect = kViewARect;
        views[1].scene = &sceneB;
        views[1].camera = makeCamera();
        views[1].clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
        views[1].rect = kViewBRect;
        return views;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// (1) The pinned 2-view layout: the two rects exactly tile the 1280x480
//     window.
// ---------------------------------------------------------------------------

TEST(T2V2MultiView, PinnedViewRectsTileWindow) {
    // View A = (0, 0, 640, 480), View B = (640, 0, 640, 480).
    EXPECT_EQ(kViewARect.x, 0);
    EXPECT_EQ(kViewARect.y, 0);
    EXPECT_EQ(kViewARect.width, 640);
    EXPECT_EQ(kViewARect.height, 480);
    EXPECT_EQ(kViewBRect.x, 640);
    EXPECT_EQ(kViewBRect.y, 0);
    EXPECT_EQ(kViewBRect.width, 640);
    EXPECT_EQ(kViewBRect.height, 480);

    // The two rects exactly tile the window: widths sum to 1280, heights to
    // 480, and there is no vertical gap.
    EXPECT_EQ(kViewARect.width + kViewBRect.width, kWindowWidth);
    EXPECT_EQ(kViewARect.height, kWindowHeight);
    EXPECT_EQ(kViewBRect.height, kWindowHeight);
}

// ---------------------------------------------------------------------------
// (2) renderViews(): each view's scene renders into its OWN 640x480 FBO
//     (dispatched through the IRenderer of the scene's technique).
// ---------------------------------------------------------------------------

TEST(T2V2MultiView, TwoViewsRenderIntoTheirOwnFbos) {
    TwoViewFixture fixture;
    const std::vector<render::View> views = fixture.views();

    auto result = fixture.composer.renderViews(views);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    // View A's FBO center pixel: the golden quad's base color {51, 102, 204}.
    ASSERT_NE(fixture.composer.viewFramebuffer(0u), nullptr);
    expectViewAColor(readPixel(*fixture.composer.viewFramebuffer(0u),
                               kFboCenterX, kFboCenterY),
                     "view A FBO center (320, 240)");

    // View B's FBO center pixel: the solid image's color {230, 26, 77}.
    ASSERT_NE(fixture.composer.viewFramebuffer(1u), nullptr);
    expectViewBColor(readPixel(*fixture.composer.viewFramebuffer(1u),
                               kFboCenterX, kFboCenterY),
                     "view B FBO center (320, 240)");

    // The two FBOs are distinct objects (per-view framebuffers).
    EXPECT_NE(fixture.composer.viewFramebuffer(0u),
              fixture.composer.viewFramebuffer(1u));
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (3) present(): the blit places each view's content in its pinned window
//     rect — center pixels (A: (320, 240), B: (960, 240)) and the exact
//     x = 640 split.
// ---------------------------------------------------------------------------

TEST(T2V2MultiView, BlitPlacesViewsInPinnedWindowRects) {
    TwoViewFixture fixture;
    WindowTarget window = makeWindow();
    const std::vector<render::View> views = fixture.views();

    auto result = fixture.composer.render(views, &window.framebuffer);
    ASSERT_TRUE(result.ok()) << result.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    // View A's center: window pixel (320, 240) = A's FBO (320, 240).
    expectViewAColor(
        readPixel(window.framebuffer, kWindowCenterAX, kWindowCenterY),
        "window view A center (320, 240)");

    // View B's center: window pixel (960, 240) = B's FBO (320, 240) shifted by
    // the rect origin (640, 0).
    expectViewBColor(
        readPixel(window.framebuffer, kWindowCenterBX, kWindowCenterY),
        "window view B center (960, 240)");

    // The split is pinned exactly at x = 640: the last pixel of rect A is A's
    // color, the first pixel of rect B is B's color.
    expectViewAColor(readPixel(window.framebuffer, 639u, kWindowCenterY),
                     "window rect boundary left (639, 240)");
    expectViewBColor(readPixel(window.framebuffer, 640u, kWindowCenterY),
                     "window rect boundary right (640, 240)");
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (4) Typed errors: an unregistered scene technique and a view-count mismatch
//     are rejected (SPEC §5, no exceptions).
// ---------------------------------------------------------------------------

TEST(T2V2MultiView, UnregisteredTechniqueRejected) {
    TwoViewFixture fixture;
    // A VolumeScene (technique 2) with no registered Volume renderer.
    render::VolumeScene volumeScene;
    std::vector<render::View> views = fixture.views();
    views[1].scene = &volumeScene; // replace View B's scene

    const data::Result<void> result = fixture.composer.renderViews(views);
    EXPECT_TRUE(result.failed());
    EXPECT_EQ(result.error().code, kNoRendererErrorCode);
    EXPECT_NE(result.error().message.find("no renderer registered"),
              std::string::npos);
}

TEST(T2V2MultiView, ViewCountMismatchRejected) {
    TwoViewFixture fixture;
    // Only ONE view against a 2-view compositor.
    std::vector<render::View> views = fixture.views();
    views.resize(1u);

    const data::Result<void> result = fixture.composer.renderViews(views);
    EXPECT_TRUE(result.failed());
    EXPECT_EQ(result.error().code, kViewCountMismatchErrorCode);
    EXPECT_NE(result.error().message.find("view count mismatch"),
              std::string::npos);
}

TEST(T2V2MultiView, PresentBeforeRenderViewsRejected) {
    TwoViewFixture fixture;
    const std::vector<render::View> views = fixture.views();

    // present() blits what renderViews() rendered into the per-view FBOs: on a
    // fresh compositor (renderViews never called) it must be rejected with a
    // typed error (code 3) instead of blitting unrendered targets (SPEC §5, no
    // exceptions). destination nullptr is never reached — the rejection aborts
    // before any blit.
    const data::Result<void> result = fixture.composer.present(views, nullptr);
    EXPECT_TRUE(result.failed());
    EXPECT_EQ(result.error().code, kFboNotCreatedErrorCode);
    EXPECT_NE(result.error().message.find("not created"), std::string::npos);
}

} // namespace re::tests