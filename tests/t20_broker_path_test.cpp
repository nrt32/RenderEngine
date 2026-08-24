// tests/t20_broker_path_test.cpp — T20 gate (broker becomes the ONLY app
// path: complete mapper inventory; no silent drops; no Noop layers).
//
// Asserts (R4 evidence rule — every check is an explainable constant):
//
//   (1) PlaneMapper converts a Space::VoxelIndex plane z=35 to the world
//       point z=35.5 EXACTLY (identity placement + unit spacing: voxel layer
//       i spans [i, i+1], center i+0.5 — the pinned analytic constant), and
//       rejects a VoxelIndex plane without a volume context with typed error
//       code 1 while a World plane passes through unchanged in ANY context.
//   (2) indexPlacementFromModel recovers the pure axis permutation from the
//       canonical MPR display models: applying it to sliceVolumeModel for
//       Coronal/Sagittal yields exactly axisDisplayModels' permutations
//       (analytic identity, 1e-6).
//   (3) A scene store containing MeshObject + VolumeObject + PlaneObject,
//       synced through AppContext/IViewBridge, produces REAL layers (one per
//       view, >= 2 across the scene — no placeholders) whose FBO readback
//       matches the DIRECT renderer oracles (MeshRenderer / VolumeRenderer /
//       PlaneRenderer driven by hand) within 1/255 at the analytic probe
//       pixel, on N = 3 consecutive runs.
//   (4) An item id that resolves to NO scene object is a TYPED ERROR (code
//       11) — never a silently skipped/placeholder layer (the "silent drop"
//       defect class this task kills).
//   (5) The material hand-off is real: two MeshObjects with identical
//       presentation values map to ONE canonical store material
//       (materialSlotCount == 1) and the mapped instance carries a non-null
//       material (the old nullptr placeholder is gone).
//   (6) With OIT enabled, an alpha<1 mesh instance is routed OUT of inline
//       layers into the compositor's capture stage (transparentCount == 1,
//       inline layer count 0 for that object).

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "broker/app_context.hpp"
#include "broker/material_mapper.hpp"
#include "broker/plane_mapper.hpp"
#include "app/mpr_slice.hpp"
#include "broker/slice_display.hpp"
#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/plane_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/view.hpp"
#include "render/volume_renderer.hpp"
#include "scene/object.hpp"
#include "scene/plane_desc.hpp"
#include "scene/translate_context.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {
namespace {

namespace app = re::app;

constexpr std::uint32_t kSize = 64u;               // probe target size
constexpr std::uint32_t kCenter = kSize / 2u;      // 32
constexpr int kTol = 1;                            // 1/255 acceptance

// --- shared fixtures ---------------------------------------------------------

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

data::Mesh makeQuadMesh() {
    std::vector<glm::vec3> positions = {
        glm::vec3(-0.5f, -0.5f, 0.0f), glm::vec3(0.5f, -0.5f, 0.0f),
        glm::vec3(0.5f, 0.5f, 0.0f),   glm::vec3(-0.5f, 0.5f, 0.0f)};
    std::vector<uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
    return data::Mesh::fromTriangles(std::move(positions),
                                     std::move(indices));
}

/// Constant-value 4x4x4 dataset (uniform voxels => closed-form ray-cast).
data::VolumeDataset makeUniformDataset(float value) {
    std::vector<float> voxels(4u * 4u * 4u, value);
    return data::VolumeDataset(4u, 4u, 4u, std::move(voxels));
}

volume::TransferFunction makeOpaqueGreenTf() {
    using CP = volume::TransferFunction::ControlPoint;
    return volume::TransferFunction(
        {CP{0.0f, volume::RgbaColor{0.0f, 0.6f, 0.0f, 1.0f}},
         CP{1.0f, volume::RgbaColor{0.0f, 0.6f, 0.0f, 1.0f}}});
}

/// A flat gradient image w x h (R increases with x, G with y): byte-exact
/// source texels for the textured-plane oracle.
data::Image makeGradientImage(std::int32_t w, std::int32_t h) {
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(w) * h * 4u);
    for (std::int32_t y = 0; y < h; ++y) {
        for (std::int32_t x = 0; x < w; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4u;
            bytes[idx + 0] = static_cast<std::uint8_t>((x * 255) / (w - 1));
            bytes[idx + 1] = static_cast<std::uint8_t>((y * 255) / (h - 1));
            bytes[idx + 2] = 64;
            bytes[idx + 3] = 255;
        }
    }
    return data::Image(w, h, 4, std::move(bytes));
}

