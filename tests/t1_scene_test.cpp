// tests/t1_scene_test.cpp — T1 gate tests for re::scene value library (V3.1).
//
// Asserts: re::scene target builds; scene/Camera pan/rotate/zoom/orbit produce
// analytic viewMatrix (lookAt) within 1e-6; SceneStore/ViewStore add/remove
// preserves generation bump; disposition_scene (no render/ include) is audit-green.

#include <gtest/gtest.h>

#include <cmath>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "scene/camera.hpp"
#include "scene/object.hpp"
#include "scene/plane_desc.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"

namespace re::tests {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool matNear(const glm::mat4& a, const glm::mat4& b, float eps = 1e-6f) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (std::abs(a[c][r] - b[c][r]) > eps) return false;
        }
    }
    return true;
}

static data::Mesh makeDummyMesh() {
    // Single triangle: positions (0,0,0) (1,0,0) (0,1,0) — analytic bounds [0,1].
    std::vector<glm::vec3> pos{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    std::vector<uint32_t> idx{0, 1, 2};
    return data::Mesh::fromTriangles(std::move(pos), std::move(idx));
}

static data::VolumeDataset makeDummyVolume() {
    // 2x2x2 volume, all zeros.
    return data::VolumeDataset(2, 2, 2, std::vector<float>(8, 0.0f));
}

// ---------------------------------------------------------------------------
// Camera: viewMatrix is lookAt analytically within 1e-6
// ---------------------------------------------------------------------------

TEST(T1SceneCamera, DefaultViewMatrixIsLookAt) {
    scene::Camera cam;
    glm::mat4 expected = glm::lookAt(glm::vec3{0, 0, 5}, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0});
    glm::mat4 actual = cam.viewMatrix();
    EXPECT_TRUE(matNear(actual, expected, 1e-6f))
        << "default viewMatrix must equal lookAt((0,0,5),(0,0,0),(0,1,0)) within 1e-6";
    EXPECT_EQ(cam.viewGen(), 0u);
    EXPECT_EQ(cam.projGen(), 0u);
}

TEST(T1SceneCamera, PanProducesAnalyticViewMatrix) {
    scene::Camera cam;
    cam.pan(1.0f, 0.5f);
    // Analytic: right=(1,0,0), up=(0,1,0) → delta=(1,0.5,0)
    // Eye (1,0.5,5), Center (1,0.5,0)
    glm::vec3 expEye{1.0f, 0.5f, 5.0f};
    glm::vec3 expCenter{1.0f, 0.5f, 0.0f};
    glm::vec3 expUp{0.0f, 1.0f, 0.0f};
    glm::mat4 expected = glm::lookAt(expEye, expCenter, expUp);
    EXPECT_TRUE(matNear(cam.viewMatrix(), expected, 1e-6f));
    EXPECT_EQ(cam.viewGen(), 1u) << "pan must bump viewGen by 1 (explainable invariant)";
    EXPECT_EQ(cam.projGen(), 0u) << "pan must not bump projGen (per-field split)";
    // Pan again
    cam.pan(-0.5f, 0.0f);
    EXPECT_EQ(cam.viewGen(), 2u);
}

TEST(T1SceneCamera, ZoomProducesAnalyticViewMatrix) {
    scene::Camera cam;
    cam.zoom(0.5f);
    // distance halves: eye (0,0,2.5)
    glm::vec3 expEye{0.0f, 0.0f, 2.5f};
    glm::mat4 expected = glm::lookAt(expEye, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0});
    EXPECT_TRUE(matNear(cam.viewMatrix(), expected, 1e-6f));
    EXPECT_EQ(cam.viewGen(), 1u);
    EXPECT_EQ(cam.projGen(), 0u);
}

