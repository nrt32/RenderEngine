// tests/t2_v2_multiview_test.cpp — V2 T2 gate (migrated to T5 ReView).
//
// V2 T2 originally tested render::ViewRenderer (per-view FBO + engine blit).
// T5 deletes ViewRenderer and replaces it with render::View (ReView) per screen
// section + ViewTarget + IRenderable + DrawContext + core::blit. This file is
// migrated to verify the SAME pinned constants via ReView so the regression lock
// (R3) holds: the 2-view 1280×480 window, View A mesh {51,102,204} and View B
// plane {230,26,77} centers within 1/255, and window blit positions 320,240 /
// 960,240 + boundary 639/640. Per GL-ownership + readback guardrails this file
// uses ONLY core/ wrappers (utils::PixelReader via core::readRgba8) — no raw gl.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>
#include <utility>
#include <vector>

#include "core/draw.hpp"
#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/texture2d.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/plane_renderer.hpp"
#include "render/types.hpp"
#include "render/view.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {
namespace {

constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 480;
constexpr std::uint32_t kViewWidth = 640u;
constexpr std::uint32_t kViewHeight = 480u;

constexpr render::ViewRect kViewARect{0, 0, 640, 480};
constexpr render::ViewRect kViewBRect{640, 0, 640, 480};

constexpr std::uint32_t kFboCenterX = kViewWidth / 2u;
constexpr std::uint32_t kFboCenterY = kViewHeight / 2u;
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

struct TwoViewFixture {
    // Shared handles (registry/material/geometry/image): the renderer and the
    // scene instances CO-OWN everything they reference, so teardown order
    // between renderer, scene, and fixture members cannot dangle anything.
    std::shared_ptr<render::AssetRegistry> registry{
        std::make_shared<render::AssetRegistry>()};
    std::shared_ptr<render::PhongMaterial> materialA{
        std::make_shared<render::PhongMaterial>(kViewABaseColor)};
    data::Mesh quadA{makeQuadMesh()};
    render::MeshScene sceneA;
    std::shared_ptr<data::Image> imageB{
        std::make_shared<data::Image>(makeSolidImage())};
    std::shared_ptr<const render::PlaneGeometry> quadB{
        std::make_shared<const render::PlaneGeometry>(
            render::PlaneGeometry::unitQuadXY())};
    render::PlaneScene sceneB;
    std::shared_ptr<render::MeshRenderer> meshRenderer{
        std::make_shared<render::MeshRenderer>(registry)};
    std::shared_ptr<render::PlaneRenderer> planeRenderer{
        std::make_shared<render::PlaneRenderer>()};
    render::Camera camera{makeCamera()};

    TwoViewFixture() {
        const auto handle = registry->registerAsset(quadA);
        EXPECT_TRUE(handle.ok()) << handle.error().message;
        if (handle.ok()) {
            sceneA.meshes.push_back(render::MeshInstance{*handle, materialA, glm::mat4(1.0f)});
        }
        sceneB.planes.push_back(render::PlaneInstance{quadB, imageB, glm::mat4(1.0f)});
    }
};

} // namespace

