// tests/t14_unified_asset_store_test.cpp — T14 gate: unified typed multi-kind
// GPU asset store (SPEC §7 T14, FR-render.5/6 regression).
//
// The store is `render::AssetRegistry`, extended from the mesh-only V2/T7
// registry to four kinds (data::Mesh → MeshGeometry kept as-is;
// data::VolumeDataset → core::Texture3D, data::Image → core::Texture2D and
// PhongMaterial value → canonical IMaterial are new), all sharing one
// generational handle contract keyed by (index, generation, contentHash) with
// reference counting + invalidation. The per-renderer pointer/weak-observer
// texture caches are gone.
//
// Asserts (R4 evidence rule — every check pins an explainable constant):
//   (1) Same dataset rendered through TWO default-constructed VolumeRenderer
//       instances: both resolve through the SAME process-wide store, both see
//       ONE Texture3D GL id, and each render reproduces the analytic
//       FR-render.6 center pixel {0, 239, 0, 239} ±1.
//   (2) Identical-content distinct-pointer datasets dedup by content hash in a
//       fresh explicit store: one live slot, equal handles, equal texture
//       pointer; a different-content dataset occupies a second slot (negative
//       control proving the dedup is content-driven).
//   (3) Reference counting + invalidation: register×2 → refs 2; one release
//       keeps the texture resolvable (refs 1); re-registering identical bytes
//       returns the same handle; the second release frees the slot — the stale
//       handle then resolves to typed error code 2 (no crash); out-of-range
//       index resolves to code 1; a fabricated wrong-contentHash handle to
//       code 2. The image kind mirrors this contract.
//   (3b) Material kind mirrors the contract on VALUES: identical Phong value
//       tuples alias to one canonical instance (bit-exact baseColor/shininess
//       round-trip); differing shininess or alpha occupy new slots (alpha
//       drives isTransparent, FR-render.3); refs/stale-handle codes as in (3).
//   (4) Invalidation destroys the GPU object and the lazy renderer path
//       recovers content-addressed: after full release,
//       lookupVolume(dataset) succeeds with a fresh valid texture while the
//       old handle stays dead; lazy lookups never change reference counts.
//   (5) Image kind via TWO default-constructed PlaneRenderer instances: one
//       shared Texture2D id and the analytic FR-render.5 solid center pixel
//       {51, 102, 204, 255} ±1 from both renderers.
//
// Per the GL-ownership + readback guardrails this file uses ONLY core/
// wrappers (including utils::PixelReader for pixel readback) — no raw glXxx calls.

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "core/framebuffer.hpp"
#include "core/gl_error.hpp"
#include "core/texture2d.hpp"
#include "data/image.hpp"
#include "data/volume_dataset.hpp"
#include "render/asset_registry.hpp"
#include "render/phong_material.hpp"
#include "render/plane_renderer.hpp"
#include "render/types.hpp" // render::Camera / render::RenderTarget
#include "render/volume_renderer.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"
#include "volume/color.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

// ---------------------------------------------------------------------------
// Explainable constants.
// ---------------------------------------------------------------------------

// Target framebuffer / viewport size: 64x64 (both analytic setups cover the
// viewport 1:1 with their camera).
constexpr std::uint32_t kTargetWidth = 64u;
constexpr std::uint32_t kTargetHeight = 64u;

// Color tolerance 1/255 (FR-render.5/6).
constexpr int kColorTolerance = 1;

// Uniform synthetic volume: 2x2x2, every voxel = 0.5 (the FR-render.6 gate
// dataset). Compositing four samples of TF {0,1,0,0.5} gives premultiplied
// {0, 0.9375, 0, 0.9375} → bytes {0, round(0.9375*255)=239, 0, 239}.
constexpr std::uint32_t kVolSize = 2u;
constexpr float kUniformVoxel = 0.5f;