/// Perspective camera looking at the origin from +Z, expressed ONCE as the
/// scene value and converted identically for both paths under test.
scene::Camera makeSceneCamera() {
    scene::Camera camera(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f, 0.0f, 0.0f),
                         glm::vec3(0.0f, 1.0f, 0.0f));
    camera.setPerspective(60.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

/// One bridge frame: sync -> renderAll -> presentAll(nullptr).
data::Result<void> driveBridge(broker::AppContext& ctx,
                               const std::vector<scene::View>& views) {
    auto s = ctx.bridge().sync(views, ctx.store());
    if (s.failed()) {
        return s;
    }
    auto r = ctx.bridge().renderAll();
    if (r.failed()) {
        return r;
    }
    return ctx.bridge().presentAll(nullptr);
}

} // namespace

// ---------------------------------------------------------------------------
// (1) PlaneMapper: voxel-index z=35 -> world z=35.5 EXACTLY.
// ---------------------------------------------------------------------------

TEST(T20BrokerPath, PlaneMapperVoxelZ35BecomesWorldZ35Point5Exactly) {
    scene::PlaneDesc desc;
    desc.setNormal(glm::vec3(0.0f, 0.0f, 1.0f));
    desc.setPoint(glm::vec3(0.0f, 0.0f, 35.0f));
    desc.setSpace(scene::Space::VoxelIndex);

    scene::TranslateContext ctx;
    scene::VolumeContext vol;
    vol.volumeModel = glm::mat4(1.0f); // index space IS world space here
    vol.dims = glm::ivec3(128, 128, 70);
    vol.voxelSpacing = 1.0f;
    ctx.volume = vol;

    broker::PlaneMapper mapper;
    auto mapped = mapper.map(desc, ctx);
    ASSERT_TRUE(mapped.ok()) << mapped.error().message;
    // Layer 35's center sits at 35.5 world units — exact float value.
    EXPECT_EQ(mapped->point.z, 35.5f) << "voxel z=35 -> world z=35.5 exactly";
    // The +0.5 center offset applies per-axis; on the free axes it shifts
    // the plane point LATERALLY (0.5 in x/y), which stays IN the z=35.5
    // plane — the plane itself is what the conversion defines.
    EXPECT_FLOAT_EQ(mapped->point.x, 0.5f) << "in-plane lateral offset";
    EXPECT_FLOAT_EQ(mapped->point.y, 0.5f) << "in-plane lateral offset";
    EXPECT_FLOAT_EQ(mapped->normal.z, 1.0f)
        << "normal declared world-space passes through unchanged";

    // World-space planes pass through in ANY context (LSP: absent roles keep
    // preconditions weak).
    scene::PlaneDesc worldDesc;
    worldDesc.setNormal(glm::vec3(0.0f, 1.0f, 0.0f));
    worldDesc.setPoint(glm::vec3(1.0f, 2.0f, 3.0f));
    worldDesc.setSpace(scene::Space::World);
    auto passthrough = mapper.map(worldDesc, scene::TranslateContext{});
    ASSERT_TRUE(passthrough.ok()) << passthrough.error().message;
    EXPECT_EQ(passthrough->point, glm::vec3(1.0f, 2.0f, 3.0f));

    // VoxelIndex WITHOUT a volume context is a typed error, never identity.
    auto missing =
        mapper.map(desc, scene::TranslateContext{}); // no ctx.volume
    ASSERT_TRUE(missing.failed());
    EXPECT_EQ(missing.error().code, 1)
        << "VoxelIndex conversion needs the volume role (typed error)";

    // Degenerate volume context rejected loudly (code 2), never divided by 0.
    scene::VolumeContext bad;
    bad.dims = glm::ivec3(0, 0, 0);
    bad.voxelSpacing = 0.0f;
    scene::TranslateContext badCtx;
    badCtx.volume = bad;
    auto degenerate = mapper.map(desc, badCtx);
    ASSERT_TRUE(degenerate.failed());
    EXPECT_EQ(degenerate.error().code, 2);
}

// ---------------------------------------------------------------------------
// (2) indexPlacementFromModel recovers the MPR display permutations.
// ---------------------------------------------------------------------------

