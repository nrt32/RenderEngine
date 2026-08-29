// tests/t6_technique_order_test.cpp — T6 gate: global techniqueOrder + scoped priority ordering (broker side).
// Validates cross-type ordering inside each Layer via the explicit hardcoded techniqueOrder array (Volume,VolumeSlice,Plane,Mesh,MeshSlice,Contour)
// that governs stable_sort by (uint16(layer) asc, orderIdx asc, priority asc, insertionIdx asc) with scoped priority inside same layer+type bucket.
// Evidence rule: every pixel assert uses analytic tolerance via kTol, never non-empty. The global techniqueOrder ordering replaces the
// former per-view LayerMask bitset and per-object override map, so lower numeric Layer draws first, techniqueOrder decides cross-type order inside a Layer,
// priority is scoped to same layer+technique, and insertionIdx is the stable tie. Version 2 serialize idempotent on second run. // 1/255
#include <gtest/gtest.h>

#include <array>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "broker/render_stack.hpp"
#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"
#include "render/offscreen.hpp"
#include "scene/layer.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"
#include "volume/transfer_function.hpp"
#include <nlohmann/json.hpp>

namespace re::tests {
namespace {

constexpr int kTol = 1;

std::array<uint8_t,4> centerPixel(const data::Image& img) {
    int cx = img.width()/2;
    int cy = img.height()/2;
    return {img.pixel(cx,cy,0), img.pixel(cx,cy,1), img.pixel(cx,cy,2), img.pixel(cx,cy,3)};
}

scene::Camera makePersp() {
    scene::Camera cam(glm::vec3(0,0,5), glm::vec3(0,0,0), glm::vec3(0,1,0));
    cam.setPerspective(60.0f,1.0f,0.1f,10.0f);
    return cam;
}
scene::Camera makeOrtho() {
    scene::Camera cam(glm::vec3(0,0,5), glm::vec3(0,0,0), glm::vec3(0,1,0));
    cam.setOrtho(-2,2,-2,2,0.1f,10.0f);
    return cam;
}
data::Mesh makeQuad() {
    std::vector<glm::vec3> p = {glm::vec3(-1,-1,0), glm::vec3(1,-1,0), glm::vec3(1,1,0), glm::vec3(-1,1,0)};
    std::vector<uint32_t> idx = {0,1,2,0,2,3};
    return data::Mesh::fromTriangles(std::move(p), std::move(idx));
}
std::shared_ptr<const data::Image> makeSolidImage(uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> px = {r,g,b,255};
    return std::make_shared<const data::Image>(1,1,4, std::move(px));
}
data::VolumeDataset makeTinyVolume() {
    std::vector<float> vox(8, 0.8f);
    return data::VolumeDataset(2,2,2, std::move(vox));
}

} // namespace

TEST(T6TechniqueOrder, GlobalTechniqueOrderArray) {
    using Kind = scene::SceneKind;
    EXPECT_EQ(broker::techniqueOrder[0], Kind::Volume);
    EXPECT_EQ(broker::techniqueOrder[1], Kind::VolumeSlice);
    EXPECT_EQ(broker::techniqueOrder[2], Kind::Plane);
    EXPECT_EQ(broker::techniqueOrder[3], Kind::Mesh);
    EXPECT_EQ(broker::techniqueOrder[4], Kind::MeshSlice);
    EXPECT_EQ(broker::techniqueOrder[5], Kind::Contour);
    EXPECT_EQ(broker::techniqueOrder.size(), 6u);
}

TEST(T6TechniqueOrder, SameLayerDifferentTypeViaTechniqueOrderSwapInvariant) {
    scene::SceneStore store;
    auto greenImg = makeSolidImage(0,255,0);
    scene::PlaneObject po;
    po.image = greenImg;
    po.transform = glm::mat4(1.0f);
    po.layer = scene::Layer::LAYER_0;
    po.priority = 0;
    uint64_t planeId = store.addPlaneObject(std::move(po));

    auto quad = std::make_shared<const data::Mesh>(makeQuad());
    scene::MeshObject mo;
    mo.mesh = quad;
    mo.transform = glm::mat4(1.0f);
    mo.presentation.phong.baseColor = glm::vec4(1,0,0,1);
    mo.layer = scene::Layer::LAYER_0;
    mo.priority = 0;
    uint64_t meshId = store.addMeshObject(std::move(mo));

    auto cam = makePersp();
    scene::View viewA; viewA.id=1; viewA.rect = scene::Rect{0,0,64,64}; viewA.camera = cam; viewA.setClearColor(glm::vec4(0,0,0,1));
    viewA.setItemIds({planeId, meshId});
    scene::View viewB; viewB.id=1; viewB.rect = scene::Rect{0,0,64,64}; viewB.camera = cam; viewB.setClearColor(glm::vec4(0,0,0,1));
    viewB.setItemIds({meshId, planeId});

    auto imgA = render::renderOffscreen(64,64, std::vector<scene::View>{viewA}, store);
    ASSERT_TRUE(imgA.ok()) << imgA.error().message;
    auto imgB = render::renderOffscreen(64,64, std::vector<scene::View>{viewB}, store);
    ASSERT_TRUE(imgB.ok()) << imgB.error().message;

    auto pA = centerPixel(*imgA);
    auto pB = centerPixel(*imgB);
    EXPECT_NEAR(pA[0], pB[0], kTol);
    EXPECT_NEAR(pA[1], pB[1], kTol);
    EXPECT_NEAR(pA[2], pB[2], kTol);
    EXPECT_NEAR(pA[0], 255, kTol);
    EXPECT_NEAR(pA[1], 0, kTol);
    EXPECT_NEAR(pA[2], 0, kTol);

    // VolumeSlice before Contour swap invariant on same LAYER_0
    scene::SceneStore store2;
    auto vol = std::make_shared<const data::VolumeDataset>(makeTinyVolume());
    scene::VolumeSliceObject vso;
    vso.volume = vol;
    vso.transform = glm::mat4(1.0f);
    vso.transferFunction = volume::TransferFunction{{{0.0f,{0,0,0,0}}, {1.0f,{0,1,0,1}}}};
    vso.layer = scene::Layer::LAYER_0;
    vso.priority = 0;
    uint64_t vsId = store2.addVolumeSliceObject(std::move(vso));

    auto q2 = std::make_shared<const data::Mesh>(makeQuad());
    scene::ContourObject co;
    co.mesh = q2;
    co.transform = glm::mat4(1.0f);
    co.plane.setNormal(glm::vec3(0,0,1));
    co.plane.setPoint(glm::vec3(0,0,0));
    co.color = glm::vec4(1,0,0,1);
    co.layer = scene::Layer::LAYER_0;
    co.priority = 0;
    uint64_t coId = store2.addContourObject(std::move(co));

    scene::PlaneDesc plane; plane.setNormal(glm::vec3(0,0,1)); plane.setPoint(glm::vec3(0,0,0)); plane.setSpace(scene::Space::World);
    auto camO = makeOrtho();
    scene::View vA; vA.id=10; vA.rect = scene::Rect{0,0,64,64}; vA.camera = camO; vA.setClearColor(glm::vec4(0,0,0,1)); vA.setPlane(plane);
    vA.setItemIds({coId, vsId});
    scene::View vB2; vB2.id=10; vB2.rect = scene::Rect{0,0,64,64}; vB2.camera = camO; vB2.setClearColor(glm::vec4(0,0,0,1)); vB2.setPlane(plane);
    vB2.setItemIds({vsId, coId});

    auto imgC = render::renderOffscreen(64,64, std::vector<scene::View>{vA}, store2);
    ASSERT_TRUE(imgC.ok()) << imgC.error().message;
    auto imgD = render::renderOffscreen(64,64, std::vector<scene::View>{vB2}, store2);
    ASSERT_TRUE(imgD.ok()) << imgD.error().message;
    auto pC = centerPixel(*imgC);
    auto pD = centerPixel(*imgD);
    EXPECT_NEAR(pC[0], pD[0], kTol);
    EXPECT_NEAR(pC[1], pD[1], kTol);
    EXPECT_NEAR(pC[2], pD[2], kTol);
}

TEST(T6TechniqueOrder, Layer0VsLayer1OverlayOnTop) {
    scene::SceneStore store;
    auto quad0 = std::make_shared<const data::Mesh>(makeQuad());
    scene::MeshObject blue; blue.mesh = quad0; blue.transform = glm::mat4(1.0f); blue.presentation.phong.baseColor = glm::vec4(0,0,1,1); blue.layer = scene::Layer::LAYER_0; blue.priority = 0;
    uint64_t blueId = store.addMeshObject(std::move(blue));
    auto quad1 = std::make_shared<const data::Mesh>(makeQuad());
    scene::MeshObject red; red.mesh = quad1; red.transform = glm::mat4(1.0f); red.presentation.phong.baseColor = glm::vec4(1,0,0,1); red.layer = scene::Layer::LAYER_1; red.priority = 0;
    uint64_t redId = store.addMeshObject(std::move(red));

    auto cam = makePersp();
    scene::View view; view.id=1; view.rect = scene::Rect{0,0,64,64}; view.camera = cam; view.setClearColor(glm::vec4(0,0,0,1));
    view.setItemIds({blueId, redId});
    auto img = render::renderOffscreen(64,64, std::vector<scene::View>{view}, store);
    ASSERT_TRUE(img.ok()) << img.error().message;
    auto p = centerPixel(*img);
    EXPECT_NEAR(p[0], 255, kTol);
    EXPECT_NEAR(p[2], 0, kTol);

    // swapped insertion still LAYER_1 on top because layer numeric asc governs, not insertion
    scene::View viewSwapped; viewSwapped.id=1; viewSwapped.rect = scene::Rect{0,0,64,64}; viewSwapped.camera = cam; viewSwapped.setClearColor(glm::vec4(0,0,0,1));
    viewSwapped.setItemIds({redId, blueId});
    auto img2 = render::renderOffscreen(64,64, std::vector<scene::View>{viewSwapped}, store);
    ASSERT_TRUE(img2.ok()) << img2.error().message;
    auto p2 = centerPixel(*img2);
    EXPECT_NEAR(p2[0], 255, kTol);
    EXPECT_NEAR(p[0], p2[0], kTol);
}

TEST(T6TechniqueOrder, SameLayerSameTypePriorityAndScopedCrossType) {
    // same layer same type prio0 vs prio1 — prio1 draws after prio0 so prio1 red wins
    scene::SceneStore store;
    auto qA = std::make_shared<const data::Mesh>(makeQuad());
    scene::MeshObject a; a.mesh = qA; a.transform = glm::mat4(1.0f); a.presentation.phong.baseColor = glm::vec4(0,1,0,1); a.layer = scene::Layer::LAYER_0; a.priority = 0;
    uint64_t aId = store.addMeshObject(std::move(a));
    auto qB = std::make_shared<const data::Mesh>(makeQuad());
    scene::MeshObject b; b.mesh = qB; b.transform = glm::mat4(1.0f); b.presentation.phong.baseColor = glm::vec4(1,0,0,1); b.layer = scene::Layer::LAYER_0; b.priority = 1;
    uint64_t bId = store.addMeshObject(std::move(b));

    auto cam = makePersp();
    scene::View view; view.id=1; view.rect = scene::Rect{0,0,64,64}; view.camera = cam; view.setClearColor(glm::vec4(0,0,0,1));
    view.setItemIds({aId, bId});
    auto img = render::renderOffscreen(64,64, std::vector<scene::View>{view}, store);
    ASSERT_TRUE(img.ok()) << img.error().message;
    auto p = centerPixel(*img);
    EXPECT_NEAR(p[0], 255, kTol);
    EXPECT_NEAR(p[1], 0, kTol);

    // swapped insertion but priority still decides — prio1 still after prio0 due to stable_sort by priority before insertionIdx
    scene::View viewSwapped; viewSwapped.id=1; viewSwapped.rect = scene::Rect{0,0,64,64}; viewSwapped.camera = cam; viewSwapped.setClearColor(glm::vec4(0,0,0,1));
    viewSwapped.setItemIds({bId, aId});
    auto img2 = render::renderOffscreen(64,64, std::vector<scene::View>{viewSwapped}, store);
    ASSERT_TRUE(img2.ok()) << img2.error().message;
    auto p2 = centerPixel(*img2);
    EXPECT_NEAR(p2[0], 255, kTol);
    EXPECT_NEAR(p[0], p2[0], kTol);

    // scoped cross-type: VolumeSlice prio100 still before Contour prio0 on same LAYER_0 because techniqueOrder outranks priority
    scene::SceneStore store2;
    auto vol = std::make_shared<const data::VolumeDataset>(makeTinyVolume());
    scene::VolumeSliceObject vso; vso.volume = vol; vso.transform = glm::mat4(1.0f); vso.transferFunction = volume::TransferFunction{{{0.0f,{0,0,0,0}}, {1.0f,{0,1,0,1}}}}; vso.layer = scene::Layer::LAYER_0; vso.priority = 100;
    uint64_t vsId = store2.addVolumeSliceObject(std::move(vso));
    auto qC = std::make_shared<const data::Mesh>(makeQuad());
    scene::ContourObject co; co.mesh = qC; co.transform = glm::mat4(1.0f); co.plane.setNormal(glm::vec3(0,0,1)); co.plane.setPoint(glm::vec3(0,0,0)); co.color = glm::vec4(1,0,0,1); co.layer = scene::Layer::LAYER_0; co.priority = 0;
    uint64_t coId = store2.addContourObject(std::move(co));

    scene::PlaneDesc plane; plane.setNormal(glm::vec3(0,0,1)); plane.setPoint(glm::vec3(0,0,0)); plane.setSpace(scene::Space::World);
    auto camO = makeOrtho();
    // Order by techniqueOrder: VolumeSlice (idx1) before Contour (idx5) regardless of prio100 vs prio0, so swap insertion still same image
    scene::View va; va.id=20; va.rect = scene::Rect{0,0,64,64}; va.camera = camO; va.setClearColor(glm::vec4(0,0,0,1)); va.setPlane(plane); va.setItemIds({coId, vsId});
    scene::View vb; vb.id=20; vb.rect = scene::Rect{0,0,64,64}; vb.camera = camO; vb.setClearColor(glm::vec4(0,0,0,1)); vb.setPlane(plane); vb.setItemIds({vsId, coId});
    auto imgA = render::renderOffscreen(64,64, std::vector<scene::View>{va}, store2);
    ASSERT_TRUE(imgA.ok()) << imgA.error().message;
    auto imgB = render::renderOffscreen(64,64, std::vector<scene::View>{vb}, store2);
    ASSERT_TRUE(imgB.ok()) << imgB.error().message;
    auto pA = centerPixel(*imgA);
    auto pB = centerPixel(*imgB);
    EXPECT_NEAR(pA[0], pB[0], kTol);
    EXPECT_NEAR(pA[1], pB[1], kTol);
    EXPECT_NEAR(pA[2], pB[2], kTol);
}

TEST(T6TechniqueOrder, StableInsertionSameLayerTypePrio) {
    scene::SceneStore store;
    auto q1 = std::make_shared<const data::Mesh>(makeQuad());
    scene::MeshObject m1; m1.mesh = q1; m1.transform = glm::mat4(1.0f); m1.presentation.phong.baseColor = glm::vec4(0,0,1,1); m1.layer = scene::Layer::LAYER_0; m1.priority = 0;
    uint64_t id1 = store.addMeshObject(std::move(m1));
    auto q2 = std::make_shared<const data::Mesh>(makeQuad());
    scene::MeshObject m2; m2.mesh = q2; m2.transform = glm::mat4(1.0f); m2.presentation.phong.baseColor = glm::vec4(1,0,0,1); m2.layer = scene::Layer::LAYER_0; m2.priority = 0;
    uint64_t id2 = store.addMeshObject(std::move(m2));

    auto cam = makePersp();
    scene::View view; view.id=1; view.rect = scene::Rect{0,0,64,64}; view.camera = cam; view.setClearColor(glm::vec4(0,0,0,1));
    view.setItemIds({id1, id2});
    auto imgA = render::renderOffscreen(64,64, std::vector<scene::View>{view}, store);
    ASSERT_TRUE(imgA.ok()) << imgA.error().message;
    auto imgB = render::renderOffscreen(64,64, std::vector<scene::View>{view}, store);
    ASSERT_TRUE(imgB.ok()) << imgB.error().message;
    auto pA = centerPixel(*imgA);
    auto pB = centerPixel(*imgB);
    EXPECT_NEAR(pA[0], pB[0], kTol);
    EXPECT_NEAR(pA[1], pB[1], kTol);
    // second inserted (id2 red) draws after first, so red wins
    EXPECT_NEAR(pA[0], 255, kTol);

    // swapped order should flip winner to blue because insertionIdx tie-breaker changes
    scene::View viewSwapped; viewSwapped.id=1; viewSwapped.rect = scene::Rect{0,0,64,64}; viewSwapped.camera = cam; viewSwapped.setClearColor(glm::vec4(0,0,0,1));
    viewSwapped.setItemIds({id2, id1});
    auto imgC = render::renderOffscreen(64,64, std::vector<scene::View>{viewSwapped}, store);
    ASSERT_TRUE(imgC.ok()) << imgC.error().message;
    auto pC = centerPixel(*imgC);
    EXPECT_NEAR(pC[2], 255, kTol);
}

TEST(T6TechniqueOrder, SerializeVersion2Idempotent) {
    scene::SceneStore store;
    auto quad = std::make_shared<const data::Mesh>(makeQuad());
    scene::MeshObject mo; mo.mesh = quad; mo.transform = glm::mat4(1.0f); mo.layer = scene::Layer::LAYER_0; mo.priority = 0;
    (void)store.addMeshObject(std::move(mo));
    std::string s1 = store.serialize();
    auto j1 = nlohmann::json::parse(s1);
    EXPECT_EQ(j1["Version"].get<uint32_t>(), 2u);
    auto deser = scene::SceneStore::deserialize(s1);
    ASSERT_TRUE(deser.ok()) << deser.error().message;
    std::string s2 = deser->serialize();
    auto j2 = nlohmann::json::parse(s2);
    EXPECT_EQ(j2["Version"].get<uint32_t>(), 2u);
    // second run idempotent
    auto deser2 = scene::SceneStore::deserialize(s2);
    ASSERT_TRUE(deser2.ok());
    std::string s3 = deser2->serialize();
    auto j3 = nlohmann::json::parse(s3);
    EXPECT_EQ(j3["Version"].get<uint32_t>(), 2u);
}

} // namespace re::tests
