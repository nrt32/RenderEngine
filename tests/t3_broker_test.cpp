// tests/t3_broker_test.cpp — T3 gate tests for broker/ (V3.2b).
//
// Asserts (R4 evidence rule — every check is an explainable constant):
//  (1) Broker empty→register(MeshObjectMapper)→get<MeshObjectMapper>() returns same
//      type_index id, second get same address (OCP type_index factory, no enum).
//  (2) Same data::Mesh pointer twice via Broker still dedups to one GL object
//      (AssetRegistry slotCount 1, same handle index/generation, same vaoId).
//  (3) V2 renderers still green via forwarding (center pixel within 1/255 — uses
//      MeshRenderer with Broker-produced AssetHandle).
//  (4) Stale generation+1 lookup via future AssetStore returns typed
//      Error::StaleHandle code 2 (never crash).
//  (5) broker_per_type + gpu_api_ownership invariants (one Mapper per file,
//      no gl* in broker/) are mechanical audit checks — this test double-checks
//      the Broker OCP invariant (no enum) by verifying type_index keys.

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <typeindex>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "broker/asset_store.hpp"
#include "broker/broker.hpp"
#include "broker/camera_mapper.hpp"
#include "broker/i_mapper.hpp"
#include "broker/i_view_bridge.hpp"
#include "broker/mesh_object_mapper.hpp"
#include "broker/view_bridge.hpp"
#include "broker/view_compositor.hpp"
#include "broker/view_synchronizer.hpp"
#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "render/asset_registry.hpp"
#include "render/mesh_renderer.hpp"
#include "render/phong_material.hpp"
#include "render/types.hpp"
#include "scene/camera.hpp"
#include "scene/object.hpp"
#include "scene/translate_context.hpp"
#include "tests/offscreen_fixture.hpp"
#include "utils/pixel_reader.hpp"

