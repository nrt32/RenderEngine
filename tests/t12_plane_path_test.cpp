// tests/t12_plane_path_test.cpp — T12 gate tests (FR-render.5, SPEC §3/§11),
// V3.4b edition: the textured-plane display path is broker-mediated and
// renderer-owned.
//
// Asserts:
//   (1) broker::PlaneMapper translation — scene::PlaneObject{image asset ref,
//       transform, presentation} maps to render::PlaneInstance whose geometry
//       is THE shared analytic unit quad (corners (±1,±1,0), UV binding
//       (0,0)@c0 … (1,1)@c2, normal (0,0,1)) with image + transform carried
//       exactly; two mappings share ONE geometry instance; a null image is a
//       typed error code 1 (SPEC §5, no exceptions);
//   (2) the Broker registry route — the mapper registered under AppT
//       scene::PlaneObject is fetched as the type-erased IMapper interface and
//       translates through it (the composition-root route every sample uses);
//   (3) FR-render.5 through ReView + Broker — a gradient-textured plane mapped
//       via PlaneMapper, drawn by render::PlaneRenderer::drawLayer inside a
//       render::View, reproduces the source texture at the center AND all four
//       corner pixels within 1/255 via utils::PixelReader (same acceptance
//       constants as the direct-render T8 gate, now via ReView/Broker);
//   (4) MPR-style slice layer through PlaneMapper — the exact sample
//       composition (app::makeSliceModel transform + app::makeSliceCamera +
//       View drawLayer) displays the held slice image's EXACT bytes across the
//       viewport, so "MPR PlaneObject via PlaneMapper" stays green;
//   (5) mechanical floor — app/ contains no occurrence of the RE-side quad
//       geometry type or its builder: no CPU quad vertex generation outside
//       render/ (the T12 grep gate, made regression-locked in-suite).
//
// Analytic setup for (3)/(4): see docs/render.md FR-render.5 constants. With a
// 64x64 texture on the identity-model unit quad under an orthographic camera
// mapping NDC [-1,1]^2 onto the full 64x64 viewport, pixel center (px,py)
// samples texel s = (px+0.5)/64 * 64 - 0.5 = px exactly (frac = 0 under
// linear filtering). With PlaneRenderer's internal row flip (imageToRgba8: data::Image
// is top-left origin, GL textures bottom-up), viewport pixel (px,py) samples
// image pixel (px, H-1-py).
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including utils::PixelReader for pixel readback) — no raw glXxx.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "app/mpr_slice.hpp" // makeSliceImage oracle + makeSliceModel display frame
#include "broker/slice_display.hpp"
#include "broker/broker.hpp"
#include "broker/plane_mapper.hpp"
#include "broker/plane_object_mapper.hpp"
#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "data/image.hpp"
#include "render/mesh_renderer.hpp" // render::MeshInstance (unregistered-key probe)
#include "render/plane_renderer.hpp"
#include "render/view.hpp"
#include "scene/object.hpp"
#include "scene/translate_context.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {
namespace {

namespace app = re::app;
namespace broker = re::broker;
namespace core = re::core;
namespace data = re::data;
namespace render = re::render;
namespace scene = re::scene;

// ---------------------------------------------------------------------------
// Explainable constants (FR-render.5).
// ---------------------------------------------------------------------------

// Target size == texture size == 64: the quad covers the viewport 1:1 and each
// pixel samples an exact texel (see file comment).
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;

// The color tolerance: 1/255 per FR-render.5.
constexpr int kColorTolerance = 1;

/// The 64x64 gradient image used by the pixel gates: pixel (x,y) =
/// (4x, 4y, 128, 255). x*4 / y*4 are exact RGBA8 bytes (0..252 step 4);
/// B=128 / A=255 are constant anchors that make the sampled texel unambiguous.
data::Image makeGradientImage() {
    constexpr std::int32_t kSide = 64;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(kSide) * kSide *
                                    4u, 0u);
    for (std::int32_t y = 0; y < kSide; ++y) {
        for (std::int32_t x = 0; x < kSide; ++x) {
            const std::size_t off =
                static_cast<std::size_t>(y * kSide + x) * 4u;
            bytes[off + 0u] = static_cast<std::uint8_t>(x * 4); // R
            bytes[off + 1u] = static_cast<std::uint8_t>(y * 4); // G
            bytes[off + 2u] = 128u;                             // B
            bytes[off + 3u] = 255u;                             // A
        }
    }
    return data::Image(kSide, kSide, 4, std::move(bytes));
}

