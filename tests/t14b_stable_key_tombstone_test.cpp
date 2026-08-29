// tests/t14b_stable_key_tombstone_test.cpp — T14b gate: StableKey alias, tombstone bound, viewGen/projGen.
// Covers: two layouts same viewId different layoutId -> distinct ReCamera (no alias), tombstone bounded O(FieldIds) after 10k cycles, viewGen/projGen analytic.
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <memory>
#include <vector>
#include "broker/broker.hpp"
#include "broker/camera_mapper.hpp"
#include "broker/stable_key.hpp"
#include "broker/view_compositor.hpp"
#include "broker/view_synchronizer.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"
#include "scene/camera.hpp"

namespace re::tests {

TEST(T14bStableKey, TwoLayoutsSameViewIdDistinctReCamera) {
    broker::CameraMapper mapper;
    scene::Camera camA(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    scene::Camera camB(glm::vec3(0, 0, 10), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    scene::TranslateContext ctxA;
    ctxA.view.layoutId = 1;
    ctxA.view.viewId = 42;
    scene::TranslateContext ctxB;
    ctxB.view.layoutId = 2;
    ctxB.view.viewId = 42;
    auto rA = mapper.mapCached(camA, ctxA);
    ASSERT_TRUE(rA.ok()) << rA.error().message;
    auto rB = mapper.mapCached(camB, ctxB);
    ASSERT_TRUE(rB.ok()) << rB.error().message;
    glm::mat4 expectA = camA.viewMatrix();
    glm::mat4 expectB = camB.viewMatrix();
    glm::mat4 gotA = rA->view;
    glm::mat4 gotB = rB->view;
    constexpr double kTol = 1e-6;
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) {
        EXPECT_NEAR(gotA[c][r], expectA[c][r], kTol) << "layout 1 camera viewMatrix analytic";
        EXPECT_NEAR(gotB[c][r], expectB[c][r], kTol) << "layout 2 camera viewMatrix analytic distinct from layout 1";
    }
    EXPECT_FALSE(gotA == gotB) << "different layouts same viewId must not alias ReCamera";
    EXPECT_EQ(mapper.cacheEntries(), 2u) << "two StableKeys hold two memos not one";
    auto rA2 = mapper.mapCached(camA, ctxA);
    ASSERT_TRUE(rA2.ok());
    EXPECT_EQ(mapper.cacheHits(), 1u) << "re-hit layout 1 still distinct";
}

TEST(T14bStableKey, TombstoneBoundedAndViewGenProjGen) {
    scene::ViewStore vstore;
    for (int i = 0; i < 10000; ++i) {
        scene::View v;
        v.rect = scene::Rect{0,0,640,480};
        v.camera = scene::Camera(glm::vec3(0,0,5), glm::vec3(0,0,0), glm::vec3(0,1,0));
        uint64_t id = vstore.addView(v);
        ASSERT_TRUE(vstore.removeView(id));
    }
    EXPECT_LE(vstore.tombstoneCount(), 16u) << "tombstoneGen size bounded O(FieldIds) after 10k cycles";
    vstore.pruneOlderThan(vstore.storeGeneration());
    EXPECT_LE(vstore.tombstoneCount(), 16u) << "pruneOlderThan keeps bounded";
    scene::View v;
    v.rect = scene::Rect{0,0,640,480};
    v.camera = scene::Camera(glm::vec3(1,2,3), glm::vec3(0,0,0), glm::vec3(0,1,0));
    uint64_t vid = vstore.addView(v);
    const scene::View* vp = vstore.getView(vid);
    ASSERT_NE(vp, nullptr);
    EXPECT_EQ(vp->lightsGen, vp->generation) << "addView seeds lightsGen from generation";
    EXPECT_EQ(vp->rectGen, vp->generation);
    scene::Camera before = vp->camera;
    uint64_t vg0 = before.viewGen();
    uint64_t pg0 = before.projGen();
    scene::Camera moved = before;
    moved.pan(1.0f, 0.0f);
    EXPECT_EQ(moved.viewGen(), vg0 + 1) << "pan bumps viewGen by one";
    EXPECT_EQ(moved.projGen(), pg0) << "pan leaves projGen unchanged";
    moved.setPerspective(60.0f, 1.5f, 0.1f, 100.0f);
    EXPECT_GT(moved.projGen(), pg0) << "perspective change bumps projGen";
    glm::mat4 vm = moved.viewMatrix();
    glm::mat4 pm = moved.projMatrix();
    // Analytic oracle: viewGen bumps on pan (integer +1) already asserted above; verify viewMatrix actually changed and matches recomputed lookAt (not tautology).
    glm::mat4 beforeVM = before.viewMatrix();
    bool viewChanged = false;
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) if (vm[c][r] != beforeVM[c][r]) viewChanged = true;
    EXPECT_TRUE(viewChanged) << "pan must change viewMatrix analytic";
    glm::mat4 expectLook = glm::lookAt(moved.eye(), moved.center(), moved.up());
    for (int c=0;c<4;++c) for (int r=0;r<4;++r) {
        EXPECT_NEAR(vm[c][r], expectLook[c][r], 1e-6) << "viewMatrix matches lookAt analytic within 1e-6 after pan";
    }
    (void)pm;
    scene::SceneStore sstore;
    for (int i=0;i<5000;++i) {
        scene::MeshObject mo;
        mo.mesh = std::make_shared<data::Mesh>(data::Mesh::fromTriangles({{0,0,0},{1,0,0},{0,1,0}}, {0,1,2}));
        uint64_t oid = sstore.addMeshObject(std::move(mo));
        sstore.removeObject(oid);
    }
    EXPECT_LE(sstore.tombstoneCount(), 16u) << "SceneStore tombstone bounded";
    std::string js = sstore.serialize();
    EXPECT_LE(sstore.tombstoneCount(), 16u) << "serialize prune keeps bounded";
    (void)js;
}

} // namespace re::tests