TEST(T20BrokerPath, IndexPlacementRecoversMprAxisPermutations) {
    // A stand-in dataset with distinct dims so the scale-undo matters
    // (voxel values irrelevant — only dims feed sliceVolumeModel).
    data::VolumeDataset dataset(
        64u, 64u, 32u,
        std::vector<float>(64u * 64u * 32u, 0.0f));

    // Coronal: display (x,y,z) = volume (x,z,y); the recovered index
    // placement must be exactly that permutation (sliceVolumeModel minus the
    // unit-cube normalization and half-offset).
    const glm::mat4 coronalModel =
        app::sliceVolumeModel(dataset, app::MprAxis::Coronal);
    const glm::mat4 coronalPlacement = broker::indexPlacementFromModel(
        coronalModel, glm::ivec3(64, 64, 32));
    // Apply to voxel-center coordinate of layer y=10 (+0.5 handled by mapper):
    // expected display point = (x, z, y) permutation of (0.5, 10.5, 0.5).
    const glm::vec3 gotCoronal =
        coronalPlacement * glm::vec4(0.5f, 10.5f, 0.5f, 1.0f);
    EXPECT_NEAR(gotCoronal.x, 0.5f, 1e-6f);
    EXPECT_NEAR(gotCoronal.y, 0.5f, 1e-6f);
    EXPECT_NEAR(gotCoronal.z, 10.5f, 1e-6f)
        << "held Y maps to display Z (coronal permutation recovered)";

    const glm::mat4 sagittalModel =
        app::sliceVolumeModel(dataset, app::MprAxis::Sagittal);
    const glm::mat4 sagittalPlacement = broker::indexPlacementFromModel(
        sagittalModel, glm::ivec3(64, 64, 32));
    // Held X maps to display Z: (10.5, 0.5, 0.5) -> display (0.5, 0.5, 10.5).
    const glm::vec3 gotSagittal =
        sagittalPlacement * glm::vec4(10.5f, 0.5f, 0.5f, 1.0f);
    EXPECT_NEAR(gotSagittal.x, 0.5f, 1e-6f);
    EXPECT_NEAR(gotSagittal.y, 0.5f, 1e-6f);
    EXPECT_NEAR(gotSagittal.z, 10.5f, 1e-6f)
        << "held X maps to display Z (sagittal permutation recovered)";

    // Transverse model is place-only; its placement is the identity (up to
    // the same recovery math).
    const glm::mat4 transversePlacement = broker::indexPlacementFromModel(
        app::sliceVolumeModel(dataset, app::MprAxis::Transverse),
        glm::ivec3(64, 64, 32));
    const glm::vec3 gotTransverse =
        transversePlacement * glm::vec4(0.5f, 0.5f, 20.5f, 1.0f);
    EXPECT_NEAR(gotTransverse.z, 20.5f, 1e-6f);
}

// ---------------------------------------------------------------------------
// (3) Mesh+Volume+Plane through the bridge match direct-renderer oracles.
// ---------------------------------------------------------------------------