// Solid image color {51,102,204,255} = (0.2,0.4,0.8)*255 rounded, alpha opaque
// (the FR-render.5 solid-quad acceptance bytes).
constexpr std::uint8_t kSolidR = 51u;
constexpr std::uint8_t kSolidG = 102u;
constexpr std::uint8_t kSolidB = 204u;
constexpr std::uint8_t kSolidA = 255u;

// ---------------------------------------------------------------------------
// Test helpers (same analytic patterns as t8/t9's gates).
// ---------------------------------------------------------------------------

/// Uniform 2x2x2 volume with every voxel = `value`.
data::VolumeDataset makeUniformDataset(float value) {
    std::vector<float> voxels(
        static_cast<std::size_t>(kVolSize) * kVolSize * kVolSize, value);
    return data::VolumeDataset(kVolSize, kVolSize, kVolSize,
                               std::move(voxels));
}

/// Constant-green transfer function {0,1,0,0.5} at control points 0 and 1.
volume::TransferFunction makeGreenTransferFunction() {
    std::vector<volume::TransferFunction::ControlPoint> points;
    points.push_back({0.0f, volume::RgbaColor{0.0f, 1.0f, 0.0f, 0.5f}});
    points.push_back({1.0f, volume::RgbaColor{0.0f, 1.0f, 0.0f, 0.5f}});
    return volume::TransferFunction(std::move(points));
}

/// Orthographic down-Z cameras mapping NDC [-1,1]^2 onto the full viewport:
/// the FR-render.6 setup centers on the unit cube (eye x=y=0.5), the
/// FR-render.5 setup on the origin-centered unit quad.
render::Camera makeVolumeCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.5f, 0.5f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.5f, 0.5f, 0.5f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

render::Camera makePlaneCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

/// A color-only FBO of w x h ready for renderer targets + readback.
struct RenderedTarget {
    core::Texture2D color;
    core::Framebuffer framebuffer;

    RenderedTarget(core::Texture2D color, core::Framebuffer framebuffer)
        : color(std::move(color)), framebuffer(std::move(framebuffer)) {}
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
    return RenderedTarget(std::move(*color), std::move(*framebuffer));
}

/// Render `scene` with `renderer` into `target` and read back pixel (x, y)
/// (readback coordinates; y = 0 is the bottom scanline).
std::vector<std::uint8_t> renderAndReadPixel(const render::VolumeScene& scene,
                                             render::VolumeRenderer& renderer,
                                             const render::Camera& camera,
                                             RenderedTarget& target,
                                             std::uint32_t x,
                                             std::uint32_t y) {
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    auto result = renderer.render(scene, camera, rt);
    EXPECT_TRUE(result.ok()) << result.error().message;

    std::vector<std::uint8_t> pixels;
    utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    return pixels;
}

std::vector<std::uint8_t> renderAndReadPixel(const render::PlaneScene& scene,
                                             render::PlaneRenderer& renderer,
                                             const render::Camera& camera,
                                             RenderedTarget& target,
                                             std::uint32_t x,
                                             std::uint32_t y) {
    render::RenderTarget rt;
    rt.framebuffer = &target.framebuffer;
    rt.width = kTargetWidth;
    rt.height = kTargetHeight;
    rt.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    auto result = renderer.render(scene, camera, rt);
    EXPECT_TRUE(result.ok()) << result.error().message;

    std::vector<std::uint8_t> pixels;
    utils::PixelReader reader;
    auto read = reader.read(x, y, 1u, 1u, pixels);
    EXPECT_TRUE(read.ok()) << read.error().message;
    EXPECT_EQ(pixels.size(), 4u);
    return pixels;
}

void expectPixelNear(const std::vector<std::uint8_t>& pixel, int r, int g,
                     int b, int a) {
    ASSERT_EQ(pixel.size(), 4u);
    EXPECT_NEAR(pixel[0], r, kColorTolerance) << "R channel";
    EXPECT_NEAR(pixel[1], g, kColorTolerance) << "G channel";
    EXPECT_NEAR(pixel[2], b, kColorTolerance) << "B channel";
    EXPECT_NEAR(pixel[3], a, kColorTolerance) << "A channel";
}

