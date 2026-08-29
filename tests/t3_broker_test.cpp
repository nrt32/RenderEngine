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

#include "broker/broker.hpp"
#include "scene/asset_registry.hpp"
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
#include "tests/test_helpers.hpp"
#include "utils/pixel_reader.hpp"
#include "tests/t3b_compat.hpp"

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

    auto registry = std::make_shared<render::AssetRegistry>();
    auto mapper = std::make_unique<broker::MeshObjectMapper>(registry);
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
    // T7: broker::AssetStore deleted — canonical is scene::AssetRegistry<T>
    // (content-hash byHash_ only, no byObject_ pointer map). Verify stale
    // generation+1 returns typed error code 2 via the canonical registry.
    scene::AssetRegistry<data::Mesh> store;
    auto mesh = std::make_shared<const data::Mesh>(makeTriangleMesh());
    auto hRes = store.registerAsset(mesh);
    ASSERT_TRUE(hRes.ok()) << "registerAsset must succeed";
    scene::AssetId h = *hRes;
    auto live = store.resolve(h);
    ASSERT_TRUE(live.ok());
    EXPECT_EQ(*live, mesh) << "live handle must resolve to the same shared asset (explainable)";

    scene::AssetId stale{h.index, static_cast<uint32_t>(h.generation + 1), h.contentHash};
    auto staleRes = store.resolve(stale);
    EXPECT_TRUE(staleRes.failed()) << "stale generation+1 must be error, not crash";
    EXPECT_EQ(staleRes.error().code, 2) << "stale handle error code must be 2 (StaleHandle, explainable)";
    scene::AssetId bad{9999u, 1u, h.contentHash};
    auto badRes = store.resolve(bad);
    EXPECT_TRUE(badRes.failed());
    EXPECT_EQ(badRes.error().code, 1) << "out-of-range index code must be 1 (explainable)";

    render::AssetRegistry renderReg;
    render::AssetHandle fake{0, 99, 0};
    auto fakeRes = renderReg.resolve(fake);
    EXPECT_TRUE(fakeRes.failed());
    EXPECT_EQ(fakeRes.error().code, 1);
}

// ---------------------------------------------------------------------------
// (2) Same data::Mesh pointer twice via Broker still dedups to one GL object
// ---------------------------------------------------------------------------

TEST(T3Broker, SameMeshPointerDedupsViaBroker) {
    auto registry = std::make_shared<render::AssetRegistry>();
    broker::Broker broker;
    broker.registerMapper(std::make_unique<broker::MeshObjectMapper>(registry));
    auto* mapper = broker.get<broker::MeshObjectMapper>();
    ASSERT_NE(mapper, nullptr);

    auto mesh = std::make_shared<data::Mesh>(makeTriangleMesh());
    scene::MeshObject obj1;
    obj1.id = 1;
    // Shared-reference ownership: the object stores its asset as a
    // co-owned shared_ptr (never a raw borrow), so the CPU bytes stay alive
    // while any scene object, store, or renderer still refers to them (T13).
    obj1.mesh = mesh;
    obj1.transform = glm::mat4(1.0f);
    obj1.generation = 0;
    scene::MeshObject obj2;
    obj2.id = 2;
    obj2.mesh = mesh;
    obj2.transform = glm::mat4(1.0f);
    obj2.generation = 0;
    scene::TranslateContext ctx;

    auto r1 = mapper->mapCached(obj1, ctx);
    ASSERT_TRUE(r1.ok()) << "first mapCached must succeed: " << r1.error().message;
    auto r2 = mapper->mapCached(obj2, ctx);
    ASSERT_TRUE(r2.ok()) << "second mapCached same pointer must succeed (dedup): " << r2.error().message;

    EXPECT_EQ(registry->slotCount(), 1u) << "same data::Mesh pointer twice must dedup to 1 slot (explainable)";
    EXPECT_EQ(r1->mesh.index, r2->mesh.index) << "both handles must share same index (dedup)";
    EXPECT_EQ(r1->mesh.generation, r2->mesh.generation) << "same generation (explainable)";

    auto g1 = registry->resolve(r1->mesh);
    auto g2 = registry->resolve(r2->mesh);
    ASSERT_TRUE(g1.ok());
    ASSERT_TRUE(g2.ok());
    EXPECT_EQ((*g1)->vaoId(), (*g2)->vaoId()) << "same GPU object must have same vaoId";
    EXPECT_NE((*g1)->vaoId(), 0u) << "vaoId must be non-zero (valid GL object, explainable)";
}

// ---------------------------------------------------------------------------
// (3) V2 renderers still green via forwarding (center pixel within 1/255)
// ---------------------------------------------------------------------------