TEST(T2V2MultiView, PinnedViewRectsTileWindow) {
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

TEST(T2V2MultiView, TwoViewsRenderIntoTheirOwnFbos) {
    TwoViewFixture f;
    render::View viewA(kViewARect, glm::vec4(0, 0, 0, 0));
    render::View viewB(kViewBRect, glm::vec4(0, 0, 0, 0));
    viewA.setCamera(f.camera);
    viewB.setCamera(f.camera);
    viewA.addItem(f.sceneA, f.meshRenderer);
    viewB.addItem(f.sceneB, f.planeRenderer);

    core::DrawContext ctxA, ctxB;
    ASSERT_TRUE(viewA.ensureTarget().ok());
    ASSERT_TRUE(viewB.ensureTarget().ok());
    ASSERT_TRUE(viewA.render(ctxA).ok());
    ASSERT_TRUE(viewB.render(ctxB).ok());
    EXPECT_FALSE(core::hasPendingGlError());

    ASSERT_NE(viewA.target(), nullptr);
    expectViewAColor(readPixel(viewA.target()->framebuffer(), kFboCenterX, kFboCenterY), "view A ViewTarget center 320,240");
    ASSERT_NE(viewB.target(), nullptr);
    expectViewBColor(readPixel(viewB.target()->framebuffer(), kFboCenterX, kFboCenterY), "view B ViewTarget center 320,240");
    EXPECT_NE(&viewA.target()->framebuffer(), &viewB.target()->framebuffer());
    EXPECT_FALSE(core::hasPendingGlError());
}

TEST(T2V2MultiView, BlitPlacesViewsInPinnedWindowRects) {
    TwoViewFixture f;
    WindowTarget window = makeWindow();
    render::View viewA(kViewARect, glm::vec4(0, 0, 0, 0));
    render::View viewB(kViewBRect, glm::vec4(0, 0, 0, 0));
    viewA.setCamera(f.camera);
    viewB.setCamera(f.camera);
    viewA.addItem(f.sceneA, f.meshRenderer);
    viewB.addItem(f.sceneB, f.planeRenderer);

    core::DrawContext ctxA, ctxB;
    ASSERT_TRUE(viewA.renderWithEnsure(ctxA).ok());
    ASSERT_TRUE(viewB.renderWithEnsure(ctxB).ok());
    ASSERT_TRUE(viewA.blitTo(&window.framebuffer).ok());
    ASSERT_TRUE(viewB.blitTo(&window.framebuffer).ok());
    EXPECT_FALSE(core::hasPendingGlError());

    expectViewAColor(readPixel(window.framebuffer, kWindowCenterAX, kWindowCenterY), "window view A center 320,240");
    expectViewBColor(readPixel(window.framebuffer, kWindowCenterBX, kWindowCenterY), "window view B center 960,240");
    expectViewAColor(readPixel(window.framebuffer, 639u, kWindowCenterY), "window rect boundary left 639,240");
    expectViewBColor(readPixel(window.framebuffer, 640u, kWindowCenterY), "window rect boundary right 640,240");
    EXPECT_FALSE(core::hasPendingGlError());
}

// Migrated error paths: View::render without ensureTarget and blit before ensure.
TEST(T2V2MultiView, UnregisteredTechniqueRejected) {
    // Via ReView the error is now View::render without target (code 2) — the
    // typed error path is preserved (SPEC §5, no exceptions). The original
    // ViewRenderer unregistered-techn technique test is superseded by View's
    // own typed errors, but we keep a failing render path to prove error handling.
    render::View view(kViewARect);
    view.setCamera(makeCamera());
    core::DrawContext ctx;
    auto r = view.render(ctx);
    EXPECT_TRUE(r.failed());
    EXPECT_EQ(r.error().code, 2);
}

TEST(T2V2MultiView, ViewCountMismatchRejected) {
    // ReView is per-section — view count mismatch is no longer a ViewRenderer
    // concept. Verify that an invalid rect is rejected (code 1) as the new
    // comparable typed error (SPEC §5).
    render::View view(render::ViewRect{0, 0, 0, 0});
    auto e = view.ensureTarget();
    EXPECT_TRUE(e.failed());
    EXPECT_EQ(e.error().code, 1);
}

TEST(T2V2MultiView, PresentBeforeRenderViewsRejected) {
    render::View view(kViewARect);
    view.setCamera(makeCamera());
    // No ensureTarget/render yet — blit must be rejected code 3.
    WindowTarget window = makeWindow();
    auto b = view.blitTo(&window.framebuffer);
    EXPECT_TRUE(b.failed());
    EXPECT_EQ(b.error().code, 3);
    EXPECT_NE(b.error().message.find("not created"), std::string::npos);
}

} // namespace re::tests