/// The live reference count of a volume slot (asserts resolvability so a
/// broken store surfaces as a failed expectation with context).
std::uint32_t volumeRefsOf(render::AssetRegistry& store,
                           const render::VolumeTextureHandle& handle) {
    auto refs = store.volumeRefs(handle);
    EXPECT_TRUE(refs.ok()) << refs.error().message;
    return refs.ok() ? *refs : 0u;
}

/// A solid RGBA image of w x h in the FR-render.5 acceptance color.
data::Image makeSolidImage(int w, int h) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(w) * h * 4u, 0u);
    for (std::size_t i = 0; i < bytes.size(); i += 4u) {
        bytes[i + 0u] = kSolidR;
        bytes[i + 1u] = kSolidG;
        bytes[i + 2u] = kSolidB;
        bytes[i + 3u] = kSolidA;
    }
    return data::Image(w, h, 4, std::move(bytes));
}

/// A PhongMaterial with the given base color and shininess (all other fields
/// at their defaults), as `shared_ptr<const>` for store registration.
std::shared_ptr<const render::PhongMaterial> makeMaterial(glm::vec4 baseColor,
                                                          float shininess) {
    auto m = std::make_shared<render::PhongMaterial>(baseColor);
    m->shininess = shininess;
    return m;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) Two default-constructed VolumeRenderer instances share ONE Texture3D.
// ---------------------------------------------------------------------------

TEST(T14UnifiedAssetStore, TwoDefaultVolumeRenderersShareOneTexture3D) {
    // Default construction must hand both renderers the SAME process-wide
    // store — that identity is what makes cross-instance dedup possible.
    render::VolumeRenderer r1;
    render::VolumeRenderer r2;
    ASSERT_NE(r1.assets(), nullptr);
    EXPECT_EQ(r1.assets().get(), r2.assets().get())
        << "default-constructed renderers must resolve through one store";

    auto dataset =
        std::make_shared<const data::VolumeDataset>(makeUniformDataset(kUniformVoxel));
    volume::TransferFunction tf = makeGreenTransferFunction();
    render::VolumeScene scene;
    scene.volumes.push_back(render::VolumeInstance{dataset, tf, glm::mat4(1.0f)});

    RenderedTarget targetA = makeTarget(kTargetWidth, kTargetHeight);
    RenderedTarget targetB = makeTarget(kTargetWidth, kTargetHeight);

    const std::vector<std::uint8_t> pixelA = renderAndReadPixel(
        scene, r1, makeVolumeCamera(), targetA, kTargetWidth / 2u,
        kTargetHeight / 2u);
    const std::vector<std::uint8_t> pixelB = renderAndReadPixel(
        scene, r2, makeVolumeCamera(), targetB, kTargetWidth / 2u,
        kTargetHeight / 2u);

    // Both renders hit the same GPU texture and reproduce the analytic
    // FR-render.6 constant: premultiplied {0, 0.9375, 0, 0.9375} → bytes
    // {0, 239, 0, 239}.
    expectPixelNear(pixelA, 0, 239, 0, 239);
    expectPixelNear(pixelB, 0, 239, 0, 239);

    // One GPU object behind both renderers: resolving the dataset's content
    // through either store reference yields the SAME texture pointer and GL id
    // (GL reserves 0, so a live texture name is non-zero).
    auto texViaRenderer1 = r1.assets()->lookupVolume(dataset);
    auto texViaRenderer2 = r2.assets()->lookupVolume(dataset);
    ASSERT_TRUE(texViaRenderer1.ok()) << texViaRenderer1.error().message;
    ASSERT_TRUE(texViaRenderer2.ok()) << texViaRenderer2.error().message;
    EXPECT_EQ(*texViaRenderer1, *texViaRenderer2)
        << "two renderer instances must share ONE Texture3D object";
    EXPECT_EQ((*texViaRenderer1)->id(), (*texViaRenderer2)->id());
    EXPECT_NE((*texViaRenderer1)->id(), 0u)
        << "a live GL texture name is non-zero (GL reserves 0)";
    EXPECT_TRUE(core::hasPendingGlError() == false);
}

