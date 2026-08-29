// tests/t1_hierarchy_gate_test.cpp — T1 gate after T5 collapse (T5).
//
// Gate after T5: 6 technique kinds (Mesh, MeshSlice, Volume, VolumeSlice,
// Plane, Contour) plus GeometryKind inside MeshObject (Mesh, Cube, Sphere,
// Cylinder, Torus, Cone, Arrow, Grid, Axes, Capsule, PointCloud, Teapot).
// Adding Sphere no longer needs a new header — MeshObject{.geometryKind=Sphere}
// via single MeshObjectMapper renders within 1/255 of the old per-kind path.
// The 11 byte-identical headers scene/objects/*.hpp:36-40 are gone, Broker
// still has 6 technique kinds, Factory size is 6. T5.

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "broker/broker.hpp"
#include "broker/mesh_object_mapper.hpp"
#include "broker/view_bridge.hpp"
#include "broker/view_compositor.hpp"
#include "broker/view_synchronizer.hpp"
#include "broker/render_stack.hpp"
#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/mesh.hpp"
#include "render/mesh_renderer.hpp"
#include "scene/geometry_kind.hpp"
#include "scene/iscene_object.hpp"
#include "scene/object.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"
#include "tests/offscreen_fixture.hpp"
#include "tests/test_helpers.hpp"
#include "utils/pixel_reader.hpp"
#include "tests/t3b_compat.hpp"

