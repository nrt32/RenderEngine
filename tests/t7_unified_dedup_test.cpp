// tests/t7_unified_dedup_test.cpp — T7 gate: unified asset identity dedup analytic
//
// Asserts (R4 evidence rule — every check is an explainable constant):
//  identical bytes share one Texture3D across 2 VolumeRenderer instances with
//  EXPECT_EQ(registry.slotCount(),1) and EXPECT_EQ(resolve(h1), resolve(h2))
//  (same index+gen+hash) plus 1/255 pixel parity — not bare grep -c threshold.
//  This test tightens the mechanical floor (spec-review #2-#3): the dedup is not
//  proven by a grep count but by a runtime analytic that identical content hashes
//  alias to one GPU slot and both renderers resolve to the same GL texture id
//  while rendered center pixel stays within 1/255 of the analytic CPU ray-cast
//  (FR-render.6). Content-hash IS identity via shared byHash_, no pointer-key
//  map remains (T7 deletes the per-renderer cache and the dual-key shim).
//
//  The registry used is an explicit (non-shared()) instance so slot counts are
//  deterministic and not polluted by the process-wide default's prior state.
//  The two distinct VolumeDataset allocations have byte-identical voxels (uniform
//  0.5) so their content hashes are equal and they must alias to one slot.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/content_hash.hpp"
#include "data/volume_dataset.hpp"
#include "render/asset_registry.hpp"
#include "render/types.hpp"
#include "render/view.hpp"
#include "render/volume_renderer.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"
#include "utils/pixel_reader.hpp"
#include "volume/transfer_function.hpp"
#include "volume/color.hpp"
#include "volume/ray_caster.hpp"