// ---------------------------------------------------------------------------
// (2) Content-hash dedup across distinct pointers (fresh explicit store so
//     slot counts are deterministic).
// ---------------------------------------------------------------------------

TEST(T14UnifiedAssetStore, DistinctPointerIdenticalVoxelsDedupByContentHash) {
    auto store = std::make_shared<render::AssetRegistry>();

    // Distinct allocations, byte-identical voxels.
    auto dsA = std::make_shared<const data::VolumeDataset>(
        makeUniformDataset(kUniformVoxel));
    auto dsB = std::make_shared<const data::VolumeDataset>(
        makeUniformDataset(kUniformVoxel));
    ASSERT_NE(dsA.get(), dsB.get())
        << "distinct allocations must have distinct addresses";

    auto hA = store->registerVolume(dsA);
    auto hB = store->registerVolume(dsB);
    ASSERT_TRUE(hA.ok()) << hA.error().message;
    ASSERT_TRUE(hB.ok()) << hB.error().message;

    EXPECT_EQ(hA->index, hB->index) << "identical content aliases to one slot";
    EXPECT_EQ(hA->generation, hB->generation);
    EXPECT_EQ(hA->contentHash, hB->contentHash);
    EXPECT_EQ(store->volumeSlotCount(), 1u)
        << "two identical-content datasets occupy exactly one slot";

    auto texA = store->resolveVolume(*hA);
    auto texB = store->resolveVolume(*hB);
    ASSERT_TRUE(texA.ok());
    ASSERT_TRUE(texB.ok());
    EXPECT_EQ(*texA, *texB) << "one GPU object behind both handles";

    // Negative control: different voxel bytes → different hash → a second
    // slot. (Uniform 0.25 vs uniform 0.5 differ in every float byte pattern.)
    auto dsC = std::make_shared<const data::VolumeDataset>(
        makeUniformDataset(kUniformVoxel * 0.5f));
    auto hC = store->registerVolume(dsC);
    ASSERT_TRUE(hC.ok());
    EXPECT_NE(hC->contentHash, hA->contentHash)
        << "different voxel bytes must hash differently";
    EXPECT_NE(hC->index, hA->index);
    EXPECT_EQ(store->volumeSlotCount(), 2u)
        << "one slot per distinct content (1 shared + 1 distinct)";

    // Null input is a typed error, never a crash (code 4 like AssetStore).
    auto nullRes = store->registerVolume(nullptr);
    EXPECT_TRUE(nullRes.failed());
    EXPECT_EQ(nullRes.error().code, 4);
}

// ---------------------------------------------------------------------------
// (3) Reference counting + invalidation (volume kind).
// ---------------------------------------------------------------------------

