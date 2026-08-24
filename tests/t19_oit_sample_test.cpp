// tests/t19_oit_sample_test.cpp — T19 gate (OIT sample: real meshes under
// order-independent transparency, consuming the T18 depth support).
//
// Asserts (each an explainable constant; derivations in docs/render.md
// "LinkedListOIT" + the tables below):
//
//   (1) FR-render.2 on the SAMPLE's arrangement — the composition built by
//       app/oit_scene.hpp (the exact code path app/oit_sample.cpp runs)
//       produces closed-form composites at three probe pixels covering the
//       three required regions:
//         P1 fully-opaque region            -> pure golden-box bytes,
//                                              alpha 255;
//         P2 near-glass-over-opaque region  -> near-over(gold) chain;
//         P3 far-over-near-over-opaque      -> full 4-fragment chain.
//       Plus a fourth probe inside the bunny silhouette asserting ONLY its
//       alpha (its smooth-shaded color is not closed-form): an opaque base
//       under the ray pins the composited alpha at exactly 1.0 -> 255.
//       Frame 2 re-runs the whole composition and must reproduce every byte
//       (cross-frame state must not leak between the depth-enabled pass and
//       the depth-off OIT passes).
//   (2) FR-render.3 — the injected pipeline spy stays OFF for the
//       opaque-only layer and engages EXACTLY for the transparent set:
//       begin/end once per frame, drawTransparent once per transparent mesh
//       (spy count == number of transparent meshes == 2).
//   (3) The node allocator counted EXACTLY 5632 captured fragments: each
//       glass box contributes 2 fragments (front +Z face + back -Z face; no
//       culling, capture runs depth-off) for every pixel whose CENTER lies
//       in its footprint. Near shell: x centers k in [11,42] (32 columns),
//       y centers k in [10,53] (44 rows) => 1408 px; far shell identical =>
//       1408 px; total (1408+1408)*2 = 5632. No footprint edge passes
//       through any pixel center (edges land at fractional center indices),
//       so the count is exact rather than tolerance-bounded.
//   (4) Mechanical floor: app/oit_sample.cpp contains neither quad-primitive
//       builder name (`unitQuadXY`, `makeQuadMesh`) — the sample scene is
//       built exclusively from real meshes — and its instructions/title
//       text describes the new scene (mentions the glass boxes and the
//       bunny, no longer the removed three-quad arrangement).
//
// Analytic derivation of the probe bytes (all colors straight RGBA; the
// capture shader stores PREMULTIPLIED base color with no lighting, the
// composite accumulates far->near with `rgb = s.rgb + (1-s.a)*acc.rgb` and
// blends over the opaque destination with (GL_ONE, GL_ONE_MINUS_SRC_ALPHA)):
//
//   gold  opaque base (flat +Z face shades at exactly base color):
//         {0.85,0.45,0.15,1} -> bytes {217,115,38,255}
//   near glass premultiplied: {0.90,0.20,0.20,0.5}*a -> {0.45,0.10,0.10,0.5}
//   far  glass premultiplied: {0.20,0.35,0.90,0.5}*a -> {0.10,0.175,0.45,0.5}
//
//   P1 (only gold beneath): {217,115,38,255}.
//
//   P2 (gold + near glass front&back): accumulate back face then front face:
//     acc = {0.45,0.10,0.10,0.5}
//     acc = {0.45,0.10,0.10} + 0.5*acc = {0.675,0.15,0.15}, a = 0.75
//     over gold: rgb = acc + 0.25*{0.85,0.45,0.15} = {0.8875,0.2625,0.1875},
//     a = 0.75 + 0.25*1 = 1 -> bytes {226,67,48,255}.
//
//   P3 (gold + near front/back + far front/back), far->near accumulation:
//     fb: {0.10,0.175,0.45,0.5}
//     ff: {0.15,0.2625,0.675}, a=0.75
//     nb: {0.525,0.23125,0.4375}, a=0.875
//     nf: {0.7125,0.215625,0.31875}, a=0.9375
//     over gold: +0.0625*{0.85,0.45,0.15} = {0.765625,0.24375,0.328125},
//     a = 1 -> bytes {195,62,84,255}.
//   Processing the fragments in the OPPOSITE order would give
//   {0.371875,0.328125,0.721875} -> {95,84,184}: far outside the 1/255
//   tolerance, so the probe discriminates depth ordering.
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (utils::PixelReader delegates the raw readback to core/) — no raw
// glXxx calls.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/vec4.hpp>