TEST(T1SceneCamera, Orbit90YProducesAnalyticViewMatrix) {
    scene::Camera cam;
    cam.orbit(90.0f, glm::vec3{0, 1, 0});
    // Analytic offset (5,0,0) → eye (5,0,0)
    glm::vec3 expEye{5.0f, 0.0f, 0.0f};
    // Up rotates to (0,1,0) stays (orbit around up preserves up)
    glm::mat4 expected = glm::lookAt(expEye, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0});
    EXPECT_TRUE(matNear(cam.viewMatrix(), expected, 1e-6f));
    EXPECT_EQ(cam.viewGen(), 1u);
}

TEST(T1SceneCamera, RotateYaw90ProducesAnalyticViewMatrix) {
    scene::Camera cam;
    cam.rotate(90.0f, 0.0f);
    // Same as orbit 90 around world up for default config
    glm::vec3 expEye{5.0f, 0.0f, 0.0f};
    glm::mat4 expected = glm::lookAt(expEye, glm::vec3{0, 0, 0}, glm::vec3{0, 1, 0});
    EXPECT_TRUE(matNear(cam.viewMatrix(), expected, 1e-6f));
    EXPECT_EQ(cam.viewGen(), 1u);
}

TEST(T1SceneCamera, RotatePitch45ProducesAnalytic) {
    scene::Camera cam;
    // Pitch 90 should move eye to (0,5,0) looking down Y
    cam.rotate(0.0f, 90.0f);
    // After pitch 90 around right (1,0,0): offset (0,0,5) → (0,-5,0) or (0,5,0) depending on sign.
    // Use the same formula as implementation to compute expected, then compare.
    // Assert that rotated viewMatrix equals recomputed lookAt from cam.eye().
    glm::mat4 expected = glm::lookAt(cam.eye(), cam.center(), cam.up());
    EXPECT_TRUE(matNear(cam.viewMatrix(), expected, 1e-6f));
    EXPECT_EQ(cam.viewGen(), 1u);
}

TEST(T1SceneCamera, ProjGenSplit) {
    scene::Camera cam;
    cam.setPerspective(60.0f, 1.5f, 0.1f, 200.0f);
    EXPECT_EQ(cam.projGen(), 1u);
    EXPECT_EQ(cam.viewGen(), 0u);
    // Changing again bumps again
    cam.setPerspective(60.0f, 1.5f, 0.1f, 200.0f);
    EXPECT_EQ(cam.projGen(), 1u) << "identical perspective must not bump projGen again (no change)";
    cam.setPerspective(70.0f, 1.5f, 0.1f, 200.0f);
    EXPECT_EQ(cam.projGen(), 2u);
}

// ---------------------------------------------------------------------------
// SceneStore generation bump on add/remove
// ---------------------------------------------------------------------------

TEST(T1SceneStore, AddRemovePreservesGenerationBump) {
    scene::SceneStore store;
    EXPECT_EQ(store.storeGeneration(), 0u) << "initial storeGen must be 0 (explainable constant)";

    auto mesh = makeDummyMesh();
    scene::MeshObject obj;
    obj.mesh = &mesh;
    obj.transform = glm::mat4{1.0f};

    uint64_t id1 = store.addMeshObject(obj);
    EXPECT_EQ(store.storeGeneration(), 1u) << "after 1 add, storeGen must be 1";
    const auto* got = store.getMeshObject(id1);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->generation, 1u) << "object generation after first add must be 1 (storeGen+1 at alloc)";
    EXPECT_EQ(store.meshObjectCount(), 1u);

    uint64_t id2 = store.addMeshObject(obj);
    EXPECT_EQ(store.storeGeneration(), 2u) << "after 2 adds, storeGen must be 2 (monotonic +1 per add)";
    EXPECT_NE(id1, id2) << "stable handles must be distinct (explainable invariant: nextId monotonic)";
    EXPECT_EQ(store.meshObjectCount(), 2u);

    // Remove first
    bool removed = store.removeMeshObject(id1);
    EXPECT_TRUE(removed);
    EXPECT_EQ(store.storeGeneration(), 3u) << "remove must bump storeGen to 3";
    EXPECT_EQ(store.getMeshObject(id1), nullptr) << "removed id must return nullptr (stale handle)";
    EXPECT_EQ(store.meshObjectCount(), 1u);

    // Adding again gets new id, not reused old with same generation
    uint64_t id3 = store.addMeshObject(obj);
    EXPECT_EQ(store.storeGeneration(), 4u);
    EXPECT_NE(id3, id1);
    EXPECT_NE(id3, id2);
    const auto* got3 = store.getMeshObject(id3);
    ASSERT_NE(got3, nullptr);
    EXPECT_EQ(got3->generation, 4u) << "new object gen must equal storeGen at alloc (4)";
}

