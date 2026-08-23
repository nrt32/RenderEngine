// tests/t5_view_test.cpp — T5 gate: ReView per screen section + IRenderable + ViewTarget + core::blit (SPEC §3.2 V3.4).
//
// Same pinned 2-view 1280×480 window as V2 T2, now via render::View (ReView) +
// ViewTarget + IRenderable type-erased drawLayer + DrawContext + core::blit.
// Verifies:
//   (1) pinned rects (0,0,640,480) / (640,0,640,480) tile 1280×480;
//   (2) each View's ViewTarget (640×480) center 320,240 matches scene color
//       within 1/255 (View A mesh {51,102,204}, View B plane {230,26,77});
//   (3) window pixels 320,240 / 960,240 / boundary 639/640 match after blit;
//   (4) View never knows renderer (type-erased drawLayer), renderers expose
//       drawLayer assuming ReView already bind+viewport+clear (single-item
//       render() keeps clear for direct tests);
//   (5) error paths: render without ensureTarget code 2, blitTo before ensure code 3;
//   (6) utils::PixelReader path unchanged (delegates to core::readRgba8).
// All asserts are explainable constants (analytic bytes, within 1/255).

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <vector>

#include "core/draw.hpp"
#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/plane_renderer.hpp"
#include "render/types.hpp"
#include "render/view.hpp"
#include "render/view_target.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (T5 gate, same as V2 T2 via ReView).
// ---------------------------------------------------------------------------
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 480;
constexpr std::uint32_t kViewWidth = 640u;
constexpr std::uint32_t kViewHeight = 480u;

constexpr render::ViewRect kViewARect{0, 0, 640, 480};
constexpr render::ViewRect kViewBRect{640, 0, 640, 480};

constexpr std::uint32_t kFboCenterX = kViewWidth / 2u;  // 320
constexpr std::uint32_t kFboCenterY = kViewHeight / 2u; // 240
constexpr std::uint32_t kWindowCenterAX = 320u;
constexpr std::uint32_t kWindowCenterBX = 960u;
constexpr std::uint32_t kWindowCenterY = 240u;

constexpr glm::vec4 kViewABaseColor(0.2f, 0.4f, 0.8f, 1.0f);
constexpr std::uint8_t kExpectedAR = 51u;
constexpr std::uint8_t kExpectedAG = 102u;
constexpr std::uint8_t kExpectedAB = 204u;

constexpr glm::vec4 kViewBBaseColor(0.9f, 0.1f, 0.3f, 1.0f);
constexpr std::uint8_t kExpectedBR = 230u;
constexpr std::uint8_t kExpectedBG = 26u;
constexpr std::uint8_t kExpectedBB = 77u;

constexpr int kColorTolerance = 1;

// Error codes from render/view.cpp: 1 invalid rect, 2 target not created (render), 3 blit before ensure.
constexpr int kInvalidRectCode = 1;
constexpr int kRenderNoTargetCode = 2;
constexpr int kBlitNoTargetCode = 3;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
data::Mesh makeQuadMesh() {
    std::vector<glm::vec3> positions = {
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
    };
    std::vector<std::uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
    return data::Mesh::fromTriangles(std::move(positions), std::move(indices));
}

data::Image makeSolidImage() {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kViewWidth) * kViewHeight * 4u, 0u);
    for (std::size_t i = 0u; i + 3u < bytes.size(); i += 4u) {
        bytes[i + 0u] = kExpectedBR;
        bytes[i + 1u] = kExpectedBG;
        bytes[i + 2u] = kExpectedBB;
        bytes[i + 3u] = 255u;
    }
    return data::Image(static_cast<int>(kViewWidth), static_cast<int>(kViewHeight), 4, std::move(bytes));
}

render::Camera makeCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

struct WindowTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;
    WindowTarget(core::Texture2D c, core::Framebuffer f) : color(std::move(c)), framebuffer(std::move(f)) {}
};

WindowTarget makeWindow() {
    auto color = core::Texture2D::create();
    auto framebuffer = core::Framebuffer::create();
    EXPECT_TRUE(color.ok()) << color.error().message;
    EXPECT_TRUE(framebuffer.ok()) << framebuffer.error().message;
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(kWindowWidth) * kWindowHeight * 4u, 0u);
    color->bind(0u);
    color->upload(static_cast<std::uint32_t>(kWindowWidth), static_cast<std::uint32_t>(kWindowHeight), zeros.data());
    color->unbind(0u);
    framebuffer->bind();
    framebuffer->attachColor(*color);
    EXPECT_TRUE(framebuffer->isComplete());
    framebuffer->unbind();
    return WindowTarget(std::move(*color), std::move(*framebuffer));
}

std::vector<std::uint8_t> readPixel(core::Framebuffer& framebuffer, std::uint32_t x, std::uint32_t y) {
    framebuffer.bind();
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    framebuffer.unbind();
    return pixels;
}