#include "app/oit_scene.hpp"
#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/texture2d.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "io/mesh/obj_mesh_loader.hpp"
#include "render/asset_registry.hpp"
#include "render/imaterial.hpp"
#include "render/linked_list_oit.hpp"
#include "render/mesh_renderer.hpp"
#include "render/view.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {
namespace {

// Shorthand for the shared sample/gate scene rig (re::app::oit_scene).
namespace oit_scene = re::app::oit_scene;

// ---------------------------------------------------------------------------
// Explainable constants (T19 gate; see the file-comment derivations).
// ---------------------------------------------------------------------------

constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;

// Color tolerance: 1/255 per the project's acceptance convention.
constexpr int kColorTolerance = 1;

// Probe pixels on the 64x64 target (readback coordinates, y = 0 bottom).
// Pixel (px,py) spans world x in [px/32-1, (px+1)/32-1], y likewise.
constexpr std::uint32_t kProbeFullyOpaqueX = 8u;  // world x ~[-0.75,-0.719]
constexpr std::uint32_t kProbeFullyOpaqueY = 31u;
constexpr std::uint32_t kProbeNearOverOpaqueX = 19u; // x ~[-0.406,-0.375]
constexpr std::uint32_t kProbeNearOverOpaqueY = 31u;
constexpr std::uint32_t kProbeFarNearOpaqueX = 40u; // x ~[+0.25,+0.281]
constexpr std::uint32_t kProbeFarNearOpaqueY = 31u;
constexpr std::uint32_t kProbeBunnyX = 33u; // inside the bunny silhouette
constexpr std::uint32_t kProbeBunnyY = 16u; // (body center region)

// Expected bytes (derivations above).
constexpr int kP1R = 217, kP1G = 115, kP1B = 38, kP1A = 255;
constexpr int kP2R = 226, kP2G = 67, kP2B = 48, kP2A = 255;
constexpr int kP3R = 195, kP3G = 62, kP3B = 84, kP3A = 255;

// Exact captured-fragment count across the frame (derivation in (3) above).
constexpr std::uint32_t kExpectedCapturedFragments = 5632u;

/// Read back one RGBA8 pixel of `framebuffer` at (x, y) via utils::PixelReader
/// (the raw readback stays under core/).
std::vector<std::uint8_t> readPixel(core::Framebuffer& framebuffer,
                                    std::uint32_t x, std::uint32_t y) {
    framebuffer.bind();
    std::vector<std::uint8_t> pixels;
    re::utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    framebuffer.unbind();
    return pixels;
}

/// Assert an RGBA8 pixel equals the given bytes within 1/255.
void expectPixel(const std::vector<std::uint8_t>& p, int r, int g, int b, int a,
                 const char* where) {
    EXPECT_NEAR(p[0], r, kColorTolerance) << "R at " << where;
    EXPECT_NEAR(p[1], g, kColorTolerance) << "G at " << where;
    EXPECT_NEAR(p[2], b, kColorTolerance) << "B at " << where;
    EXPECT_NEAR(p[3], a, kColorTolerance) << "A at " << where;
}

/// A recording stub pipeline: counts the lifecycle calls MeshRenderer makes
/// so a test can assert when/how the transparency pipeline engages.
class RecordingPipeline final : public render::ITransparencyPipeline {
   public:
    data::Result<void> begin(const render::Camera&,
                             const render::RenderTarget&) override {
        ++beginCount_;
        return data::Result<void>(data::value);
    }
    data::Result<void> drawTransparent(const render::MeshGeometry&,
                                       const glm::vec4&, const glm::mat4&,
                                       const render::Camera&) override {
        ++drawTransparentCount_;
        return data::Result<void>(data::value);
    }
    data::Result<void> end(const render::Camera&,
                           const render::RenderTarget&) override {
        ++endCount_;
        return data::Result<void>(data::value);
    }
    bool isEngaged() const noexcept override { return beginCount_ > endCount_; }
    int beginCount() const noexcept { return beginCount_; }
    int drawTransparentCount() const noexcept { return drawTransparentCount_; }
    int endCount() const noexcept { return endCount_; }

   private:
    int beginCount_{0};
    int drawTransparentCount_{0};
    int endCount_{0};
};

/// Color-only FBO target for the direct-render spy checks.
struct RenderedTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;
};

RenderedTarget makeTarget(std::uint32_t w, std::uint32_t h) {
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
    return RenderedTarget{std::move(*color), std::move(*framebuffer)};
}

std::string readFile(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) {
        return {};
    }
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