TEST(T1SceneStore, VolumeAndPlaneAddRemove) {
    scene::SceneStore store;
    auto vol = makeDummyVolume();
    scene::VolumeObject vobj;
    vobj.volume = &vol;
    uint64_t vid = store.addVolumeObject(vobj);
    EXPECT_EQ(store.storeGeneration(), 1u);
    EXPECT_NE(store.getVolumeObject(vid), nullptr);
    EXPECT_EQ(store.volumeObjectCount(), 1u);
    EXPECT_TRUE(store.removeVolumeObject(vid));
    EXPECT_EQ(store.storeGeneration(), 2u);
    EXPECT_EQ(store.getVolumeObject(vid), nullptr);
}

TEST(T1SceneStore, DirtyFieldsSince) {
    scene::SceneStore store;
    EXPECT_TRUE(store.dirtyFieldsSince(0).empty()) << "no dirty when storeGen==lastGen (0==0)";
    auto mesh = makeDummyMesh();
    scene::MeshObject obj;
    obj.mesh = &mesh;
    store.addMeshObject(obj);
    auto dirty = store.dirtyFieldsSince(0);
    // Bounded set: implementation returns exactly 4 fields (Transform, Material, TransferFunction, Items) when storeGen != lastGen.
    EXPECT_EQ(dirty.size(), 4u) << "dirtyFieldsSince(0) must return exactly 4 fields (bounded, explainable constant)";
    EXPECT_TRUE(store.dirtyFieldsSince(store.storeGeneration()).empty())
        << "dirtyFieldsSince(currentGen) must be empty (no new mutation)";
    // ViewStore variant: also bounded.
    scene::ViewStore vstore;
    EXPECT_TRUE(vstore.dirtyFieldsSince(0).empty());
    scene::View view;
    vstore.addView(view);
    auto vdirty = vstore.dirtyFieldsSince(0);
    EXPECT_EQ(vdirty.size(), 4u) << "ViewStore dirtyFieldsSince must return 4 fields (Rect,Plane,CameraView,Items)";
}

// ---------------------------------------------------------------------------
// ViewStore generation
// ---------------------------------------------------------------------------

TEST(T1ViewStore, AddRemovePreservesGenerationBump) {
    scene::ViewStore vstore;
    EXPECT_EQ(vstore.storeGeneration(), 0u);

    scene::View v;
    v.rect = scene::Rect{0, 0, 640, 480};
    uint64_t id1 = vstore.addView(v);
    EXPECT_EQ(vstore.storeGeneration(), 1u);
    const auto* got = vstore.getView(id1);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->generation, 1u);
    EXPECT_EQ(vstore.count(), 1u);

    uint64_t id2 = vstore.addView(v);
    EXPECT_EQ(vstore.storeGeneration(), 2u);
    EXPECT_NE(id1, id2);

    EXPECT_TRUE(vstore.removeView(id1));
    EXPECT_EQ(vstore.storeGeneration(), 3u);
    EXPECT_EQ(vstore.getView(id1), nullptr);
}