TEST(T20BrokerPath, BridgedSceneLayersMatchDirectRendererOracles) {
    constexpr int kRuns = 3; // R10: N>=3 consecutive green runs

    // Shared CPU assets.
    auto quad = std::make_shared<const data::Mesh>(makeQuadMesh());
    auto dataset =
        std::make_shared<const data::VolumeDataset>(makeUniformDataset(0.75f));
    auto tf = makeOpaqueGreenTf();
    auto image =
        std::make_shared<const data::Image>(makeGradientImage(16, 16));

    // Direct-renderer oracles (hand-driven render side, one technique each).
    auto oracleRegistry = std::make_shared<render::AssetRegistry>();
    const auto quadHandle = oracleRegistry->registerAsset(*quad);
    ASSERT_TRUE(quadHandle.ok()) << quadHandle.error().message;

    render::Camera oracleCamera = broker::toRenderCamera(makeSceneCamera());

    render::MeshScene meshOracle;
    meshOracle.meshes.push_back(render::MeshInstance{
        *quadHandle, std::make_shared<render::PhongMaterial>(
                         glm::vec4(0.85f, 0.45f, 0.15f, 1.0f)),
        glm::mat4(1.0f)});

    render::VolumeScene volumeOracle;
    volumeOracle.volumes.push_back(
        render::VolumeInstance{dataset, tf, glm::mat4(1.0f)});

    render::PlaneScene planeOracle;
    {
        render::PlaneInstance inst;
        inst.geometry = std::make_shared<const render::PlaneGeometry>(
            render::PlaneGeometry::unitQuadXY());
        inst.image = image;
        // Map the unit quad onto [-0.5,0.5]^2 facing +Z so the perspective
        // camera sees its full face at the viewport center.
        inst.model = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
        planeOracle.planes.push_back(inst);
    }

    auto oracleTarget = makeTarget(kSize, kSize);
    render::RenderTarget ort;
    ort.framebuffer = &oracleTarget.framebuffer;
    ort.width = kSize;
    ort.height = kSize;
    ort.clearColor = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f);

    render::MeshRenderer meshR(oracleRegistry, nullptr);
    render::VolumeRenderer volumeR(oracleRegistry);
    render::PlaneRenderer planeR(oracleRegistry);
    ASSERT_TRUE(meshR.render(meshOracle, oracleCamera, ort).ok());
    const std::vector<std::uint8_t> meshOraclePx =
        readPixel(oracleTarget.framebuffer, kCenter, kCenter);
    ASSERT_TRUE(volumeR.render(volumeOracle, oracleCamera, ort).ok());
    const std::vector<std::uint8_t> volumeOraclePx =
        readPixel(oracleTarget.framebuffer, kCenter, kCenter);
    ASSERT_TRUE(planeR.render(planeOracle, oracleCamera, ort).ok());
    const std::vector<std::uint8_t> planeOraclePx =
        readPixel(oracleTarget.framebuffer, kCenter, kCenter);

    // Sanity: the three oracles are mutually distinct at the probe (each
    // technique actually drew its own color there — a placeholder layer
    // matching ALL of them is impossible).
    ASSERT_FALSE(meshOraclePx == volumeOraclePx || meshOraclePx == planeOraclePx)
        << "oracles must differ at the probe pixel";

    // ---- Bridge path: one store {Mesh, Volume, Plane}, three views -------
    broker::AppContext ctx(broker::AppContext::Params{});

    scene::MeshObject mo;
    mo.mesh = quad;
    mo.transform = glm::mat4(1.0f);
    mo.presentation.phong.baseColor = glm::vec4(0.85f, 0.45f, 0.15f, 1.0f);
    const uint64_t meshId = ctx.store().addMeshObject(std::move(mo));

    scene::VolumeObject vo;
    vo.volume = dataset;
    vo.transferFunction = tf;
    vo.transform = glm::mat4(1.0f);
    const uint64_t volId = ctx.store().addVolumeObject(std::move(vo));

    scene::PlaneObject po;
    po.image = image;
    po.transform = glm::scale(glm::mat4(1.0f), glm::vec3(0.5f));
    const uint64_t planeId = ctx.store().addPlaneObject(std::move(po));

    std::array<scene::View, 3> views{};
    views[0].id = 1;
    views[1].id = 2;
    views[2].id = 3;
    for (auto& v : views) {
        v.rect = scene::Rect{0, 0, static_cast<int>(kSize),
                             static_cast<int>(kSize)};
        v.camera = makeSceneCamera();
        v.setClearColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.0f));
    }
    views[0].setItemIds({meshId});
    views[1].setItemIds({volId});
    views[2].setItemIds({planeId});

    std::vector<scene::View> frame{views[0], views[1], views[2]};
    for (int run = 1; run <= kRuns; ++run) {
        auto ok = driveBridge(ctx, frame);
        ASSERT_TRUE(ok.ok()) << "run " << run << ": " << ok.error().message;

        // Each view carries EXACTLY one real layer (no placeholder padding).
        auto* meshView = ctx.compositor()->getView(0, 1);
        auto* volumeView = ctx.compositor()->getView(0, 2);
        auto* planeView = ctx.compositor()->getView(0, 3);
        ASSERT_NE(meshView, nullptr);
        ASSERT_NE(volumeView, nullptr);
        ASSERT_NE(planeView, nullptr);
        EXPECT_EQ(meshView->itemCount(), 1u);
        EXPECT_EQ(volumeView->itemCount(), 1u);
        EXPECT_EQ(planeView->itemCount(), 1u);

        // Readback parity with the direct oracles (>= 2 non-Noop layers
        // proven by pixels: a do-nothing layer would show clear color).
        ASSERT_NE(meshView->target(), nullptr);
        ASSERT_NE(volumeView->target(), nullptr);
        ASSERT_NE(planeView->target(), nullptr);
        const auto bridgedMesh =
            readPixel(meshView->target()->framebuffer(), kCenter, kCenter);
        const auto bridgedVolume =
            readPixel(volumeView->target()->framebuffer(), kCenter, kCenter);
        const auto bridgedPlane =
            readPixel(planeView->target()->framebuffer(), kCenter, kCenter);
        for (int c = 0; c < 4; ++c) {
            EXPECT_NEAR(bridgedMesh[c], meshOraclePx[c], kTol)
                << "mesh layer channel " << c << " (run " << run << ")";
            EXPECT_NEAR(bridgedVolume[c], volumeOraclePx[c], kTol)
                << "volume layer channel " << c << " (run " << run << ")";
            EXPECT_NEAR(bridgedPlane[c], planeOraclePx[c], kTol)
                << "plane layer channel " << c << " (run " << run << ")";
        }
    }
}