namespace re::tests {

namespace {

constexpr std::uint32_t kW = 64u;
constexpr std::uint32_t kH = 64u;

// Uniform synthetic volume: 2x2x2, every voxel = 0.5 (same as FR-render.6 gate).
data::VolumeDataset makeUniformVolume(float v) {
    return data::VolumeDataset(2, 2, 2, std::vector<float>(8, v));
}

volume::TransferFunction makeGreenTF() {
    return volume::TransferFunction({{0.0f, {0, 1, 0, 0.5f}}, {1.0f, {0, 1, 0, 0.5f}}});
}

render::Camera makeVolCamera() {
    render::Camera c;
    c.position = glm::vec3(0.5f, 0.5f, 5.0f);
    c.view = glm::lookAt(c.position, glm::vec3(0.5f, 0.5f, 0.5f), glm::vec3(0, 1, 0));
    c.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return c;
}

} // namespace

TEST(T7UnifiedDedup, IdenticalBytesShareOneTexture3DAcrossTwoRenderersWithPixelParity) {
    // Ensure the global offscreen fixture's GL context is current before any
    // Texture3D creation — prior tests (e.g. T4Offscreen manualOracle) create
    // and destroy their own OffscreenContext without always restoring the
    // fixture, so we explicitly restore it here for deterministic GL state.
    if (auto* fixture = OffscreenEnvironment::context()) {
        fixture->makeCurrent();
    }
    // Explicit registry so slot counts are deterministic (not shared()).
    auto registry = std::make_shared<render::AssetRegistry>();

    // Two distinct allocations, byte-identical voxels (uniform 0.5).
    auto dsA = std::make_shared<const data::VolumeDataset>(makeUniformVolume(0.5f));
    auto dsB = std::make_shared<const data::VolumeDataset>(makeUniformVolume(0.5f));
    ASSERT_NE(dsA.get(), dsB.get()) << "distinct allocations must have distinct addresses (explainable)";

    // Content hashes must be equal (hash of stable bytes, not pointer).
    const uint64_t hashA = data::computeContentHash(*dsA);
    const uint64_t hashB = data::computeContentHash(*dsB);
    EXPECT_EQ(hashA, hashB) << "identical byte contents must produce identical hash (explainable)";

    auto hA = registry->registerVolume(dsA);
    auto hB = registry->registerVolume(dsB);
    ASSERT_TRUE(hA.ok()) << hA.error().message;
    ASSERT_TRUE(hB.ok()) << hB.error().message;

    // Analytic dedup: same index+gen+hash, one slot.
    EXPECT_EQ(hA->index, hB->index) << "identical content aliases to same slot index (dedup)";
    EXPECT_EQ(hA->generation, hB->generation) << "same generation for aliased content";
    EXPECT_EQ(hA->contentHash, hB->contentHash) << "same contentHash for identical bytes";
    EXPECT_EQ(hA->contentHash, hashA) << "handle hash equals computeContentHash (explainable)";
    EXPECT_EQ(registry->volumeSlotCount(), 1u) << "two identical-byte volumes must occupy 1 slot (content dedup)";

    auto texA = registry->resolveVolume(*hA);
    auto texB = registry->resolveVolume(*hB);
    ASSERT_TRUE(texA.ok());
    ASSERT_TRUE(texB.ok());
    EXPECT_EQ(*texA, *texB) << "one Texture3D object behind both handles (explainable)";
    EXPECT_EQ((*texA)->id(), (*texB)->id()) << "same GL texture name (non-zero)";
    EXPECT_NE((*texA)->id(), 0u) << "live GL texture name non-zero (GL reserves 0)";

    // Two VolumeRenderer instances sharing one registry — each renders the same
    // dataset via its own handle (hA vs hB are equal). Pixel parity 1/255.
    render::VolumeRenderer r1(registry);
    render::VolumeRenderer r2(registry);
    EXPECT_EQ(r1.assets().get(), r2.assets().get()) << "both renderers share one registry";

    volume::TransferFunction tf = makeGreenTF();
    render::VolumeInstance instA{*hA, dsA, tf, glm::mat4(1.0f)};
    render::VolumeInstance instB{*hB, dsB, tf, glm::mat4(1.0f)};
    render::VolumeScene sceneA; sceneA.volumes.push_back(instA);
    render::VolumeScene sceneB; sceneB.volumes.push_back(instB);

    auto cam = makeVolCamera();

    // Helper to render via View and read center pixel.
    auto renderAndRead = [&](render::VolumeRenderer& renderer, const render::VolumeScene& scene) {
        render::View view(render::ViewRect{0, 0, static_cast<int>(kW), static_cast<int>(kH)}, glm::vec4(0));
        view.setCamera(cam);
        auto rendererPtr = std::make_shared<render::VolumeRenderer>(renderer.assets());
        // Use the scene with the pre-registered handle (owner-driven, hashed at
        // load/register time, never per frame per data/content_hash.hpp:31).
        view.addItem(scene, rendererPtr);
        auto result = view.renderWithEnsure();
        EXPECT_TRUE(result.ok()) << result.error().message;
        EXPECT_NE(view.target(), nullptr);
        view.target()->framebuffer().bind();
        std::vector<std::uint8_t> px;
        utils::PixelReader reader;
        auto read = reader.read(kW/2, kH/2, 1, 1, px);
        EXPECT_TRUE(read.ok()) << read.error().message;
        view.target()->framebuffer().unbind();
        return px;
    };

    std::vector<uint8_t> px1 = renderAndRead(r1, sceneA);
    std::vector<uint8_t> px2 = renderAndRead(r2, sceneB);
    ASSERT_EQ(px1.size(), 4u);
    ASSERT_EQ(px2.size(), 4u);

    // Analytic expectation: uniform 0.5 with constant-green TF {0,1,0,0.5}
    // front-to-back of 4 steps => {0,0.9375,0,0.9375} => bytes {0,239,0,239}
    // within 1/255 (FR-render.6). Both renderers must match each other within
    // 1/255 as well (same content, same GL texture).
    constexpr int kTol = 1; // 1/255
    EXPECT_NEAR(px1[1], 239, kTol) << "r1 G channel 239 within 1/255 (analytic 0.9375*255)";
    EXPECT_NEAR(px2[1], 239, kTol) << "r2 G channel 239 within 1/255";
    EXPECT_NEAR(px1[0], 0, kTol);
    EXPECT_NEAR(px2[0], 0, kTol);
    EXPECT_NEAR(px1[1], px2[1], kTol) << "both renderers produce same G within 1/255 (same texture)";
    EXPECT_NEAR(px1[3], 239, kTol);
    EXPECT_NEAR(px2[3], 239, kTol);

    // Slot count still 1 after both renders (no pinned-slot growth, no
    // per-renderer duplicate upload). The dedup is not a grep threshold but a
    // runtime invariant: one GPU object for identical content across renderers.
    EXPECT_EQ(registry->volumeSlotCount(), 1u) << "after two-renderer renders still 1 slot (no growth)";
    // Handles still resolve to same object after renders.
    auto texA2 = registry->resolveVolume(*hA);
    auto texB2 = registry->resolveVolume(*hB);
    ASSERT_TRUE(texA2.ok()); ASSERT_TRUE(texB2.ok());
    EXPECT_EQ(*texA2, *texB2);
}

} // namespace re::tests