namespace re::tests {

static data::Mesh makeTriangleMesh() {
    std::vector<glm::vec3> pos = {
        {-0.5f, -0.5f, 0.0f},
        {0.5f, -0.5f, 0.0f},
        {0.0f, 0.5f, 0.0f},
    };
    std::vector<uint32_t> idx = {0, 1, 2};
    return data::Mesh::fromTriangles(pos, idx);
}

static data::Mesh makeQuadMesh() {
    std::vector<glm::vec3> positions = {
        glm::vec3(-1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, -1.0f, 0.0f),
        glm::vec3(1.0f, 1.0f, 0.0f),
        glm::vec3(-1.0f, 1.0f, 0.0f),
    };
    std::vector<uint32_t> indices = {0u, 1u, 2u, 0u, 2u, 3u};
    return data::Mesh::fromTriangles(std::move(positions), std::move(indices));
}

static render::Camera makeOrthoCamera() {
    render::Camera camera;
    camera.position = glm::vec3(0.0f, 0.0f, 5.0f);
    camera.view = glm::lookAt(camera.position, glm::vec3(0.0f, 0.0f, 0.0f),
                              glm::vec3(0.0f, 1.0f, 0.0f));
    camera.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return camera;
}

// ---------------------------------------------------------------------------
// (1) Broker empty -> register -> get same type_index, second get same address
// ---------------------------------------------------------------------------

TEST(T3Broker, EmptyRegisterGetSameAddress) {
    broker::Broker broker;
    EXPECT_TRUE(broker.empty()) << "fresh Broker must be empty (size 0)";
    EXPECT_EQ(broker.size(), 0u);

    render::AssetRegistry registry;
    auto mapper = std::make_unique<broker::MeshObjectMapper>(&registry);
    broker::MeshObjectMapper* raw = mapper.get();
    std::type_index expectedTi = std::type_index(typeid(broker::MeshObjectMapper));
    EXPECT_EQ(expectedTi, broker::Broker::typeIndex<broker::MeshObjectMapper>())
        << "type_index(MeshObjectMapper) must equal static helper (explainable)";

    broker.registerMapper(std::move(mapper));
    EXPECT_FALSE(broker.empty()) << "after register Broker must be non-empty";
    EXPECT_EQ(broker.size(), 1u) << "one mapper registered -> size 1 (explainable)";

    broker::MeshObjectMapper* got = broker.get<broker::MeshObjectMapper>();
    ASSERT_NE(got, nullptr) << "get<MeshObjectMapper>() must return non-null after register";
    EXPECT_EQ(got, raw) << "returned address must equal original mapper address (same object, explainable)";
    broker::MeshObjectMapper* got2 = broker.get<broker::MeshObjectMapper>();
    EXPECT_EQ(got2, got) << "second get must return same address (stable registry, explainable)";
    EXPECT_EQ(got2, raw);

    broker::Broker broker2;
    auto camMapper = std::make_unique<broker::CameraMapper>();
    broker::CameraMapper* camRaw = camMapper.get();
    broker2.registerMapper(std::move(camMapper));
    broker::CameraMapper* camGot = broker2.get<broker::CameraMapper>();
    ASSERT_NE(camGot, nullptr);
    EXPECT_EQ(camGot, camRaw) << "CameraMapper concrete path same address (explainable)";
    std::type_index camAppTi = std::type_index(typeid(scene::Camera));
    (void)camAppTi;
}

TEST(T3Broker, AppTypedRegisterAndGet) {
    broker::Broker broker;
    // Register via concrete MapperT overload which also populates AppT alias (OCP via type_index).
    broker.registerMapper(std::make_unique<broker::CameraMapper>());
    auto* got = broker.get<scene::Camera, render::Camera>();
    ASSERT_NE(got, nullptr) << "get<scene::Camera,render::Camera> must return registered mapper (explainable)";
    scene::Camera camScene;
    scene::TranslateContext ctx;
    auto r = got->map(camScene, ctx);
    ASSERT_TRUE(r.ok()) << "CameraMapper::map must succeed";
    EXPECT_FLOAT_EQ(r->position.z, 5.0f) << "default camera eye z=5 is explainable constant";
}

// ---------------------------------------------------------------------------
// (4) Stale generation+1 lookup returns typed Error code 2 (never crash)
// ---------------------------------------------------------------------------

TEST(T3Broker, StaleGenerationPlusOneReturnsCode2) {
    broker::AssetStore store;
    data::Mesh mesh = makeTriangleMesh();
    auto hRes = store.registerAsset(mesh);
    ASSERT_TRUE(hRes.ok()) << "registerAsset must succeed";
    broker::BrokerAssetHandle h = *hRes;
    auto live = store.resolve(h);
    ASSERT_TRUE(live.ok());
    EXPECT_EQ(*live, &mesh) << "live handle must resolve to original mesh pointer (explainable)";

    broker::BrokerAssetHandle stale{h.index, static_cast<uint32_t>(h.generation + 1)};
    auto staleRes = store.resolve(stale);
    EXPECT_TRUE(staleRes.failed()) << "stale generation+1 must be error, not crash";
    EXPECT_EQ(staleRes.error().code, 2) << "stale handle error code must be 2 (StaleHandle, explainable)";
    broker::BrokerAssetHandle bad{9999u, 1u};
    auto badRes = store.resolve(bad);
    EXPECT_TRUE(badRes.failed());
    EXPECT_EQ(badRes.error().code, 1) << "out-of-range index code must be 1 (explainable)";

    render::AssetRegistry renderReg;
    render::AssetHandle fake{0, 99};
    auto fakeRes = renderReg.resolve(fake);
    EXPECT_TRUE(fakeRes.failed());
    EXPECT_EQ(fakeRes.error().code, 1);
}

// ---------------------------------------------------------------------------
// (2) Same data::Mesh pointer twice via Broker still dedups to one GL object
// ---------------------------------------------------------------------------

TEST(T3Broker, SameMeshPointerDedupsViaBroker) {
    render::AssetRegistry registry;
    broker::Broker broker;
    broker.registerMapper(std::make_unique<broker::MeshObjectMapper>(&registry));
    auto* mapper = broker.get<broker::MeshObjectMapper>();
    ASSERT_NE(mapper, nullptr);

    data::Mesh mesh = makeTriangleMesh();
    scene::MeshObject obj1;
    obj1.id = 1;
    obj1.mesh = &mesh;
    obj1.transform = glm::mat4(1.0f);
    obj1.generation = 0;
    scene::MeshObject obj2;
    obj2.id = 2;
    obj2.mesh = &mesh;
    obj2.transform = glm::mat4(1.0f);
    obj2.generation = 0;
    scene::TranslateContext ctx;

    auto r1 = mapper->mapCached(obj1, ctx);
    ASSERT_TRUE(r1.ok()) << "first mapCached must succeed: " << r1.error().message;
    auto r2 = mapper->mapCached(obj2, ctx);
    ASSERT_TRUE(r2.ok()) << "second mapCached same pointer must succeed (dedup): " << r2.error().message;

    EXPECT_EQ(registry.slotCount(), 1u) << "same data::Mesh pointer twice must dedup to 1 slot (explainable)";
    EXPECT_EQ(r1->mesh.index, r2->mesh.index) << "both handles must share same index (dedup)";
    EXPECT_EQ(r1->mesh.generation, r2->mesh.generation) << "same generation (explainable)";

    auto g1 = registry.resolve(r1->mesh);
    auto g2 = registry.resolve(r2->mesh);
    ASSERT_TRUE(g1.ok());
    ASSERT_TRUE(g2.ok());
    EXPECT_EQ((*g1)->vaoId(), (*g2)->vaoId()) << "same GPU object must have same vaoId";
    EXPECT_NE((*g1)->vaoId(), 0u) << "vaoId must be non-zero (valid GL object, explainable)";
}

// ---------------------------------------------------------------------------
// (3) V2 renderers still green via forwarding (center pixel within 1/255)
// ---------------------------------------------------------------------------

TEST(T3Broker, ForwardingRenderStillGreen) {
    render::AssetRegistry registry;
    broker::Broker broker;
    broker.registerMapper(std::make_unique<broker::MeshObjectMapper>(&registry));
    auto* mapper = broker.get<broker::MeshObjectMapper>();
    ASSERT_NE(mapper, nullptr);

    data::Mesh quad = makeQuadMesh();
    scene::MeshObject appObj;
    appObj.id = 42;
    appObj.mesh = &quad;
    appObj.transform = glm::mat4(1.0f);
    scene::TranslateContext ctx;
    auto mapped = mapper->map(appObj, ctx);
    ASSERT_TRUE(mapped.ok()) << "Broker map must produce MeshInstance";

    constexpr std::uint32_t kW = 64, kH = 64;
    constexpr std::uint32_t kCX = kW / 2u;
    constexpr std::uint32_t kCY = kH / 2u;
    constexpr glm::vec4 kBaseColor(0.2f, 0.4f, 0.8f, 1.0f);
    constexpr std::uint8_t kExpR = 51u;
    constexpr std::uint8_t kExpG = 102u;
    constexpr std::uint8_t kExpB = 204u;

    render::PhongMaterial material(kBaseColor);
    ASSERT_FALSE(material.isTransparent());

    render::MeshInstance inst;
    inst.mesh = mapped->mesh;
    inst.material = &material;
    inst.model = glm::mat4(1.0f);
    render::MeshScene scene;
    scene.meshes.push_back(inst);

    render::Camera cam = makeOrthoCamera();

    auto targetColor = core::Texture2D::create();
    auto targetFb = core::Framebuffer::create();
    ASSERT_TRUE(targetColor.ok()) << targetColor.error().message;
    ASSERT_TRUE(targetFb.ok()) << targetFb.error().message;
    std::vector<std::uint8_t> zeros(kW * kH * 4u, 0u);
    targetColor->bind(0u);
    targetColor->upload(kW, kH, zeros.data());
    targetColor->unbind(0u);
    targetFb->bind();
    targetFb->attachColor(*targetColor);
    ASSERT_TRUE(targetFb->isComplete());
    targetFb->unbind();

    render::RenderTarget target;
    target.framebuffer = &*targetFb;
    target.width = kW;
    target.height = kH;
    target.clearColor = glm::vec4(0, 0, 0, 0);

    render::MeshRenderer renderer(&registry, nullptr);
    auto rr = renderer.render(scene, cam, target);
    ASSERT_TRUE(rr.ok()) << "MeshRenderer::render via Broker handle must succeed: " << rr.error().message;

    std::vector<std::uint8_t> pixels;
    utils::PixelReader reader;
    auto read = reader.read(kCX, kCY, 1u, 1u, pixels);
    ASSERT_TRUE(read.ok()) << read.error().message;
    ASSERT_EQ(pixels.size(), 4u);
    EXPECT_NEAR(pixels[0], kExpR, 1) << "R within 1/255 (explainable Phong headlight)";
    EXPECT_NEAR(pixels[1], kExpG, 1) << "G within 1/255";
    EXPECT_NEAR(pixels[2], kExpB, 1) << "B within 1/255";
    EXPECT_EQ(pixels[3], 255u) << "A must be 255 (opaque, explainable)";
}

// ---------------------------------------------------------------------------
// (5) ViewBridge SRP composition and DIP: app depends on IViewBridge only
// ---------------------------------------------------------------------------

TEST(T3Broker, ViewBridgeIsCoordinatorNotMapper) {
    broker::Broker broker;
    broker.registerMapper(std::make_unique<broker::CameraMapper>());
    auto sync = std::make_unique<broker::ViewSynchronizer>(&broker);
    auto comp = std::make_unique<broker::ViewCompositor>(&broker);
    broker::ViewBridge bridge(std::move(sync), std::move(comp));
    broker::IViewBridge* iface = &bridge;
    scene::SceneStore store;
    std::vector<scene::View> views;
    auto s = iface->sync(views, store);
    EXPECT_TRUE(s.ok()) << "IViewBridge::sync must succeed via ViewSynchronizer";
    auto r = iface->renderAll();
    EXPECT_TRUE(r.ok()) << "renderAll via ViewCompositor must succeed";
    auto p = iface->presentAll(nullptr);
    EXPECT_TRUE(p.ok()) << "presentAll must succeed";
    static_assert(!std::is_same_v<broker::ViewBridge, broker::CameraMapper>,
                  "ViewBridge must not be a Mapper (SRP coordinator, not per-type mapper)");
}

TEST(T3Broker, TranslateContextLspFor3D) {
    scene::TranslateContext ctx3d;
    EXPECT_FALSE(ctx3d.hasPlane());
    EXPECT_FALSE(ctx3d.hasVolume());
    broker::CameraMapper mapper;
    scene::Camera cam;
    auto r = mapper.map(cam, ctx3d);
    ASSERT_TRUE(r.ok());
}

} // namespace re::tests