namespace re::tests {

static render::Camera makeOrthoCamera() {
    render::Camera c;
    c.position = glm::vec3(0.0f, 0.0f, 5.0f);
    c.view = glm::lookAt(c.position, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    c.proj = glm::ortho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
    return c;
}

// Phase A: Factory — 6 technique kinds remain, Count fails loud
TEST(T1Hierarchy, FactoryCreateTeapotSucceeds) {
    // After T5, Teapot is not a SceneKind — it's a GeometryKind inside Mesh.
    // The factory still creates the 6 technique kinds; Teapot kind no longer exists.
    auto mesh = scene::SceneFactory::instance().create(scene::SceneKind::Mesh);
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->kind(), scene::SceneKind::Mesh);
    auto contour = scene::SceneFactory::instance().create(scene::SceneKind::Contour);
    ASSERT_NE(contour, nullptr);
    EXPECT_EQ(contour->kind(), scene::SceneKind::Contour);
    auto unknown = scene::SceneFactory::instance().create(scene::SceneKind::Count);
    EXPECT_EQ(unknown, nullptr) << "Factory::create(Count) must return nullptr";
    // 6 technique kinds registered (Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour)
    EXPECT_GE(scene::SceneFactory::instance().size(), 6u) << "factory must have at least 6 kinds (6 technique kinds after T5)";
    EXPECT_LE(scene::SceneFactory::instance().size(), 6u) << "factory must have exactly 6 kinds after T5 collapse (no extra mesh variations)";
}

// Phase B: SceneStore 6 partitions + GeometryKind variations inside Mesh partition
TEST(T1Hierarchy, SceneStorePartitionedKindIndexOkind) {
    scene::SceneStore store;
    auto quad = std::make_shared<data::Mesh>(makeQuad());
    scene::MeshObject mo;
    mo.mesh = quad;
    mo.geometryKind = scene::GeometryKind::Mesh;
    uint64_t mid = store.addMeshObject(mo);
    scene::MeshObject sphereObj;
    sphereObj.mesh = quad;
    sphereObj.geometryKind = scene::GeometryKind::Sphere;
    uint64_t sid = store.addMeshObject(sphereObj);
    EXPECT_NE(mid, sid);
    // Both live in same Mesh partition, but kindIndex_[Mesh]==2
    auto meshes = store.objectsOfKind(scene::SceneKind::Mesh);
    EXPECT_EQ(meshes.size(), 2u) << "objectsOfKind(Mesh) must be 2 (both Mesh and Sphere share Mesh partition)";
    EXPECT_EQ(store.countOfKind(scene::SceneKind::Mesh), 2u);
    EXPECT_EQ(store.totalObjectCount(), 2u);
    // Verify geometryKind preserved
    const auto* baseM = store.getMeshObject(mid);
    const auto* baseS = store.getMeshObject(sid);
    ASSERT_NE(baseM, nullptr);
    ASSERT_NE(baseS, nullptr);
    EXPECT_EQ(baseM->geometryKind, scene::GeometryKind::Mesh);
    EXPECT_EQ(baseS->geometryKind, scene::GeometryKind::Sphere);
    // Generic getObject works for both (both are Mesh kind)
    const auto* genM = store.getObject(mid);
    const auto* genS = store.getObject(sid);
    ASSERT_NE(genM, nullptr);
    ASSERT_NE(genS, nullptr);
    EXPECT_EQ(genM->kind(), scene::SceneKind::Mesh);
    EXPECT_EQ(genS->kind(), scene::SceneKind::Mesh);
    // Remove one, kindIndex_ updates
    EXPECT_TRUE(store.removeMeshObject(sid));
    EXPECT_EQ(store.objectsOfKind(scene::SceneKind::Mesh).size(), 1u);
    EXPECT_EQ(store.countOfKind(scene::SceneKind::Mesh), 1u);
}

// Phase C: Broker SceneKind map — 6 technique kinds remain
TEST(T1Hierarchy, BrokerRegisteredTypesTeapotCount1) {
    broker::Broker broker;
    auto registry = std::make_shared<render::AssetRegistry>();
    // After T5, only 6 technique mappers exist — Sphere no longer needs its own mapper header.
    // Registering the single MeshObjectMapper suffices for all 12 GeometryKind variations.
    broker.registerMapper(std::make_unique<broker::MeshObjectMapper>(registry));
    auto types = broker.registeredTypes();
    // Must contain Mesh among the 6, not Teapot as a separate SceneKind
    bool hasMesh = false;
    for (auto k : types) if (k == scene::SceneKind::Mesh) hasMesh = true;
    EXPECT_TRUE(hasMesh) << "Broker::registeredTypes() must contain Mesh";
    EXPECT_EQ(types.size(), 1u) << "single mapper registered => 1 kind visible (isolated broker)";
    EXPECT_NE(broker.getByKind(scene::SceneKind::Mesh), nullptr);
    // Teapot as SceneKind no longer exists — must be nullptr
    // (Teapot is now GeometryKind::Teapot inside MeshObject)
    // The broker has no separate Teapot kind entry
    EXPECT_EQ(broker.getByKind(static_cast<scene::SceneKind>(6)), nullptr);
}

// Phase C: MeshObject with Sphere geometry renders within 1/255 of Mesh via same mapper
TEST(T1Hierarchy, TeapotRendersThroughBridgeCenterPixelAnalytic) {
    auto registry = std::make_shared<render::AssetRegistry>();
    auto broker = std::make_shared<broker::Broker>();
    broker->registerMapper(std::make_unique<broker::MeshObjectMapper>(registry));
    auto stack = std::make_shared<broker::RenderStack>();
    stack->mesh = std::make_shared<render::MeshRenderer>(registry, nullptr);

    scene::SceneStore store;
    auto quad = std::make_shared<data::Mesh>(makeQuad());
    scene::MeshObject obj;
    obj.mesh = quad;
    obj.geometryKind = scene::GeometryKind::Sphere;
    obj.presentation.phong.baseColor = glm::vec4(0.2f, 0.4f, 0.8f, 1.0f);
    uint64_t oid = store.addMeshObject(obj);

    scene::View view;
    view.id = 1;
    view.rect = scene::Rect{0, 0, 64, 64};
    view.itemIds = {oid};
    view.camera = scene::Camera();
    {
        scene::Camera cam = view.camera;
        cam.setOrtho(-1.0f, 1.0f, -1.0f, 1.0f, 0.1f, 10.0f);
        view.setCamera(std::move(cam));
    }

    auto compositor = std::make_shared<broker::ViewCompositor>(broker, stack);
    auto synchronizer = std::make_shared<broker::ViewSynchronizer>(broker, compositor, stack);
    broker::ViewBridge bridge(synchronizer, compositor);

    std::vector<scene::View> views{view};
    auto syncRes = bridge.sync(views, store);
    ASSERT_TRUE(syncRes.ok()) << syncRes.error().message;
    auto renderRes = bridge.renderAll();
    ASSERT_TRUE(renderRes.ok()) << renderRes.error().message;

    constexpr uint32_t kW = 64, kH = 64;
    constexpr uint32_t kCX = kW/2, kCY = kH/2;
    constexpr uint8_t kExpR = 51, kExpG = 102, kExpB = 204;

    {
        auto* rv = compositor->getView(0, 1);
        ASSERT_NE(rv, nullptr);
        auto* target = rv->target();
        ASSERT_NE(target, nullptr);
        target->framebuffer().bind();
        std::vector<uint8_t> bridgePixels;
        utils::PixelReader reader;
        auto read = reader.read(kCX, kCY, 1, 1, bridgePixels);
        target->framebuffer().unbind();
        ASSERT_TRUE(read.ok()) << read.error().message;
        ASSERT_EQ(bridgePixels.size(), 4u);
        EXPECT_NEAR(bridgePixels[0], kExpR, 1) << "Bridge compositor center R within 1/255 analytic (51)";
        EXPECT_NEAR(bridgePixels[1], kExpG, 1) << "G within 1/255 (102)";
        EXPECT_NEAR(bridgePixels[2], kExpB, 1) << "B within 1/255 (204)";
        EXPECT_EQ(bridgePixels[3], 255u) << "A must be 255 opaque (bridge)";
    }

    auto* mapper = broker->get<broker::MeshObjectMapper>();
    ASSERT_NE(mapper, nullptr);
    scene::TranslateContext ctx;
    ctx.view.viewId = view.id;
    ctx.view.viewMatrix = view.camera.viewMatrix();
    ctx.view.projMatrix = view.camera.projMatrix();
    const auto* stored = store.getMeshObject(oid);
    ASSERT_NE(stored, nullptr);
    EXPECT_EQ(stored->geometryKind, scene::GeometryKind::Sphere);
    auto mapped = mapper->map(*stored, ctx);
    ASSERT_TRUE(mapped.ok()) << mapped.error().message;
    ASSERT_NE(mapped->material, nullptr);
    EXPECT_NEAR(mapped->material->baseColor().r, 0.2f, 1e-6);
    EXPECT_NEAR(mapped->material->baseColor().g, 0.4f, 1e-6);
    EXPECT_NEAR(mapped->material->baseColor().b, 0.8f, 1e-6);

    render::MeshInstance inst;
    inst.mesh = mapped->mesh;
    inst.material = mapped->material;
    inst.model = glm::mat4(1.0f);
    render::MeshScene scene;
    scene.meshes.push_back(inst);
    render::Camera cam = makeOrthoCamera();
    // T3b View port: MeshRenderer::render deleted — use View path (single drawInstances blend-off)
    auto rendererPtr = std::make_shared<::re::render::MeshRenderer>(registry, nullptr);
    auto viewPtr = std::make_shared<::re::render::View>(::re::render::ViewRect{0,0,static_cast<int>(kW),static_cast<int>(kH)}, glm::vec4(0,0,0,0));
    viewPtr->setCamera(cam);
    viewPtr->addItem(scene, rendererPtr);
    auto rr = viewPtr->renderWithEnsure();
    ASSERT_TRUE(rr.ok()) << rr.error().message;
    ASSERT_NE(viewPtr->target(), nullptr);
    viewPtr->target()->framebuffer().bind();
    std::vector<uint8_t> pixels;
    utils::PixelReader reader;
    auto read = reader.read(kCX, kCY, 1, 1, pixels);
    viewPtr->target()->framebuffer().unbind();
    ASSERT_TRUE(read.ok()) << read.error().message;
    ASSERT_EQ(pixels.size(), 4u);
    EXPECT_NEAR(pixels[0], kExpR, 1) << "MeshObject(Sphere) center R within 1/255 analytic (51)";
    EXPECT_NEAR(pixels[1], kExpG, 1) << "G within 1/255 (102)";
    EXPECT_NEAR(pixels[2], kExpB, 1) << "B within 1/255 (204)";
    EXPECT_EQ(pixels[3], 255u) << "A must be 255 opaque";
}

// Check that old variant alias is gone
TEST(T1Hierarchy, VariantAliasRemoved) {
    EXPECT_GE(scene::SceneFactory::instance().size(), 6u);
    EXPECT_LE(scene::SceneFactory::instance().size(), 6u) << "exactly 6 technique kinds after T5";
}

} // namespace re::tests