// ---------------------------------------------------------------------------
// (4) Unknown item id -> typed error (never a silent placeholder).
// ---------------------------------------------------------------------------

TEST(T20BrokerPath, UnknownItemIdIsTypedErrorNotSilentDrop) {
    broker::AppContext ctx(broker::AppContext::Params{});
    scene::View view;
    view.id = 9;
    view.rect = scene::Rect{0, 0, 32, 32};
    view.camera = makeSceneCamera();
    view.setItemIds({7777u}); // nothing in the store has this id

    std::vector<scene::View> frame{view};
    auto result = driveBridge(ctx, frame);
    ASSERT_TRUE(result.failed())
        << "an unresolvable item id must fail sync loudly";
    EXPECT_EQ(result.error().code, 11)
        << "code 11 = unknown item id (no silent drops)";
}

// ---------------------------------------------------------------------------
// (5) Real material hand-off: identical presentation values dedup in the store.
// ---------------------------------------------------------------------------

TEST(T20BrokerPath, MaterialHandOffIsRealAndValueDeduped) {
    auto registry = std::make_shared<render::AssetRegistry>();
    broker::MaterialMapper mapper(registry);
    scene::TranslateContext ctx;

    scene::MeshMaterialDesc desc;
    desc.phong.baseColor = glm::vec4(0.25f, 0.55f, 0.85f, 1.0f);

    auto first = mapper.map(desc, ctx);
    auto second = mapper.map(desc, ctx);
    ASSERT_TRUE(first.ok()) << first.error().message;
    ASSERT_TRUE(second.ok()) << second.error().message;
    ASSERT_NE(*first, nullptr)
        << "mapped material is real (the old nullptr placeholder is gone)";
    EXPECT_EQ((*first)->baseColor(), glm::vec4(0.25f, 0.55f, 0.85f, 1.0f));
    EXPECT_EQ(mapper.cachedCount(), 1u)
        << "identical values share one canonical (explainable)";
    EXPECT_EQ(registry->materialSlotCount(), 1u)
        << "store registered the canonical once (value dedup)";

    // A different value creates a second canonical.
    scene::MeshMaterialDesc other;
    other.phong.baseColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    auto third = mapper.map(other, ctx);
    ASSERT_TRUE(third.ok());
    EXPECT_EQ(mapper.cachedCount(), 2u);
    EXPECT_EQ(registry->materialSlotCount(), 2u);
}

// ---------------------------------------------------------------------------
// (6) Transparent instances route to the compositor capture stage.
// ---------------------------------------------------------------------------

TEST(T20BrokerPath, TransparentInstancesRouteToOitCaptureStage) {
    auto quad = std::make_shared<const data::Mesh>(makeQuadMesh());

    broker::AppContext ctx(
        broker::AppContext::Params{.enableOIT = true,
                                   .registerCameraMapper = false});

    scene::MeshObject glass;
    glass.mesh = quad;
    glass.presentation.phong.baseColor = glm::vec4(0.9f, 0.2f, 0.2f, 0.5f);
    const uint64_t glassId = ctx.store().addMeshObject(std::move(glass));

    scene::View view;
    view.id = 1;
    view.rect = scene::Rect{0, 0, 32, 32};
    view.camera = makeSceneCamera();
    view.setItemIds({glassId});

    std::vector<scene::View> frame{view};
    auto ok = driveBridge(ctx, frame);
    ASSERT_TRUE(ok.ok()) << ok.error().message;

    auto* rv = ctx.compositor()->getView(0, 1);
    ASSERT_NE(rv, nullptr);
    // The alpha<1 instance was NOT added as an inline layer...
    EXPECT_EQ(rv->itemCount(), 0u)
        << "transparent instances never become inline View layers";
    // ...it rides in the compositor's capture stage instead.
    EXPECT_EQ(ctx.compositor()->transparentCount(0, 1), 1u)
        << "exactly one transparent instance pending capture";
}

} // namespace re::tests