TEST(T14UnifiedAssetStore, VolumeRefcountReleaseInvalidatesStaleHandles) {
    auto store = std::make_shared<render::AssetRegistry>();
    auto dsA = std::make_shared<const data::VolumeDataset>(
        makeUniformDataset(kUniformVoxel));
    auto dsCopy = std::make_shared<const data::VolumeDataset>(
        makeUniformDataset(kUniformVoxel)); // distinct pointer, same bytes

    auto h1 = store->registerVolume(dsA);
    ASSERT_TRUE(h1.ok());
    EXPECT_EQ(volumeRefsOf(*store, *h1), 1u)
        << "first registration holds one reference";

    auto h2 = store->registerVolume(dsCopy);
    ASSERT_TRUE(h2.ok());
    EXPECT_EQ(*h1 == *h2, true) << "identical content returns the same handle";
    EXPECT_EQ(volumeRefsOf(*store, *h1), 2u)
        << "second registration takes one more reference (refs 2)";

    // First release: refs 2 → 1, texture stays alive and resolvable.
    auto rel1 = store->unregisterVolume(*h1);
    ASSERT_TRUE(rel1.ok()) << rel1.error().message;
    EXPECT_EQ(volumeRefsOf(*store, *h2), 1u);
    auto stillLive = store->resolveVolume(*h2);
    ASSERT_TRUE(stillLive.ok())
        << "co-owned texture must survive its first release";
    EXPECT_EQ(store->volumeSlotCount(), 1u);

    // Second release: last reference drops → destroy + invalidate.
    auto rel2 = store->unregisterVolume(*h2);
    ASSERT_TRUE(rel2.ok());
    EXPECT_EQ(store->volumeSlotCount(), 0u) << "slot freed at zero references";

    auto dead = store->resolveVolume(*h2);
    EXPECT_TRUE(dead.failed()) << "stale handle after free must be an error";
    EXPECT_EQ(dead.error().code, 2)
        << "generation was bumped at free time → code 2 (stale handle)";
    auto deadOldHandle = store->resolveVolume(*h1);
    EXPECT_TRUE(deadOldHandle.failed());

    // Fabricated handles are rejected too: right index, wrong generation /
    // wrong content hash.
    render::VolumeTextureHandle badGen{h2->index, h2->generation + 1u,
                                       h2->contentHash};
    auto genErr = store->resolveVolume(badGen);
    EXPECT_TRUE(genErr.failed());
    EXPECT_EQ(genErr.error().code, 2);
    render::VolumeTextureHandle badHash{h2->index, h2->generation,
                                        h2->contentHash ^ 0xFFU};
    auto hashErr = store->resolveVolume(badHash);
    EXPECT_TRUE(hashErr.failed());
    EXPECT_EQ(hashErr.error().code, 2);

    // Out-of-range index → code 1.
    render::VolumeTextureHandle far{9999u, 1u, h2->contentHash};
    auto rangeErr = store->resolveVolume(far);
    EXPECT_TRUE(rangeErr.failed());
    EXPECT_EQ(rangeErr.error().code, 1);

    // Double-release of a freed handle is also a typed error (no crash).
    auto again = store->unregisterVolume(*h2);
    EXPECT_TRUE(again.failed());
}

TEST(T14UnifiedAssetStore, ImageRefcountMirrorsVolumeContract) {
    auto store = std::make_shared<render::AssetRegistry>();
    auto imgA = std::make_shared<const data::Image>(makeSolidImage(8, 8));
    auto imgCopy = std::make_shared<const data::Image>(makeSolidImage(8, 8));

    auto h1 = store->registerImage(imgA);
    ASSERT_TRUE(h1.ok());
    auto h2 = store->registerImage(imgCopy);
    ASSERT_TRUE(h2.ok());
    EXPECT_EQ(*h1 == *h2, true) << "identical pixels alias to one slot";
    EXPECT_EQ(store->imageSlotCount(), 1u);

    auto h1Refs = store->imageRefs(*h1);
    ASSERT_TRUE(h1Refs.ok()) << h1Refs.error().message;
    EXPECT_EQ(*h1Refs, 2u) << "two registrations → refs 2";

    auto tex1 = store->resolveImage(*h1);
    ASSERT_TRUE(tex1.ok());
    EXPECT_NE((*tex1)->id(), 0u) << "live GL texture name is non-zero";

    ASSERT_TRUE(store->unregisterImage(*h1).ok());
    ASSERT_TRUE(store->unregisterImage(*h2).ok());
    EXPECT_EQ(store->imageSlotCount(), 0u);
    auto dead = store->resolveImage(*h2);
    EXPECT_TRUE(dead.failed());
    EXPECT_EQ(dead.error().code, 2)
        << "stale image handle after free → code 2 (typed error, no crash)";

    auto nullRes = store->lookupImage(nullptr);
    EXPECT_TRUE(nullRes.failed());
    EXPECT_EQ(nullRes.error().code, 4);
}