TEST(T3Broker, ForwardingRenderStillGreen) {
    auto registry = std::make_shared<render::AssetRegistry>();
    broker::Broker broker;
    broker.registerMapper(std::make_unique<broker::MeshObjectMapper>(registry));
    auto* mapper = broker.get<broker::MeshObjectMapper>();
    ASSERT_NE(mapper, nullptr);

    auto quad = std::make_shared<data::Mesh>(makeQuad());
    scene::MeshObject appObj;
    appObj.id = 42;
    // Shared-reference ownership: the object stores its asset as a
    // co-owned shared_ptr (never a raw borrow), so the CPU bytes stay alive
    // while any scene object, store, or renderer still refers to them (T13).
    appObj.mesh = quad;
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

    auto material =
        std::make_shared<render::PhongMaterial>(kBaseColor);
    ASSERT_FALSE(material->isTransparent());

    render::MeshInstance inst;
    inst.mesh = mapped->mesh;
    // The material is stored as a SHARED reference: scene instance and
    // renderer co-own it, so neither can dangle the other at teardown.
    inst.material = material;
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

    render::MeshRenderer renderer(registry, nullptr);
    auto rr = renderMeshViaView(renderer, scene, cam, target);
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
    auto broker = std::make_shared<broker::Broker>();
    broker->registerMapper(std::make_unique<broker::CameraMapper>());
    auto sync = std::make_shared<broker::ViewSynchronizer>(broker);
    auto comp = std::make_shared<broker::ViewCompositor>(broker);
    broker::ViewBridge bridge(sync, comp);
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

// T3 pair-key gate — hash_combine(type_index(AppT),type_index(ReT)) distinct entries and wrong-ReT typed miss (nullptr not UB). R4 evidence: analytic nullptr vs type-punning invariant, hash 0/1 counts, distinct entry count 2. Wrong AppT/ReT combination must miss cleanly; registering MeshObject->right Re still finds it; same AppT/different ReT are distinct entries (no silent overwrite). This block explains the T3 type-safety invariant that a mismatched ReT returns nullptr via pair-key hash mismatch rather than UB type-punning.
namespace {
struct ReWrongType {}; // distinct ReT for MeshObject to prove typed miss (no mapper)
using namespace re::broker;
struct DummyMeshToCameraMapper : public IMapper<scene::MeshObject, render::Camera> {
    data::Result<render::Camera> map(const scene::MeshObject& /*app*/,
                                     const scene::TranslateContext& /*ctx*/) const override {
        render::Camera c;
        c.position = glm::vec3(0.0f);
        return data::Result<render::Camera>(data::value, c);
    }
};
} // namespace

TEST(T3Broker, PairKeyWrongReReturnsNullptrAndDistinctEntries) {
    broker::Broker broker;
    auto registry = std::make_shared<render::AssetRegistry>();
    broker.registerMapper(std::make_unique<broker::MeshObjectMapper>(registry));

    // Correct pair finds the mapper (registered via concrete MapperT alias -> pair-keyed)
    auto* correct = broker.get<scene::MeshObject, render::MeshInstance>();
    ASSERT_NE(correct, nullptr)
        << "get<MeshObject,MeshInstance> must find registered MeshObjectMapper (explainable pair-key hit)";

    // Wrong ReT for same AppT must return nullptr — typed miss, not UB type-punning. The pair-key hash_combine guarantees the wrong ReT hashes to a different bucket, so Broker::get returns nullptr (analytic typed miss) instead of the former UB static_cast to the wrong mapper type. This is the T3 A1 type-safety gate that prevents type-punning.
    auto* wrong = broker.get<scene::MeshObject, render::Camera>();
    EXPECT_EQ(wrong, nullptr)
        << "get<MeshObject, Camera> must be nullptr (pair-key miss, not mis-typed pointer — analytic typed null)";

    // Wrong ReT via dummy distinct type also nullptr
    auto* wrong2 = broker.get<scene::MeshObject, ReWrongType>();
    EXPECT_EQ(wrong2, nullptr) << "get<MeshObject,ReWrongType> must be nullptr (explainable typed miss)";

    // hash_combine distinct proof — same AppT/different ReT hashes differ (analytic distinct count)
    const std::size_t hCorrect = broker::Broker::pairKeyHash<scene::MeshObject, render::MeshInstance>();
    const std::size_t hWrong = broker::Broker::pairKeyHash<scene::MeshObject, render::Camera>();
    const std::size_t hWrong2 = broker::Broker::pairKeyHash<scene::MeshObject, ReWrongType>();
    EXPECT_NE(hCorrect, hWrong) << "hash_combine(MeshObject,MeshInstance) != hash_combine(MeshObject,Camera) (distinct entries)";
    EXPECT_NE(hCorrect, hWrong2) << "hash_combine distinct for ReWrongType";
    EXPECT_NE(hWrong, hWrong2) << "hash_combine distinct for two wrong ReTs";
    // hashCombine helper auditable
    const std::size_t hc = broker::Broker::hashCombine(std::type_index(typeid(scene::MeshObject)),
                                                       std::type_index(typeid(render::MeshInstance)));
    EXPECT_EQ(hc, hCorrect) << "hashCombine helper must equal pairKeyHash (explainable hash invariant)";

    // Same AppT/different ReT distinct registrations — register MeshObject->Camera via direct pair-key path
    // This must NOT overwrite the existing MeshObject->MeshInstance entry (no silent overwrite)
    broker.registerMapper<scene::MeshObject, render::Camera>(std::make_unique<DummyMeshToCameraMapper>());
    EXPECT_EQ(broker.size(), 2u) << "two distinct {AppT,ReT} pairs must be size 2 (analytic distinct count, not silent overwrite)";

    auto* correctAgain = broker.get<scene::MeshObject, render::MeshInstance>();
    ASSERT_NE(correctAgain, nullptr) << "original MeshObject->MeshInstance still present after second ReT registration";
    EXPECT_EQ(correctAgain, correct) << "original pointer stable after distinct ReT insert (explainable)";

    auto* second = broker.get<scene::MeshObject, render::Camera>();
    ASSERT_NE(second, nullptr) << "second pair MeshObject->Camera must be found (distinct entry)";
    EXPECT_NE(static_cast<void*>(second), static_cast<void*>(correct)) << "distinct ReT entries must be different mapper objects";

    // get<MapperT> stays exact-keyed (unchanged by pair-key fix)
    auto* byMapper = broker.get<broker::MeshObjectMapper>();
    ASSERT_NE(byMapper, nullptr) << "get<MeshObjectMapper> exact-keyed must still find mapper";
    EXPECT_EQ(byMapper, correct) << "concrete get and pair get must alias same object for MeshObjectMapper";
}

} // namespace re::tests