// tests/t8_render_plane_test.cpp — T8 gate tests (FR-render.5, SPEC §4).
//
// Asserts:
//   (1) the center pixel of a textured quad matches the source texture sample
//       within 1/255 (FR-render.5);
//   (2) the four corner pixels of a textured quad match the source texture
//       samples at the corresponding corner texels within 1/255 (FR-render.5);
//   (3) the plane's UV mapping and orientation are verifiable analytically —
//       (a) the unit quad's corner/UV binding and analytic normal
//           (PlaneGeometry::unitQuadXY), and (b) a rendered plane rotated 90°
//           about Z maps the texture to the analytically expected corner
//           (the image's top-left appears at the quad's top-left when viewed
//           from the normal's side).
//
// Analytic setup (docs/render.md): the textured pass samples the source
// texture with linear and clamp-to-edge (core::Texture2D defaults). The
// plane is the unit XY quad ([-1,1]^2 at z=0) with an identity model, and the
// camera uses an orthographic projection mapping NDC [-1,1]^2 onto the full
// 64x64 viewport, so the quad covers the viewport 1:1. The texture is made the
// SAME size as the viewport (64x64), so the pixel center at (px,py) samples
// the exact texel:
//
//   u = (px + 0.5) / 64,  s_u = u * 64 - 0.5 = px          (integer)
//   v = (py + 0.5) / 64,  s_v = v * 64 - 0.5 = py          (integer)
//
// so the rendered pixel reproduces the source texel exactly (frac = 0 under
// linear filtering), within the 1/255 tolerance.
//
// Orientation / vertical flip: the source image has a top-left origin
// (data::Image), while core::Texture2D expects row 0 = the BOTTOM scanline.
// PlaneRenderer flips the rows on upload (imageToRgba8), so the image's top
// row renders at the viewport's top when viewed from the normal's side.
// Consequently the viewport pixel (px,py) (py=0 is the bottom) samples image
// pixel (px, H-1-py). This is the analytic convention the corner checks below
// assert.
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including utils::PixelReader for pixel readback) — no raw glXxx calls.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>

#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <utility>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "utils/pixel_reader.hpp"
#include "core/texture2d.hpp"
#include "data/image.hpp"
#include "render/plane_renderer.hpp"
#include "render/types.hpp" // render::Camera / render::RenderTarget
#include "render/view.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants (FR-render.5).
// ---------------------------------------------------------------------------

// Target framebuffer and texture size: 64x64, so the quad covers the viewport
// 1:1 and each pixel samples an exact texel (see file comment).
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;
constexpr std::int32_t kImageWidth = 64;
constexpr std::int32_t kImageHeight = 64;

// The color tolerance: 1/255 per FR-render.5.
constexpr int kColorTolerance = 1;

// ---------------------------------------------------------------------------
// Test helpers.
// ---------------------------------------------------------------------------

/// The default camera: eye at (0,0,5) looking down -Z at the origin, with an
/// orthographic projection mapping NDC [-1,1]^2 onto the full viewport.
/// Build a render target (color-only FBO of `w` x `h`) bound for readback.
/// Kept for documentation / potential direct-target tests; View path now owns target via ensureTarget().
struct [[maybe_unused]] RenderedTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;

    RenderedTarget(core::Texture2D color, core::Framebuffer framebuffer)
        : color(std::move(color)), framebuffer(std::move(framebuffer)) {}
};

[[maybe_unused]] RenderedTarget makeTarget(std::uint32_t w, std::uint32_t h) {
    auto color = core::Texture2D::create();
    auto framebuffer = core::Framebuffer::create();
    EXPECT_TRUE(color.ok()) << color.error().message;
    EXPECT_TRUE(framebuffer.ok()) << framebuffer.error().message;
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(w) * h * 4u, 0u);
    color->bind(0u);
    color->upload(w, h, zeros.data());
    color->unbind(0u);
    framebuffer->bind();
    framebuffer->attachColor(*color);
    EXPECT_TRUE(framebuffer->isComplete());
    framebuffer->unbind();
    return RenderedTarget(std::move(*color), std::move(*framebuffer));
}