// ---------------------------------------------------------------------------
// (3b) Material kind: value-dedup + refcount + invalidation mirror the
//      texture kinds (T14 DoD: "material slot with real dedup").
// ---------------------------------------------------------------------------

TEST(T14UnifiedAssetStore, MaterialValueDedupRefcountInvalidation) {
    auto store = std::make_shared<render::AssetRegistry>();

    // The FR-render.1 acceptance base color (0.2, 0.4, 0.8, 1.0) with the
    // PhongMaterial default shininess 32: two DISTINCT allocations carrying
    // IDENTICAL values must alias to one canonical instance.
    constexpr glm::vec4 kBase(0.2f, 0.4f, 0.8f, 1.0f);
    constexpr float kShininess = 32.0f;
    auto matA = makeMaterial(kBase, kShininess);
    auto matB = makeMaterial(kBase, kShininess);
    ASSERT_NE(matA.get(), matB.get())
        << "distinct allocations must have distinct addresses";

    auto h1 = store->registerMaterial(matA);
    auto h2 = store->registerMaterial(matB);
    ASSERT_TRUE(h1.ok()) << h1.error().message;
    ASSERT_TRUE(h2.ok()) << h2.error().message;
    EXPECT_EQ(*h1 == *h2, true)
        << "identical material values alias to one slot";
    EXPECT_EQ(store->materialSlotCount(), 1u);

    auto mRefs = store->materialRefs(*h1);
    ASSERT_TRUE(mRefs.ok()) << mRefs.error().message;
    EXPECT_EQ(*mRefs, 2u) << "two registrations take two references";

    // The canonical is a byte-exact clone of the registered value: baseColor
    // components and shininess compare exactly (plain float member copies),
    // and the derived transparency flag matches (alpha == 1 → opaque).
    auto canon = store->resolveMaterial(*h1);
    ASSERT_TRUE(canon.ok()) << canon.error().message;
    ASSERT_NE(*canon, nullptr);
    constexpr glm::vec4 kExactBase = kBase; // bit-exact copy through ctor
    EXPECT_EQ((*canon)->baseColor() == kExactBase, true)
        << "canonical baseColor equals the registered value exactly";
    EXPECT_EQ((*canon)->isTransparent(), false)
        << "alpha 1.0 canonical is opaque (FR-render.3 ⇔ a < 1)";
    {
        const render::PhongMaterial* asPhong =
            dynamic_cast<const render::PhongMaterial*>(*canon);
        ASSERT_NE(asPhong, nullptr);
        EXPECT_EQ(asPhong->shininess, kShininess)
            << "canonical shininess equals the registered value";
        EXPECT_FLOAT_EQ(asPhong->specular.r, matA->specular.r);
    }

    // Negative control 1: a different shininess value is a DIFFERENT content
    // → its own slot (value identity covers every field).
    auto hDiff = store->registerMaterial(makeMaterial(kBase, 64.0f));
    ASSERT_TRUE(hDiff.ok());
    EXPECT_NE(hDiff->contentHash, h1->contentHash)
        << "differing shininess must hash differently";
    EXPECT_NE(hDiff->index, h1->index);
    EXPECT_EQ(store->materialSlotCount(), 2u);

    // Negative control 2: the alpha that drives isTransparent participates —
    // same RGB but alpha 0.5 is different content (FR-render.3 invariant).
    auto hTranslucent =
        store->registerMaterial(makeMaterial(glm::vec4(0.2f, 0.4f, 0.8f, 0.5f),
                                             kShininess));
    ASSERT_TRUE(hTranslucent.ok());
    EXPECT_NE(hTranslucent->contentHash, h1->contentHash)
        << "alpha participates in material identity";
    EXPECT_EQ(store->materialSlotCount(), 3u);

    // Ref-count contract: release ×2 frees the slot and invalidates handles.
    ASSERT_TRUE(store->unregisterMaterial(*h1).ok());
    ASSERT_TRUE(store->resolveMaterial(*h2).ok())
        << "one reference outstanding keeps the canonical alive";
    ASSERT_TRUE(store->unregisterMaterial(*h2).ok());
    EXPECT_EQ(store->materialSlotCount(), 2u)
        << "only the identical-value pair's slot is freed";
    auto dead = store->resolveMaterial(*h2);
    EXPECT_TRUE(dead.failed()) << "stale handle must be an error";
    EXPECT_EQ(dead.error().code, 2)
        << "generation bumped at free time → code 2 (typed error, no crash)";

    // Fabricated wrong-contentHash handle → code 2.
    render::MaterialHandle badHash{h2->index, h2->generation,
                                   h2->contentHash ^ 0xFFU};
    auto hashErr = store->resolveMaterial(badHash);
    EXPECT_TRUE(hashErr.failed());
    EXPECT_EQ(hashErr.error().code, 2);

    // Double-release of the freed handle is also a typed error (no crash).
    auto again = store->unregisterMaterial(*h2);
    EXPECT_TRUE(again.failed());

    // Null input is a typed error (code 4), never a crash.
    auto nullRes = store->registerMaterial(nullptr);
    EXPECT_TRUE(nullRes.failed());
    EXPECT_EQ(nullRes.error().code, 4);
}