int countOccurrences(const std::string& hay, const std::string& needle) {
    int c = 0;
    size_t pos = 0;
    while ((pos = hay.find(needle, pos)) != std::string::npos) {
        ++c;
        pos += needle.size();
    }
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// (1)+(3) FR-render.2 on the sample arrangement: analytic composite probes +
//         exact captured-fragment count, stable across repeated frames.
// ---------------------------------------------------------------------------

TEST(T19OitSample, CompositeProbesMatchAnalyticChain) {
    auto registry = std::make_shared<render::AssetRegistry>();
    auto bunnyResult = io::loadObjMesh(
        std::string(TEST_SOURCE_DIR) + "/data/meshes/bunny.obj");
    ASSERT_TRUE(bunnyResult.ok()) << bunnyResult.error().message;

    oit_scene::Rig rig(registry, std::move(*bunnyResult));
    ASSERT_TRUE(rig.handlesRegistered());

    auto renderer = std::make_shared<render::MeshRenderer>(registry);
    auto pipeline = std::make_shared<render::LinkedListOIT>();

    // The occlusion-capable composition owner: depth-test ON => ensureTarget
    // creates a DepthMode::Enabled target (a REAL depth attachment behind the
    // opaque pass — the T18 support this sample consumes).
    render::View view(render::ViewRect{0, 0, static_cast<int>(kTargetWidth),
                                       static_cast<int>(kTargetHeight)},
                      oit_scene::kClearColor);
    view.setDepthTest(true);
    view.addItem(rig.opaqueScene(), renderer);

    for (int frame = 1; frame <= 2; ++frame) {
        core::DrawContext ctx;
        auto composed = oit_scene::composeFrame(view, *pipeline, rig,
                                                kTargetWidth, kTargetHeight,
                                                ctx);
        ASSERT_TRUE(composed.ok()) << composed.error().message;
        ASSERT_NE(view.target(), nullptr);

        // The target physically owns the depth attachment this composition
        // relies on, and the framebuffer is complete WITH it attached.
        EXPECT_TRUE(view.target()->hasDepth());
        view.target()->framebuffer().bind();
        EXPECT_TRUE(view.target()->framebuffer().isComplete());
        view.target()->framebuffer().unbind();

        // P1 fully-opaque region: pure golden-box bytes, alpha pinned at 1.0.
        expectPixel(readPixel(view.target()->framebuffer(),
                              kProbeFullyOpaqueX, kProbeFullyOpaqueY),
                    kP1R, kP1G, kP1B, kP1A, "P1 fully-opaque");
        // P2 near-transparent-over-opaque region.
        expectPixel(readPixel(view.target()->framebuffer(),
                              kProbeNearOverOpaqueX, kProbeNearOverOpaqueY),
                    kP2R, kP2G, kP2B, kP2A, "P2 near-over-opaque");
        // P3 far-transparent-over-near-over-opaque region (full chain).
        expectPixel(readPixel(view.target()->framebuffer(),
                              kProbeFarNearOpaqueX, kProbeFarNearOpaqueY),
                    kP3R, kP3G, kP3B, kP3A, "P3 far-near-opaque");
        // Bunny-interior probe: only ALPHA is closed-form there (an opaque
        // surface lies under the ray, pinning the composited alpha at 1.0),
        // the shaded RGB is not (smooth vertex normals).
        const std::vector<std::uint8_t> bunnyPixel =
            readPixel(view.target()->framebuffer(), kProbeBunnyX,
                      kProbeBunnyY);
        EXPECT_NEAR(bunnyPixel[3], 255, kColorTolerance)
            << "alpha == 1.0 under an opaque base at the bunny probe (frame "
            << frame << ")";

        // Exactly 5632 glass fragments were captured this frame (both faces
        // of both shells over their pixel-center footprints — constant (3)).
        const auto captured = pipeline->readCapturedFragmentCount();
        ASSERT_TRUE(captured.ok()) << captured.error().message;
        EXPECT_EQ(*captured, kExpectedCapturedFragments)
            << "captured fragment count (frame " << frame << ")";
        EXPECT_FALSE(pipeline->isEngaged());
        EXPECT_FALSE(core::hasPendingGlError());
    }
}

// ---------------------------------------------------------------------------
// (2) FR-render.3: the pipeline engages EXACTLY for the transparent set.
// ---------------------------------------------------------------------------

TEST(T19OitSample, PipelineSpyEngagesExactlyForTransparentSet) {
    auto registry = std::make_shared<render::AssetRegistry>();
    auto bunnyResult = io::loadObjMesh(
        std::string(TEST_SOURCE_DIR) + "/data/meshes/bunny.obj");
    ASSERT_TRUE(bunnyResult.ok()) << bunnyResult.error().message;

    oit_scene::Rig rig(registry, std::move(*bunnyResult));
    ASSERT_TRUE(rig.handlesRegistered());
    const render::Camera camera = rig.cameraFor(1.0f);

    // Opaque-only layer through a spy-injected renderer: the pipeline must
    // never engage (no transparent material present).
    {
        RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
        render::RenderTarget rt;
        rt.framebuffer = &target.framebuffer;
        rt.width = kTargetWidth;
        rt.height = kTargetHeight;
        rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

        auto spy = std::make_shared<RecordingPipeline>();
        render::MeshRenderer renderer(registry, spy);
        auto result = renderer.render(rig.opaqueScene(), camera, rt);
        ASSERT_TRUE(result.ok()) << result.error().message;

        EXPECT_EQ(spy->beginCount(), 0) << "opaque-only scene never begins";
        EXPECT_EQ(spy->drawTransparentCount(), 0);
        EXPECT_EQ(spy->endCount(), 0);
        EXPECT_FALSE(spy->isEngaged());
        // Every probed pixel keeps alpha 1.0 (nothing transparent engaged).
        constexpr std::uint32_t kSampleX[3] = {8u, 19u, 40u};
        constexpr std::uint32_t kSampleY[3] = {31u, 31u, 31u};
        for (int i = 0; i < 3; ++i) {
            const std::vector<std::uint8_t> pixel =
                readPixel(target.framebuffer, kSampleX[i], kSampleY[i]);
            EXPECT_EQ(pixel[3], 255u) << "opaque-only alpha at probe " << i;
        }
        EXPECT_FALSE(core::hasPendingGlError());
    }

    // Full mixed scene: the spy sees exactly one frame driven for the whole
    // transparent set — begin/end once, drawTransparent once PER transparent
    // mesh (count == number of transparent meshes == 2).
    {
        RenderedTarget target = makeTarget(kTargetWidth, kTargetHeight);
        render::RenderTarget rt;
        rt.framebuffer = &target.framebuffer;
        rt.width = kTargetWidth;
        rt.height = kTargetHeight;
        rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

        auto spy = std::make_shared<RecordingPipeline>();
        render::MeshRenderer renderer(registry, spy);
        auto result = renderer.render(rig.fullScene(), camera, rt);
        ASSERT_TRUE(result.ok()) << result.error().message;

        EXPECT_EQ(spy->beginCount(), 1) << "pipeline engaged for the frame";
        EXPECT_EQ(spy->drawTransparentCount(),
                  static_cast<int>(oit_scene::Rig::kTransparentCount))
            << "one capture per transparent mesh";
        EXPECT_EQ(spy->endCount(), 1);
        EXPECT_FALSE(spy->isEngaged()) << "frame finished";
        EXPECT_FALSE(core::hasPendingGlError());
    }
}

// ---------------------------------------------------------------------------
// (4) Mechanical floor: the sample source builds no quad primitives and its
//     help text matches the new scene.
// ---------------------------------------------------------------------------

TEST(T19OitSample, SampleSourceHasNoQuadPrimitivesAndUpdatedText) {
    const std::filesystem::path samplePath =
        std::filesystem::path(TEST_SOURCE_DIR) / "app" / "oit_sample.cpp";
    const std::string source = readFile(samplePath);
    ASSERT_FALSE(source.empty()) << "sample source must be readable";

    // Neither quad-primitive builder identifier may appear anywhere in the
    // sample (comments included) — the scene is real meshes only.
    EXPECT_EQ(countOccurrences(source, "unitQuadXY"), 0)
        << "no unitQuadXY in app/oit_sample.cpp";
    EXPECT_EQ(countOccurrences(source, "makeQuadMesh"), 0)
        << "no makeQuadMesh in app/oit_sample.cpp";

    // The instructions text describes the NEW scene (gate G): the glass
    // boxes and the bunny are named; the removed three-quad arrangement is
    // gone.
    EXPECT_GE(countOccurrences(source, "glass"), 1)
        << "instructions must describe the glass boxes";
    EXPECT_GE(countOccurrences(source, "bunny"), 1)
        << "instructions must describe the bunny";
    EXPECT_EQ(countOccurrences(source, "three overlapping transparent quads"),
              0)
        << "the removed three-quad scene text must be gone";
}

} // namespace re::tests