void expectViewAColor(const std::vector<std::uint8_t>& pixel, const char* where) {
    EXPECT_NEAR(pixel[0], kExpectedAR, kColorTolerance) << "R at " << where;
    EXPECT_NEAR(pixel[1], kExpectedAG, kColorTolerance) << "G at " << where;
    EXPECT_NEAR(pixel[2], kExpectedAB, kColorTolerance) << "B at " << where;
    EXPECT_EQ(pixel[3], 255u) << "A at " << where;
}

void expectViewBColor(const std::vector<std::uint8_t>& pixel, const char* where) {
    EXPECT_NEAR(pixel[0], kExpectedBR, kColorTolerance) << "R at " << where;
    EXPECT_NEAR(pixel[1], kExpectedBG, kColorTolerance) << "G at " << where;
    EXPECT_NEAR(pixel[2], kExpectedBB, kColorTolerance) << "B at " << where;
    EXPECT_EQ(pixel[3], 255u) << "A at " << where;
}

struct TwoViewReViewFixture {
    render::AssetRegistry registry;
    render::PhongMaterial materialA{kViewABaseColor};
    data::Mesh quadA{makeQuadMesh()};
    render::MeshScene sceneA;
    data::Image imageB{makeSolidImage()};
    render::PlaneGeometry quadB{render::PlaneGeometry::unitQuadXY()};
    render::PlaneScene sceneB;
    render::MeshRenderer meshRenderer{&registry};
    render::PlaneRenderer planeRenderer;
    render::Camera camera{makeCamera()};

    TwoViewReViewFixture() {
        const auto handle = registry.registerAsset(quadA);
        EXPECT_TRUE(handle.ok()) << handle.error().message;
        if (handle.ok()) {
            sceneA.meshes.push_back(render::MeshInstance{*handle, &materialA, glm::mat4(1.0f)});
        }
        sceneB.planes.push_back(render::PlaneInstance{&quadB, &imageB, glm::mat4(1.0f)});
    }
};

} // namespace

// ---------------------------------------------------------------------------
// (1) Pinned rects tile window (same constants as V2 T2, now via ReView).
// ---------------------------------------------------------------------------
TEST(T5View, PinnedViewRectsTileWindow) {
    EXPECT_EQ(kViewARect.x, 0);
    EXPECT_EQ(kViewARect.y, 0);
    EXPECT_EQ(kViewARect.width, 640);
    EXPECT_EQ(kViewARect.height, 480);
    EXPECT_EQ(kViewBRect.x, 640);
    EXPECT_EQ(kViewBRect.y, 0);
    EXPECT_EQ(kViewBRect.width, 640);
    EXPECT_EQ(kViewBRect.height, 480);
    EXPECT_EQ(kViewARect.width + kViewBRect.width, kWindowWidth);
    EXPECT_EQ(kViewARect.height, kWindowHeight);
    EXPECT_EQ(kViewBRect.height, kWindowHeight);
}

// ---------------------------------------------------------------------------
// (2) ReView ViewTarget owns Texture2D+Framebuffer sized rect.w×h; ensureTarget.
// ---------------------------------------------------------------------------
TEST(T5View, ViewTargetOwnedPerRect) {
    TwoViewReViewFixture f;
    render::View viewA(kViewARect, glm::vec4(0, 0, 0, 0));
    viewA.setCamera(f.camera);
    // Before ensureTarget, no ViewTarget
    EXPECT_EQ(viewA.target(), nullptr);
    auto e = viewA.ensureTarget();
    ASSERT_TRUE(e.ok()) << e.error().message;
    ASSERT_NE(viewA.target(), nullptr);
    EXPECT_EQ(viewA.target()->width(), kViewWidth);
    EXPECT_EQ(viewA.target()->height(), kViewHeight);
    EXPECT_TRUE(viewA.target()->valid());
    // ReView delegates FBO lifecycle to ViewTarget — View owns only view semantics.
    EXPECT_EQ(viewA.rect().width, 640);
}

TEST(T5View, ViewTargetResizeOnRectChange) {
    render::View view(render::ViewRect{0, 0, 640, 480});
    view.setCamera(makeCamera());
    ASSERT_TRUE(view.ensureTarget().ok());
    ASSERT_EQ(view.target()->width(), 640u);
    // Resize rect to 800×600 triggers ViewTarget recreate on next ensureTarget.
    view.setRect(render::ViewRect{0, 0, 800, 600});
    ASSERT_TRUE(view.ensureTarget().ok());
    EXPECT_EQ(view.target()->width(), 800u) << "rect.w 640→800 must recreate ViewTarget width 800 (explainable)";
    EXPECT_EQ(view.target()->height(), 600u);
}

