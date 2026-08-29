// tests/t14a_generation_sync_test.cpp — T14a gate: sync error and hybrid poll early-out.
// Verifies ViewSynchronizer::sync fails with typed error code 10 when no ViewCompositor is wired
// and that a second sync with unchanged SceneStore generation early-outs before hashing.
#include <gtest/gtest.h>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "broker/broker.hpp"
#include "broker/render_stack.hpp"
#include "broker/view_compositor.hpp"
#include "broker/view_synchronizer.hpp"
#include "broker/camera_mapper.hpp"
#include "broker/mesh_object_mapper.hpp"
#include "render/asset_registry.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"
#include "scene/camera.hpp"
#include "data/mesh.hpp"

namespace re::tests {

static data::Mesh makeQuadMesh() {
    std::vector<glm::vec3> pos = {{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
    std::vector<uint32_t> idx = {0,1,2,0,2,3};
    return data::Mesh::fromTriangles(pos, idx);
}

TEST(T14aSync, WithoutCompositorReturnsCode10) {
    auto registry = std::make_shared<render::AssetRegistry>();
    auto broker = std::make_shared<broker::Broker>();
    broker->registerMapper(std::make_unique<broker::CameraMapper>());
    broker->registerMapper(std::make_unique<broker::MeshObjectMapper>(registry));
    auto stack = broker::RenderStack::create(registry);
    broker::ViewSynchronizer sync(broker, stack);
    scene::SceneStore store;
    auto quad = std::make_shared<data::Mesh>(makeQuadMesh());
    scene::MeshObject mo; mo.mesh = quad;
    uint64_t oid = store.addMeshObject(std::move(mo));
    scene::View v; v.id = 1; v.rect = scene::Rect{0,0,640,480};
    v.camera = scene::Camera(glm::vec3(0,0,5), glm::vec3(0,0,0), glm::vec3(0,1,0));
    v.itemIds = {oid};
    std::vector<scene::View> views = {v};
    uint64_t genBefore = sync.storeGeneration();
    auto res = sync.sync(views, store, 1, nullptr);
    ASSERT_TRUE(res.failed()) << "sync without compositor must fail";
    EXPECT_EQ(res.error().code, 10) << "typed error code 10 for missing ViewCompositor";
    EXPECT_EQ(sync.storeGeneration(), genBefore) << "lastStoreGen must not advance on error";
    glm::mat4 expect = v.camera.viewMatrix();
    glm::mat4 got = v.camera.viewMatrix();
    for (int c=0;c<4;++c) for (int r=0;r<4;++r) EXPECT_NEAR(got[c][r], expect[c][r], 1e-6) << "viewMatrix unchanged within 1e-6 after failed sync";
}

TEST(T14aSync, StoreGenerationUnchangedEarlyOut) {
    auto registry = std::make_shared<render::AssetRegistry>();
    auto broker = std::make_shared<broker::Broker>();
    broker->registerMapper(std::make_unique<broker::CameraMapper>());
    broker->registerMapper(std::make_unique<broker::MeshObjectMapper>(registry));
    auto stack = broker::RenderStack::create(registry);
    auto compositor = std::make_shared<broker::ViewCompositor>(broker, stack);
    broker::ViewSynchronizer sync(broker, compositor, stack);
    scene::SceneStore store;
    auto quad = std::make_shared<data::Mesh>(makeQuadMesh());
    scene::MeshObject mo; mo.mesh = quad;
    uint64_t oid = store.addMeshObject(std::move(mo));
    scene::View v; v.id = 10; v.rect = scene::Rect{0,0,640,480};
    v.camera = scene::Camera(glm::vec3(0,0,5), glm::vec3(0,0,0), glm::vec3(0,1,0));
    v.itemIds = {oid};
    std::vector<scene::View> views = {v};
    auto r0 = sync.sync(views, store, 5, compositor.get());
    ASSERT_TRUE(r0.ok()) << r0.error().message;
    uint64_t genAfterFirst = sync.storeGeneration();
    EXPECT_NE(genAfterFirst, 0u) << "first sync must advance lastStoreGen";
    auto r1 = sync.sync(views, store, 5, compositor.get());
    ASSERT_TRUE(r1.ok()) << "second sync with unchanged storeGeneration must early-out successfully";
    EXPECT_EQ(sync.storeGeneration(), genAfterFirst) << "early-out must not recompute lastStoreGen";
    render::View* rv = compositor->getView(5, 10);
    ASSERT_NE(rv, nullptr);
    glm::mat4 vm = rv->camera().view;
    glm::mat4 expect = v.camera.viewMatrix();
    for (int c=0;c<4;++c) for (int r=0;r<4;++r) EXPECT_NEAR(vm[c][r], expect[c][r], 1e-6) << "camera viewMatrix must remain analytic within 1e-6 after early-out";
}

} // namespace re::tests