// ---------------------------------------------------------------------------
// (4) Lazy renderer path: no ref-count churn + content-addressed recovery
//     after invalidation.
// ---------------------------------------------------------------------------

TEST(T14UnifiedAssetStore, LazyLookupNeverChurnsRefsAndRecoversAfterFree) {
    auto store = std::make_shared<render::AssetRegistry>();
    auto ds = std::make_shared<const data::VolumeDataset>(
        makeUniformDataset(kUniformVoxel));

    auto handle = store->registerVolume(ds);
    ASSERT_TRUE(handle.ok());
    auto originalRes = store->resolveVolume(*handle);
    ASSERT_TRUE(originalRes.ok()) << originalRes.error().message;
    core::Texture3D* original = *originalRes;
    ASSERT_NE(original, nullptr);
    EXPECT_TRUE(original->valid());

    // Lazy lookups (the renderer path) neither add nor remove references.
    auto lazy1Res = store->lookupVolume(ds);
    ASSERT_TRUE(lazy1Res.ok()) << lazy1Res.error().message;
    core::Texture3D* lazy1 = *lazy1Res;
    EXPECT_EQ(lazy1, original) << "lookup finds the registered object";
    EXPECT_EQ(volumeRefsOf(*store, *handle), 1u)
        << "lazy lookup must not change the reference count";
    auto lazy2Res = store->lookupVolume(ds);
    ASSERT_TRUE(lazy2Res.ok());
    EXPECT_EQ(*lazy2Res, original);
    EXPECT_EQ(volumeRefsOf(*store, *handle), 1u);

    // Full release invalidates the entry...
    ASSERT_TRUE(store->unregisterVolume(*handle).ok());
    EXPECT_EQ(store->volumeSlotCount(), 0u);
    auto dead = store->resolveVolume(*handle);
    ASSERT_TRUE(dead.failed());
    EXPECT_EQ(dead.error().code, 2);

    // ...but the lazy path recovers content-addressed: a fresh upload serves
    // the next frame (no stale GPU data can ever be served for freed
    // content).
    auto recoveredRes = store->lookupVolume(ds);
    ASSERT_TRUE(recoveredRes.ok()) << recoveredRes.error().message;
    core::Texture3D* recovered = *recoveredRes;
    ASSERT_NE(recovered, nullptr);
    EXPECT_TRUE(recovered->valid())
        << "after invalidation the store re-uploads on demand";
    EXPECT_EQ(store->volumeSlotCount(), 1u);
}

// ---------------------------------------------------------------------------
// (5) Image kind end-to-end: two default PlaneRenderers share one Texture2D.
// ---------------------------------------------------------------------------