/// The default FR-render.5 camera: eye at (0,0,5) looking down -Z, ortho
/// mapping NDC [-1,1]^2 onto the full viewport (identical to the direct-render
/// T8 gate's camera, so the acceptance constants transfer unchanged).
render::Camera makeOrthoCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

/// Read back the whole framebuffer currently bound (via utils::PixelReader ->
/// core/ readback anchor; no raw gl* here).
std::vector<std::uint8_t> readBoundFramebuffer(std::uint32_t w,
                                               std::uint32_t h) {
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(0u, 0u, w, h, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(),
              static_cast<std::size_t>(w) * h * 4u);
    return pixels;
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

// ---------------------------------------------------------------------------
// (1) broker::PlaneObjectMapper (named PlaneMapper before T20 renamed it to
//     match SPEC §11.3 and free the name for the PlaneDesc->ClipPlane rule) —
//     pure translation to the shared unit quad.
// ---------------------------------------------------------------------------

TEST(T12PlanePath, PlaneMapperTranslatesSceneToRender) {
    auto image = std::make_shared<data::Image>(makeGradientImage());
    scene::PlaneObject plane;
    // Shared-reference ownership: the object stores its asset as a
    // co-owned shared_ptr (never a raw borrow), so the CPU bytes stay alive
    // while any scene object, store, or renderer still refers to them (T13).
    plane.image = image;
    plane.transform = glm::translate(glm::mat4(1.0f),
                                     glm::vec3(1.0f, 2.0f, 3.0f));

    broker::PlaneObjectMapper mapper;
    auto mapped = mapper.map(plane, scene::TranslateContext{});
    ASSERT_TRUE(mapped.ok()) << mapped.error().message;

    // Geometry IS the shared analytic unit quad (PlaneGeometry::unitQuadXY
    // contract, docs/render.md): corners of [-1,1]^2 at z=0, UV binding
    // (0,0)@corner0 … (1,1)@corner2, normal cross(c1-c0, c3-c0)/|.|=(0,0,1).
    ASSERT_NE(mapped->geometry, nullptr);
    EXPECT_EQ(mapped->geometry->corners[0], glm::vec3(-1.0f, -1.0f, 0.0f));
    EXPECT_EQ(mapped->geometry->corners[1], glm::vec3(1.0f, -1.0f, 0.0f));
    EXPECT_EQ(mapped->geometry->corners[2], glm::vec3(1.0f, 1.0f, 0.0f));
    EXPECT_EQ(mapped->geometry->corners[3], glm::vec3(-1.0f, 1.0f, 0.0f));
    EXPECT_EQ(mapped->geometry->uv[0], glm::vec2(0.0f, 0.0f));
    EXPECT_EQ(mapped->geometry->uv[1], glm::vec2(1.0f, 0.0f));
    EXPECT_EQ(mapped->geometry->uv[2], glm::vec2(1.0f, 1.0f));
    EXPECT_EQ(mapped->geometry->uv[3], glm::vec2(0.0f, 1.0f));
    EXPECT_EQ(mapped->geometry->normal, glm::vec3(0.0f, 0.0f, 1.0f));

    // Asset ref + transform carried exactly (RE-minimal pass-through): the
    // mapped instance must share the SAME image object the scene holds —
    // shared ownership, no copy of pixels and no re-wrap into a new object.
    EXPECT_EQ(mapped->image, image);
    EXPECT_NEAR(mapped->model[3][0], 1.0f, 1e-6f) << "model translation x";
    EXPECT_NEAR(mapped->model[3][1], 2.0f, 1e-6f) << "model translation y";
    EXPECT_NEAR(mapped->model[3][2], 3.0f, 1e-6f) << "model translation z";

    // One shared quad: a second object maps to the SAME geometry instance
    // because the mapper owns exactly ONE unit quad; instances co-own a
    // reference to it (pointer identity of the shared_ptr proves sharing).
    scene::PlaneObject second;
    second.image = image;
    auto mappedAgain = mapper.map(second, scene::TranslateContext{});
    ASSERT_TRUE(mappedAgain.ok()) << mappedAgain.error().message;
    EXPECT_EQ(mappedAgain->geometry.get(), mapped->geometry.get())
        << "all mapped instances share the mapper's single unit quad";

    // A null image asset reference must come back as typed error code 1 —
    // never a crash and never a silently-empty mapped instance that would
    // draw nothing downstream.
    scene::PlaneObject broken; // image == null shared reference
    auto err = mapper.map(broken, scene::TranslateContext{});
    ASSERT_TRUE(err.failed());
    EXPECT_EQ(err.error().code, 1);
}

// ---------------------------------------------------------------------------
// (2) Broker registry route — type-erased IMapper dispatch per AppT.
// ---------------------------------------------------------------------------

TEST(T12PlanePath, BrokerRegistryRoutesPlaneObjectsThroughIMapper) {
    broker::Broker broker_;
    broker_.registerMapper(std::make_unique<broker::PlaneObjectMapper>());

    // Composition-root route: fetch by (AppT, ReT) as the type-erased
    // interface — app never holds a concrete mapper handle.
    auto* mapper = broker_.get<scene::PlaneObject, render::PlaneInstance>();
    ASSERT_NE(mapper, nullptr) << "PlaneMapper must be reachable via AppT key";

    auto image = std::make_shared<data::Image>(makeGradientImage());
    scene::PlaneObject plane;
    // Shared-reference ownership: the object stores its asset as a
    // co-owned shared_ptr (never a raw borrow), so the CPU bytes stay alive
    // while any scene object, store, or renderer still refers to them (T13).
    plane.image = image;
    auto mapped = mapper->map(plane, scene::TranslateContext{});
    ASSERT_TRUE(mapped.ok()) << mapped.error().message;
    EXPECT_EQ(mapped->image, image);
    ASSERT_NE(mapped->geometry, nullptr);

    // Unregistered AppT stays null (OCP registry, no cross-type leakage).
    EXPECT_EQ((broker_.get<scene::MeshObject, render::MeshInstance>()),
              nullptr);
}

// ---------------------------------------------------------------------------
// (3) FR-render.5 via ReView + Broker — center AND corner pixels match the
//     source texture within 1/255 (utils::PixelReader readback).
// ---------------------------------------------------------------------------

TEST(T12PlanePath, CenterAndCornerPixelsMatchTextureThroughReViewAndBroker) {
    auto image = std::make_shared<data::Image>(makeGradientImage());

    broker::Broker broker_;
    broker_.registerMapper(std::make_unique<broker::PlaneObjectMapper>());
    auto* mapper = broker_.get<scene::PlaneObject, render::PlaneInstance>();

    scene::PlaneObject plane;
    // The image is stored by SHARED reference (scene and mapper co-own it),
    // while geometry comes from the mapper's single unit quad spanning NDC
    // [-1,1]^2 — one quad, shared by every mapped plane instance.
    plane.image = image;
    auto mapped = mapper->map(plane, scene::TranslateContext{});
    ASSERT_TRUE(mapped.ok()) << mapped.error().message;

    render::PlaneScene planeScene;
    planeScene.planes.push_back(*mapped);

    // The composed display path: render::View owns the target, composes the
    // layer through PlaneRenderer::drawLayer (type-erased IRenderable), no
    // direct renderer.render() call anywhere in this test.
    render::View view(render::ViewRect{0, 0,
                                       static_cast<int>(kTargetWidth),
                                       static_cast<int>(kTargetHeight)},
                      glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    view.setCamera(makeOrthoCamera());
    auto renderer = std::make_shared<render::PlaneRenderer>();
    view.addItem(planeScene, renderer);

    core::DrawContext ctx;
    auto rendered = view.renderWithEnsure(ctx);
    ASSERT_TRUE(rendered.ok()) << rendered.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    ASSERT_NE(view.target(), nullptr);
    view.target()->framebuffer().bind();
    const std::vector<std::uint8_t> pixels =
        readBoundFramebuffer(kTargetWidth, kTargetHeight);

    // Readback index helper (readback row 0 = viewport bottom).
    const auto at = [&](std::uint32_t px, std::uint32_t py) {
        return (static_cast<std::size_t>(py) * kTargetWidth + px) * 4u;
    };
    // Row-flip convention: viewport (px,py) samples image (px, H-1-py).
    //   center      (32,32) -> image (32,31) = (128,124,128,255)
    //   bottom-left (0,0)   -> image (0,63)  = (0,252,128,255)
    //   bottom-right(63,0)  -> image (63,63) = (252,252,128,255)
    //   top-left    (0,63)  -> image (0,0)   = (0,0,128,255)
    //   top-right   (63,63) -> image (63,0)  = (252,0,128,255)
    struct Probe {
        std::uint32_t px, py;
        std::uint8_t r, g, b, a;
    };
    const std::array<Probe, 5> probes = {{
        {32u, 32u, 128u, 124u, 128u, 255u},
        {0u, 0u, 0u, 252u, 128u, 255u},
        {63u, 0u, 252u, 252u, 128u, 255u},
        {0u, 63u, 0u, 0u, 128u, 255u},
        {63u, 63u, 252u, 0u, 128u, 255u},
    }};
    for (const Probe& probe : probes) {
        SCOPED_TRACE(::testing::Message()
                     << "probe (" << probe.px << "," << probe.py << ")");
        EXPECT_NEAR(pixels[at(probe.px, probe.py) + 0u], probe.r,
                    kColorTolerance)
            << "R channel (FR-render.5)";
        EXPECT_NEAR(pixels[at(probe.px, probe.py) + 1u], probe.g,
                    kColorTolerance)
            << "G channel (FR-render.5)";
        EXPECT_NEAR(pixels[at(probe.px, probe.py) + 2u], probe.b,
                    kColorTolerance)
            << "B channel (FR-render.5)";
        EXPECT_NEAR(pixels[at(probe.px, probe.py) + 3u], probe.a,
                    kColorTolerance)
            << "A channel (FR-render.5)";
    }
    EXPECT_FALSE(core::hasPendingGlError());
}

// ---------------------------------------------------------------------------
// (4) MPR-style slice layer through PlaneMapper — the sample composition
//     (makeSliceModel transform + makeSliceCamera + View drawLayer) shows the
//     slice image's EXACT bytes edge-to-edge.
// ---------------------------------------------------------------------------

TEST(T12PlanePath, MprSliceLayerThroughPlaneMapperDisplaysSliceBytes) {
    // A solid mid-gray 64x64 slice image: every texel (40,40,40,255), so the
    // displayed bytes are orientation-independent and analytically known.
    constexpr std::uint8_t kSliceByte = 40u;
    std::vector<std::uint8_t> solid(static_cast<std::size_t>(64) * 64 * 4u);
    for (std::size_t i = 0u; i < solid.size(); i += 4u) {
        solid[i + 0u] = kSliceByte;
        solid[i + 1u] = kSliceByte;
        solid[i + 2u] = kSliceByte;
        solid[i + 3u] = 255u;
    }
    auto sliceImage =
        std::make_shared<const data::Image>(64, 64, 4, std::move(solid));

    // Scene-side object exactly as the MPR sample builds it: {asset ref,
    // transform} where the transform scales the shared quad onto the image's
    // pixel rectangle; camera via broker/slice_display.hpp).
    scene::PlaneObject appPlane;
    // Shared-reference ownership: the object stores its asset as a
    // co-owned shared_ptr (never a raw borrow), so the CPU bytes stay alive
    // while any scene object, store, or renderer still refers to them (T13).
    appPlane.image = sliceImage;
    appPlane.transform = app::makeSliceModel(*sliceImage);

    broker::Broker broker_;
    broker_.registerMapper(std::make_unique<broker::PlaneObjectMapper>());
    auto* mapper = broker_.get<scene::PlaneObject, render::PlaneInstance>();
    ASSERT_NE(mapper, nullptr);
    auto mapped = mapper->map(appPlane, scene::TranslateContext{});
    ASSERT_TRUE(mapped.ok()) << mapped.error().message;

    render::PlaneScene sliceScene;
    sliceScene.planes.push_back(*mapped);

    render::View view(render::ViewRect{0, 0,
                                       static_cast<int>(kTargetWidth),
                                       static_cast<int>(kTargetHeight)},
                      glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    // The display factory now yields a scene::Camera value (broker
    // slice_display); convert to the draw-time RE camera for the
    // direct View call — same matrices CameraMapper produces.
    view.setCamera(broker::toRenderCamera(
        broker::makeSliceCamera(*sliceImage)));
    auto sliceRenderer = std::make_shared<render::PlaneRenderer>();
    view.addItem(sliceScene, sliceRenderer);

    core::DrawContext ctx;
    auto rendered = view.renderWithEnsure(ctx);
    ASSERT_TRUE(rendered.ok()) << rendered.error().message;
    EXPECT_FALSE(core::hasPendingGlError());

    ASSERT_NE(view.target(), nullptr);
    view.target()->framebuffer().bind();
    const std::vector<std::uint8_t> pixels =
        readBoundFramebuffer(kTargetWidth, kTargetHeight);

    // The ortho window [0,imgW]x[0,imgH] matches the 64x64 viewport 1:1, so
    // EVERY pixel shows the solid slice byte — the probes pin the extent
    // edge-to-edge (center + all four corners).
    const auto at = [&](std::uint32_t px, std::uint32_t py) {
        return (static_cast<std::size_t>(py) * kTargetWidth + px) * 4u;
    };
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 5> probes = {
        {{32u, 32u}, {0u, 0u}, {63u, 0u}, {0u, 63u}, {63u, 63u}}};
    for (const auto& [px, py] : probes) {
        SCOPED_TRACE(::testing::Message() << "probe (" << px << "," << py
                                          << ")");
        for (int c = 0; c < 3; ++c) {
            EXPECT_NEAR(pixels[at(px, py) + static_cast<std::size_t>(c)],
                        kSliceByte, kColorTolerance)
                << "channel " << c << ": slice byte through PlaneMapper";
        }
        EXPECT_EQ(pixels[at(px, py) + 3u], 255u) << "alpha";
    }
}

// ---------------------------------------------------------------------------
// (5) Mechanical floor — no CPU quad parsing outside render/: app/ names
//     neither the RE-side quad geometry type nor its builder.
// ---------------------------------------------------------------------------

TEST(T12PlanePath, AppHoldsNoQuadGeometryParsing) {
    const std::filesystem::path dir =
        std::filesystem::path(TEST_SOURCE_DIR) / "app";
    ASSERT_TRUE(std::filesystem::exists(dir)) << dir.string() << " must exist";

    int hits = 0;
    std::string offenders;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        const auto& ext = entry.path().extension();
        if (ext != ".cpp" && ext != ".hpp") continue;
        const std::string content = readFile(entry.path());
        // The tokens are split here so this file itself cannot trip the same
        // rule if the sweep were ever pointed at tests/.
        const std::string token1 = std::string("Plane") + "Geometry";
        const std::string token2 = std::string("unitQuad") + "XY";
        if (content.find(token1) != std::string::npos ||
            content.find(token2) != std::string::npos) {
            ++hits;
            offenders += entry.path().filename().string() + " ";
        }
    }
    EXPECT_EQ(hits, 0)
        << "app/ must not name the RE-side quad geometry or its builder "
           "(V3.4b T12: no CPU quad vertex generation outside render/); "
           "offenders: "
        << offenders;
}

} // namespace re::tests