/// Render `scene` to the target and read back the single pixel at (x, y).
/// `x`/`y` are readback coordinates (y = 0 is the bottom scanline).
/// T3a: direct renderer.render deleted — port via render::View + REContext::current().beginPass + View::addItem.
/// @note lifetime: `renderer` is co-owned by the View's IRenderable for the duration of the call.
std::vector<std::uint8_t> renderAndReadPixel(const render::PlaneScene& scene,
                                             const std::shared_ptr<render::PlaneRenderer>& renderer,
                                             std::uint32_t x, std::uint32_t y) {
    render::Camera camera = makeCamera();
    render::View view(render::ViewRect{0, 0, static_cast<int>(kTargetWidth), static_cast<int>(kTargetHeight)},
                      glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    view.setCamera(camera);
    view.addItem(scene, renderer);
    auto ensured = view.ensureTarget();
    EXPECT_TRUE(ensured.ok()) << ensured.error().message;
    auto result = view.render();
    EXPECT_TRUE(result.ok()) << result.error().message;

    view.target()->framebuffer().bind();
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    view.target()->framebuffer().unbind();
    return pixels;
}

/// Build an RGBA8 image of size w x h where pixel (x, y) = (r, g, b, a) via
/// the provided callable. Channel count 4.
data::Image makeGradientImage(
    int w, int h,
    const std::function<std::array<std::uint8_t, 4>(int, int)>& fn) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(w) * h * 4u, 0u);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::array<std::uint8_t, 4> p = fn(x, y);
            const std::size_t off = static_cast<std::size_t>(y * w + x) * 4u;
            bytes[off + 0u] = p[0];
            bytes[off + 1u] = p[1];
            bytes[off + 2u] = p[2];
            bytes[off + 3u] = p[3];
        }
    }
    return data::Image(w, h, 4, std::move(bytes));
}

/// The 2D gradient image: pixel (x, y) = (4x, 4y, 128, 255). x*4 / y*4 are
/// exact RGBA8 bytes (0..252 in steps of 4), and B=128/A=255 are constant
/// anchors that make the sampled pixel unambiguous.
data::Image makeGradientImage() {
    return makeGradientImage(kImageWidth, kImageHeight,
                             [](int x, int y) -> std::array<std::uint8_t, 4> {
                                 return {static_cast<std::uint8_t>(x * 4),
                                         static_cast<std::uint8_t>(y * 4), 128u,
                                         255u};
                             });
}

} // namespace

// ---------------------------------------------------------------------------
// (1) FR-render.5 — center pixel of a textured quad matches the source sample.
// ---------------------------------------------------------------------------