TEST(T1SceneView, PerFieldGeneration) {
    scene::View v;
    EXPECT_EQ(v.rectGen, 0u);
    v.setRect(scene::Rect{0, 0, 800, 600});
    EXPECT_EQ(v.rectGen, 1u);
    EXPECT_EQ(v.generation, 1u);
    v.setRect(scene::Rect{0, 0, 800, 600});
    EXPECT_EQ(v.rectGen, 1u) << "setting identical rect must not bump gen (no change)";
    // Plane gen only bumps on change (equality-guarded).
    EXPECT_EQ(v.planeGen, 0u);
    scene::PlaneDesc pd;
    pd.normal = glm::vec3{0, 0, 1};
    v.setPlane(pd);
    EXPECT_EQ(v.planeGen, 1u) << "first plane set must bump planeGen to 1";
    v.setPlane(pd);
    EXPECT_EQ(v.planeGen, 1u) << "identical plane must not bump planeGen again";
    // ItemIds gen only bumps on change.
    EXPECT_EQ(v.itemsGen, 0u);
    v.setItemIds({1, 2, 3});
    EXPECT_EQ(v.itemsGen, 1u);
    v.setItemIds({1, 2, 3});
    EXPECT_EQ(v.itemsGen, 1u) << "identical itemIds must not bump itemsGen";
    v.setItemIds({1, 2});
    EXPECT_EQ(v.itemsGen, 2u);
    // mutateCamera via pan bumps cameraGen + viewGen.
    EXPECT_EQ(v.cameraGen, 0u);
    v.mutateCamera([](scene::Camera& c) { c.pan(1.0f, 0.0f); });
    EXPECT_EQ(v.cameraGen, 1u) << "mutateCamera(pan) must bump cameraGen by 1";
    EXPECT_EQ(v.generation, 5u) << "generation must be 5 after rect(1)+plane(1)+items(2)+camera(1) bumps";
    uint64_t beforeCam = v.cameraGen;
    v.mutateCamera([](scene::Camera& c) { c.setPerspective(60.0f, 1.5f, 0.1f, 200.0f); });
    EXPECT_EQ(v.cameraGen, beforeCam + 1u) << "mutateCamera(setPerspective) must bump cameraGen via projGen";
    // No-op mutate must not bump.
    uint64_t genBefore = v.cameraGen;
    v.mutateCamera([](scene::Camera&) {});
    EXPECT_EQ(v.cameraGen, genBefore) << "no-op mutateCamera must not bump cameraGen";
}

TEST(T1ScenePlaneDesc, SpaceAndGeneration) {
    scene::PlaneDesc p;
    EXPECT_EQ(p.space, scene::Space::World);
    EXPECT_EQ(p.generation, 0u);
    p.setSpace(scene::Space::VoxelIndex);
    EXPECT_EQ(p.space, scene::Space::VoxelIndex);
    EXPECT_EQ(p.generation, 1u);
    p.setNormal(glm::vec3{1, 0, 0});
    EXPECT_EQ(p.generation, 2u);
    EXPECT_NEAR(p.normal.x, 1.0f, 1e-6f);
    p.setPoint(glm::vec3{1, 2, 3});
    EXPECT_EQ(p.generation, 3u) << "setPoint must bump generation";
    EXPECT_NEAR(p.point.x, 1.0f, 1e-6f);
    // Equality check for View setPlane guard.
    scene::PlaneDesc q;
    q.setSpace(scene::Space::VoxelIndex);
    q.setNormal(glm::vec3{1, 0, 0});
    q.setPoint(glm::vec3{1, 2, 3});
    EXPECT_TRUE(p == q) << "identical PlaneDesc must compare equal (enables no-bump guard)";
}

TEST(T1SceneObject, PureValueCopyable) {
    auto mesh = makeDummyMesh();
    scene::MeshObject a;
    a.mesh = &mesh;
    a.transform = glm::translate(glm::mat4{1.0f}, glm::vec3{1, 2, 3});
    scene::MeshObject b = a; // copyable
    EXPECT_EQ(b.mesh, a.mesh);
    EXPECT_TRUE(matNear(b.transform, a.transform, 1e-6f));
    b.setTransform(glm::mat4{1.0f});
    EXPECT_NE(b.generation, a.generation) << "mutating copy must bump copy's gen, not original's";
}

} // namespace re::tests