TEST(T14UnifiedAssetStore, TwoDefaultPlaneRenderersShareOneTexture2D) {
    render::PlaneRenderer r1;
    render::PlaneRenderer r2;
    ASSERT_NE(r1.assets(), nullptr);
    EXPECT_EQ(r1.assets().get(), r2.assets().get())
        << "default-constructed plane renderers share one store";

    auto image = std::make_shared<const data::Image>(makeSolidImage(64, 64));
    auto geometry = std::make_shared<const render::PlaneGeometry>(
        render::PlaneGeometry::unitQuadXY());
    render::PlaneScene scene;
    scene.planes.push_back(
        render::PlaneInstance{geometry, image, glm::mat4(1.0f)});

    RenderedTarget targetA = makeTarget(kTargetWidth, kTargetHeight);
    RenderedTarget targetB = makeTarget(kTargetWidth, kTargetHeight);

    // The quad covers the viewport 1:1 and the texture is solid, so the
    // center pixel equals the source color {51,102,204,255} ±1 from BOTH
    // renderers (FR-render.5 acceptance bytes).
    const std::vector<std::uint8_t> pixelA = renderAndReadPixel(
        scene, r1, makePlaneCamera(), targetA, kTargetWidth / 2u,
        kTargetHeight / 2u);
    const std::vector<std::uint8_t> pixelB = renderAndReadPixel(
        scene, r2, makePlaneCamera(), targetB, kTargetWidth / 2u,
        kTargetHeight / 2u);
    expectPixelNear(pixelA, kSolidR, kSolidG, kSolidB, kSolidA);
    expectPixelNear(pixelB, kSolidR, kSolidG, kSolidB, kSolidA);

    auto tex1 = r1.assets()->lookupImage(image);
    auto tex2 = r2.assets()->lookupImage(image);
    ASSERT_TRUE(tex1.ok());
    ASSERT_TRUE(tex2.ok());
    EXPECT_EQ(*tex1, *tex2)
        << "two plane renderer instances must share ONE Texture2D object";
    EXPECT_EQ((*tex1)->id(), (*tex2)->id());
    EXPECT_NE((*tex1)->id(), 0u);
}

// ---------------------------------------------------------------------------
// (6) Explicit-store injection isolates stores: two renderers constructed
//     with a FRESH registry produce exactly one volume slot there.
// ---------------------------------------------------------------------------

TEST(T14UnifiedAssetStore, ExplicitStoreIsolationKeepsCountsExact) {
    auto store = std::make_shared<render::AssetRegistry>();
    render::VolumeRenderer r1(store);
    render::VolumeRenderer r2(store);
    EXPECT_EQ(r1.assets(), store);
    EXPECT_EQ(r2.assets(), store);

    auto dataset = std::make_shared<const data::VolumeDataset>(
        makeUniformDataset(kUniformVoxel));
    volume::TransferFunction tf = makeGreenTransferFunction();
    render::VolumeScene scene;
    scene.volumes.push_back(render::VolumeInstance{dataset, tf, glm::mat4(1.0f)});

    RenderedTarget targetA = makeTarget(kTargetWidth, kTargetHeight);
    RenderedTarget targetB = makeTarget(kTargetWidth, kTargetHeight);
    auto px1 = renderAndReadPixel(scene, r1, makeVolumeCamera(), targetA,
                                  kTargetWidth / 2u, kTargetHeight / 2u);
    auto px2 = renderAndReadPixel(scene, r2, makeVolumeCamera(), targetB,
                                  kTargetWidth / 2u, kTargetHeight / 2u);
    expectPixelNear(px1, 0, 239, 0, 239);
    expectPixelNear(px2, 0, 239, 0, 239);

    // Deterministic count in the isolated store: two renderer instances × the
    // same dataset = exactly ONE lazily-created slot (zero references — the
    // renderers never claim ownership).
    EXPECT_EQ(store->volumeSlotCount(), 1u);
}

} // namespace re::tests