TEST(T8RenderPlane, CenterPixelMatchesSolidTextureSample) {
    // Solid 64x64 texture: the center pixel (32,32) must equal the source
    // color exactly (the quad covers the viewport 1:1, so the center pixel
    // samples the center texel, which is the same solid color).
    constexpr std::uint8_t kR = 51u;  // 0.2 * 255
    constexpr std::uint8_t kG = 102u; // 0.4 * 255
    constexpr std::uint8_t kB = 204u; // 0.8 * 255
    constexpr std::uint8_t kA = 255u;
    auto image = std::make_shared<data::Image>(makeGradientImage(
        kImageWidth, kImageHeight,
        [kR, kG, kB, kA](int, int) -> std::array<std::uint8_t, 4> {
            return {kR, kG, kB, kA};
        }));

    auto geometry = std::make_shared<const render::PlaneGeometry>(
        render::PlaneGeometry::unitQuadXY());
    render::PlaneScene scene;
    scene.planes.push_back(
        render::PlaneInstance{geometry, image, glm::mat4(1.0f)});

    auto renderer = std::make_shared<render::PlaneRenderer>();
    const std::vector<std::uint8_t> pixel = renderAndReadPixel(
        scene, renderer, kTargetWidth / 2u, kTargetHeight / 2u);

    EXPECT_NEAR(pixel[0], kR, kColorTolerance) << "R channel";
    EXPECT_NEAR(pixel[1], kG, kColorTolerance) << "G channel";
    EXPECT_NEAR(pixel[2], kB, kColorTolerance) << "B channel";
    EXPECT_NEAR(pixel[3], kA, kColorTolerance) << "A channel";
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (2) FR-render.5 — corner pixels match the source texture samples at the
//     corresponding corner texels.
// ---------------------------------------------------------------------------

TEST(T8RenderPlane, CornerPixelsMatchGradientTextureSamples) {
    // 2D gradient: pixel (x,y) = (4x, 4y, 128, 255). With the vertical row
    // flip (image top row renders at the viewport top), the viewport pixel
    // (px, py) samples image pixel (px, H-1-py). The four viewport corners:
    //   bottom-left (0,0)  -> image (0,63)   = (0, 252, 128, 255)
    //   bottom-right(63,0) -> image (63,63)  = (252,252,128,255)
    //   top-left   (0,63)  -> image (0,0)    = (0,   0, 128, 255)
    //   top-right  (63,63) -> image (63,0)   = (252, 0, 128, 255)
    constexpr std::uint8_t kBlue128 = 128u;
    constexpr std::uint8_t kAlpha255 = 255u;

    auto image = std::make_shared<data::Image>(makeGradientImage());
    auto geometry = std::make_shared<const render::PlaneGeometry>(
        render::PlaneGeometry::unitQuadXY());
    render::PlaneScene scene;
    scene.planes.push_back(
        render::PlaneInstance{geometry, image, glm::mat4(1.0f)});

    auto renderer = std::make_shared<render::PlaneRenderer>();

    const std::vector<std::uint8_t> bottomLeft =
        renderAndReadPixel(scene, renderer, 0u, 0u);
    EXPECT_NEAR(bottomLeft[0], 0u, kColorTolerance);
    EXPECT_NEAR(bottomLeft[1], 252u, kColorTolerance);
    EXPECT_NEAR(bottomLeft[2], kBlue128, kColorTolerance);
    EXPECT_NEAR(bottomLeft[3], kAlpha255, kColorTolerance);

    const std::vector<std::uint8_t> bottomRight =
        renderAndReadPixel(scene, renderer, kTargetWidth - 1u, 0u);
    EXPECT_NEAR(bottomRight[0], 252u, kColorTolerance);
    EXPECT_NEAR(bottomRight[1], 252u, kColorTolerance);
    EXPECT_NEAR(bottomRight[2], kBlue128, kColorTolerance);
    EXPECT_NEAR(bottomRight[3], kAlpha255, kColorTolerance);

    const std::vector<std::uint8_t> topLeft =
        renderAndReadPixel(scene, renderer, 0u, kTargetHeight - 1u);
    EXPECT_NEAR(topLeft[0], 0u, kColorTolerance);
    EXPECT_NEAR(topLeft[1], 0u, kColorTolerance);
    EXPECT_NEAR(topLeft[2], kBlue128, kColorTolerance);
    EXPECT_NEAR(topLeft[3], kAlpha255, kColorTolerance);

    const std::vector<std::uint8_t> topRight = renderAndReadPixel(
        scene, renderer, kTargetWidth - 1u, kTargetHeight - 1u);
    EXPECT_NEAR(topRight[0], 252u, kColorTolerance);
    EXPECT_NEAR(topRight[1], 0u, kColorTolerance);
    EXPECT_NEAR(topRight[2], kBlue128, kColorTolerance);
    EXPECT_NEAR(topRight[3], kAlpha255, kColorTolerance);

    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (3) FR-render.5 — center pixel of the gradient (with the analytic row flip).
// ---------------------------------------------------------------------------

TEST(T8RenderPlane, CenterPixelMatchesGradientWithRowFlip) {
    // Viewport center (32,32) samples image pixel (32, 63-32) = (32,31)
    // -> (128, 124, 128, 255) (the vertical row flip, see file comment).
    constexpr std::uint8_t kR = 128u; // 32 * 4
    constexpr std::uint8_t kG = 124u; // 31 * 4
    constexpr std::uint8_t kB = 128u;
    constexpr std::uint8_t kA = 255u;

    auto image = std::make_shared<data::Image>(makeGradientImage());
    auto geometry = std::make_shared<const render::PlaneGeometry>(
        render::PlaneGeometry::unitQuadXY());
    render::PlaneScene scene;
    scene.planes.push_back(
        render::PlaneInstance{geometry, image, glm::mat4(1.0f)});

    auto renderer = std::make_shared<render::PlaneRenderer>();
    const std::vector<std::uint8_t> pixel = renderAndReadPixel(
        scene, renderer, kTargetWidth / 2u, kTargetHeight / 2u);

    EXPECT_NEAR(pixel[0], kR, kColorTolerance) << "R channel";
    EXPECT_NEAR(pixel[1], kG, kColorTolerance) << "G channel";
    EXPECT_NEAR(pixel[2], kB, kColorTolerance) << "B channel";
    EXPECT_NEAR(pixel[3], kA, kColorTolerance) << "A channel";
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (4) FR-render.5 — plane orientation + UV binding verifiable analytically
//     (pure math on PlaneGeometry, no readback).
// ---------------------------------------------------------------------------

TEST(T8RenderPlane, UnitQuadOrientationAndUvBindingAreAnalytic) {
    const render::PlaneGeometry g = render::PlaneGeometry::unitQuadXY();

    // Corners: the XY square [-1,1]^2 at z=0, in CCW order when viewed from
    // +Z (the normal's side).
    EXPECT_EQ(g.corners[0], glm::vec3(-1.0f, -1.0f, 0.0f));
    EXPECT_EQ(g.corners[1], glm::vec3(1.0f, -1.0f, 0.0f));
    EXPECT_EQ(g.corners[2], glm::vec3(1.0f, 1.0f, 0.0f));
    EXPECT_EQ(g.corners[3], glm::vec3(-1.0f, 1.0f, 0.0f));

    // UV binding: (0,0) at corner0, (1,1) at corner2, so the image maps
    // exactly once across the quad.
    EXPECT_EQ(g.uv[0], glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(g.uv[1], glm::vec2(1.0f, 0.0f));
    EXPECT_EQ(g.uv[2], glm::vec2(1.0f, 1.0f));
    EXPECT_EQ(g.uv[3], glm::vec2(0.0f, 1.0f));

    // Analytic normal: normalized cross(corner1 - corner0, corner3 - corner0)
    // = cross((2,0,0),(0,2,0)) / |.| = (0,0,4)/4 = (0,0,1).
    EXPECT_EQ(g.normal, glm::vec3(0.0f, 0.0f, 1.0f));
}

// ---------------------------------------------------------------------------
// (5) FR-render.5 — plane orientation verifiable analytically through a
//     rendered, model-rotated plane.
// ---------------------------------------------------------------------------

TEST(T8RenderPlane, RotatedPlaneMapsTextureAnalytically) {
    // Rotate the unit quad 90 degrees about +Z (CCW when viewed from +Z):
    // (x,y,0) -> (-y,x,0). The viewport bottom-left (NDC -1,-1) is the
    // preimage of local (-1,1,0) = corner3 (UV (0,1)), which samples image
    // pixel (0, 63-63=0) = (0, 0, 128, 255). Without the rotation that pixel
    // was (0, 252, 128, 255) (corner3 maps elsewhere). So the G channel of
    // the bottom-left pixel proves the plane was re-oriented: 252 -> 0.
    constexpr std::uint8_t kBlue128 = 128u;
    constexpr std::uint8_t kAlpha255 = 255u;

    auto image = std::make_shared<data::Image>(makeGradientImage());
    auto geometry = std::make_shared<const render::PlaneGeometry>(
        render::PlaneGeometry::unitQuadXY());

    glm::mat4 model(1.0f);
    model =
        glm::rotate(model, glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    render::PlaneScene scene;
    scene.planes.push_back(render::PlaneInstance{geometry, image, model});

    auto renderer = std::make_shared<render::PlaneRenderer>();
    const std::vector<std::uint8_t> bottomLeft =
        renderAndReadPixel(scene, renderer, 0u, 0u);

    // After the 90-degree rotation the bottom-left pixel samples the image's
    // top-left texel (0,0): R=0, G=0.
    EXPECT_NEAR(bottomLeft[0], 0u, kColorTolerance);
    EXPECT_NEAR(bottomLeft[1], 0u, kColorTolerance);
    EXPECT_NEAR(bottomLeft[2], kBlue128, kColorTolerance);
    EXPECT_NEAR(bottomLeft[3], kAlpha255, kColorTolerance);
    EXPECT_FALSE(core::hasPendingGlError());
}

} // namespace re::tests