// ---------------------------------------------------------------------------
// (3) ReView render via drawLayer: each View's FBO center matches scene color.
// ---------------------------------------------------------------------------
TEST(T5View, TwoViewsRenderIntoTheirOwnViewTargets) {
    TwoViewReViewFixture f;
    render::View viewA(kViewARect, glm::vec4(0, 0, 0, 0));
    render::View viewB(kViewBRect, glm::vec4(0, 0, 0, 0));
    viewA.setCamera(f.camera);
    viewB.setCamera(f.camera);
    // Heterogeneous list: View A = MeshSlice (here Mesh for gate), View B = Plane.
    // View never knows renderer — type-erased IRenderable.
    viewA.addItem(f.sceneA, &f.meshRenderer);
    viewB.addItem(f.sceneB, &f.planeRenderer);
    EXPECT_EQ(viewA.itemCount(), 1u);
    EXPECT_EQ(viewB.itemCount(), 1u);

    core::DrawContext ctxA, ctxB;
    ASSERT_TRUE(viewA.ensureTarget().ok());
    ASSERT_TRUE(viewB.ensureTarget().ok());
    auto rA = viewA.render(ctxA);
    ASSERT_TRUE(rA.ok()) << rA.error().message;
    auto rB = viewB.render(ctxB);
    ASSERT_TRUE(rB.ok()) << rB.error().message;

    // FBO center 320,240 must be scene color within 1/255 via utils::PixelReader (delegates to core::readRgba8).
    ASSERT_NE(viewA.target(), nullptr);
    expectViewAColor(readPixel(viewA.target()->framebuffer(), kFboCenterX, kFboCenterY), "viewA ViewTarget center 320,240");
    ASSERT_NE(viewB.target(), nullptr);
    expectViewBColor(readPixel(viewB.target()->framebuffer(), kFboCenterX, kFboCenterY), "viewB ViewTarget center 320,240");
    EXPECT_NE(&viewA.target()->framebuffer(), &viewB.target()->framebuffer()) << "distinct FBOs per View (explainable)";
}

// ---------------------------------------------------------------------------
// (4) Blit via core::blit places ViewTarget into window rect (1:1, GL_NEAREST).
// ---------------------------------------------------------------------------
TEST(T5View, BlitPlacesViewsInPinnedWindowRects) {
    TwoViewReViewFixture f;
    WindowTarget window = makeWindow();
    render::View viewA(kViewARect, glm::vec4(0, 0, 0, 0));
    render::View viewB(kViewBRect, glm::vec4(0, 0, 0, 0));
    viewA.setCamera(f.camera);
    viewB.setCamera(f.camera);
    viewA.addItem(f.sceneA, &f.meshRenderer);
    viewB.addItem(f.sceneB, &f.planeRenderer);

    core::DrawContext ctxA, ctxB;
    ASSERT_TRUE(viewA.renderWithEnsure(ctxA).ok());
    ASSERT_TRUE(viewB.renderWithEnsure(ctxB).ok());

    // Engine present: blit each ViewTarget into its pinned window rect.
    ASSERT_TRUE(viewA.blitTo(&window.framebuffer).ok());
    ASSERT_TRUE(viewB.blitTo(&window.framebuffer).ok());

    expectViewAColor(readPixel(window.framebuffer, kWindowCenterAX, kWindowCenterY), "window viewA center 320,240");
    expectViewBColor(readPixel(window.framebuffer, kWindowCenterBX, kWindowCenterY), "window viewB center 960,240");
    expectViewAColor(readPixel(window.framebuffer, 639u, kWindowCenterY), "boundary left 639,240");
    expectViewBColor(readPixel(window.framebuffer, 640u, kWindowCenterY), "boundary right 640,240");
}

// ---------------------------------------------------------------------------
// (5) View clipPlane: 2D vs 3D discrimination (optional<ClipPlane>).
// ---------------------------------------------------------------------------
TEST(T5View, ClipPlaneDiscriminates2Dvs3D) {
    render::View view2D(kViewARect);
    EXPECT_FALSE(view2D.is2D()) << "default nullopt is 3D (explainable)";
    EXPECT_FALSE(view2D.clipPlane().has_value());
    render::ClipPlane plane;
    plane.normal = glm::vec3(0, 0, 1);
    plane.point = glm::vec3(0, 0, 0);
    view2D.setClipPlane(plane);
    EXPECT_TRUE(view2D.is2D());
    ASSERT_TRUE(view2D.clipPlane().has_value());
    EXPECT_FLOAT_EQ(view2D.clipPlane()->normal.z, 1.0f);
    view2D.setClipPlane(std::nullopt);
    EXPECT_FALSE(view2D.is2D()) << "clearing plane returns to 3D";
}

// ---------------------------------------------------------------------------
// (6) Renderer drawLayer assumes already bind+viewport+clear; single-item render keeps clear.
// ---------------------------------------------------------------------------
TEST(T5View, RendererDrawLayerAssumesBoundCleared) {
    TwoViewReViewFixture f;
    // Direct single-item render() keeps clear for tests: renders mesh gold quad.
    render::ViewTarget target;
    {
        auto t = render::ViewTarget::create(64, 64);
        ASSERT_TRUE(t.ok()) << t.error().message;
        target = std::move(*t);
    }
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer();
    rt.width = 64;
    rt.height = 64;
    rt.clearColor = glm::vec4(0, 0, 0, 0);
    auto r = f.meshRenderer.render(f.sceneA, f.camera, rt);
    ASSERT_TRUE(r.ok()) << r.error().message;
    // Center pixel must be {51,102,204} within 1/255 (FR-render.1 analytic).
    {
        auto pix = readPixel(target.framebuffer(), 32, 32);
        EXPECT_NEAR(pix[0], kExpectedAR, kColorTolerance);
        EXPECT_NEAR(pix[1], kExpectedAG, kColorTolerance);
        EXPECT_NEAR(pix[2], kExpectedAB, kColorTolerance);
    }
    // Layer path via View::render + drawLayer must produce same center pixel
    // (ReView already bind+clear, drawLayer does not clear).
    render::View view(render::ViewRect{0, 0, 64, 64}, glm::vec4(0, 0, 0, 0));
    view.setCamera(f.camera);
    view.addItem(f.sceneA, &f.meshRenderer);
    core::DrawContext ctx;
    ASSERT_TRUE(view.renderWithEnsure(ctx).ok());
    auto pix2 = readPixel(view.target()->framebuffer(), 32, 32);
    EXPECT_NEAR(pix2[0], kExpectedAR, kColorTolerance) << "drawLayer center R 51 (explainable via baseColor 0.2*255)";
    EXPECT_NEAR(pix2[1], kExpectedAG, kColorTolerance);
    EXPECT_NEAR(pix2[2], kExpectedAB, kColorTolerance);
}

// ---------------------------------------------------------------------------
// (7) IRenderable type erasure: View never knows renderer.
// ---------------------------------------------------------------------------
TEST(T5View, IRenderableTypeErasure) {
    TwoViewReViewFixture f;
    render::View view(kViewARect);
    view.setCamera(f.camera);
    // Add Mesh and Plane to same View (heterogeneous list) — proves type erasure.
    view.addItem(f.sceneA, &f.meshRenderer);
    view.addItem(f.sceneB, &f.planeRenderer);
    EXPECT_EQ(view.itemCount(), 2u) << "heterogeneous 2 items (Mesh+Plane) via type erasure";
    core::DrawContext ctx;
    ASSERT_TRUE(view.renderWithEnsure(ctx).ok());
    // The second layer (plane solid {230,26,77}) overwrites the first where overlapping
    // (full-screen quad) — center must be plane color, proving both layers drew
    // without second clearing away first's FBO (only one clear by View).
    expectViewBColor(readPixel(view.target()->framebuffer(), kFboCenterX, kFboCenterY), "heterogeneous list second layer wins center");
}

// ---------------------------------------------------------------------------
// (8) Error paths: render without ensureTarget, blit before ensure.
// ---------------------------------------------------------------------------
TEST(T5View, RenderWithoutTargetIsError) {
    render::View view(kViewARect);
    view.setCamera(makeCamera());
    core::DrawContext ctx;
    auto r = view.render(ctx);
    EXPECT_TRUE(r.failed());
    EXPECT_EQ(r.error().code, kRenderNoTargetCode) << "render without target code 2 (explainable)";
}

TEST(T5View, BlitBeforeEnsureIsError) {
    render::View view(kViewARect);
    WindowTarget window = makeWindow();
    auto b = view.blitTo(&window.framebuffer);
    EXPECT_TRUE(b.failed());
    EXPECT_EQ(b.error().code, kBlitNoTargetCode) << "blit before ensure code 3 (explainable)";
}

// ---------------------------------------------------------------------------
// (9) DrawContext per-frame viewport cache: duplicate setViewport is 1 glViewport.
// ---------------------------------------------------------------------------
TEST(T5View, DrawContextViewportCachePerFrame) {
    core::DrawContext ctx;
    ctx.setViewport(0, 0, 640, 480);
    ctx.setViewport(0, 0, 640, 480);
    EXPECT_EQ(ctx.getSpyCounts().viewport, 1) << "duplicate viewport exactly 1 glViewport (cache hit, explainable)";
    core::DrawContext ctx2;
    ctx2.setViewport(0, 0, 640, 480);
    EXPECT_EQ(ctx2.getSpyCounts().viewport, 1) << "fresh DrawContext still 1 (instance isolation)";
}

} // namespace re::tests
