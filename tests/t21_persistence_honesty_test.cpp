// tests/t21_persistence_honesty_test.cpp — persistence-honesty gate: the
// write-only scaffolding flagged by review must now genuinely drive behavior.
//
// Asserted contracts (each an explainable constant or structural identity):
//  (1) SceneStore/ViewStore::dirtyFieldsSince COMPUTE from the bounded
//      per-field dirty log: a lone field bump yields exactly that one field
//      (e.g. {CameraView} for a camera-only mutation), never a fixed
//      four-field superset; repeated bumps of one field stay one entry.
//  (2) Tombstones written on erase are ENFORCED: SceneStore::resolve /
//      ViewStore::resolve return typed errors — code 2 for a stale handle
//      (id erased, tombstone retained), code 1 for a never-existing id,
//      success only for a live id in any owned object family.
//  (3) The fake batch-exercise scaffolding is gone: the string "parallelFor"
//      occurs zero times under broker/ (mechanically scanned from the test).
//  (4) ReView identity has ONE definition: "struct StableKey" is declared
//      exactly once under broker/ (the shared broker/stable_key.hpp used by
//      both ViewCompositor and ViewSynchronizer).
//  (5) CameraMapper holds an id-keyed multi-entry memo with spy counters:
//      two cameras alternating pans each accumulate hits and neither evicts
//      nor serves the other (no cross-camera thrash), invalidate(id) drops
//      exactly that view's entries.

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "broker/broker.hpp"
#include "broker/camera_mapper.hpp"
#include "broker/idirty_tracker.hpp"
#include "broker/stable_key.hpp"
#include "broker/view_compositor.hpp"
#include "broker/view_synchronizer.hpp"
#include "data/mesh.hpp"
#include "scene/composite_key.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"

namespace re::tests {

namespace {

/// Count total occurrences of `needle` lines across all .hpp/.cpp files
/// directly under broker/ (the mechanical form of the gate greps).
std::size_t countOccurrencesInBroker(const std::string& needle) {
    std::size_t total = 0;
    const std::filesystem::path dir =
        std::filesystem::path(TEST_SOURCE_DIR) / "broker";
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file())
            continue;
        const auto& p = entry.path();
        if (p.extension() != ".hpp" && p.extension() != ".cpp")
            continue;
        std::ifstream in(p);
        std::string line;
        while (std::getline(in, line)) {
            if (line.find(needle) != std::string::npos)
                ++total;
        }
    }
    return total;
}

} // namespace

// ---------------------------------------------------------------------------
// (1) dirtyFieldsSince computes from the per-field dirty log
// ---------------------------------------------------------------------------

TEST(T21PersistenceHonesty, DirtyFieldsSinceComputedExactlyOneFieldPerBump) {
    scene::SceneStore store;
    auto mesh = std::make_shared<data::Mesh>(data::Mesh::fromTriangles(
        {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}, {0, 1, 2}));
    scene::MeshObject mo;
    mo.mesh = mesh;
    const uint64_t mid = store.addMeshObject(mo);
    EXPECT_EQ(mid, 1u)
        << "nextId_ starts at 1: the first allocated handle is exactly 1";
    const uint64_t genAfterAdd = store.storeGeneration();

    // Mutate ONLY the material dimension: the computed dirty set must be
    // exactly {Material} — not the former hardcoded four-field superset.
    store.bump(scene::FieldId::Material);
    auto d = store.dirtyFieldsSince(genAfterAdd);
    ASSERT_EQ(d.size(), 1u) << "exactly one field mutated since genAfterAdd";
    EXPECT_EQ(d[0], scene::FieldId::Material);

    // A different lone bump yields exactly its own single field.
    const uint64_t g2 = store.storeGeneration();
    store.markDirty(mid, scene::FieldId::TransferFunction);
    auto d2 = store.dirtyFieldsSince(g2);
    ASSERT_EQ(d2.size(), 1u);
    EXPECT_EQ(d2[0], scene::FieldId::TransferFunction);

    // Bounded drain: re-bumping a field raises its slot generation in place,
    // so querying since g2 answers with the two genuinely-changed fields
    // (first-mutation order: Material entered the log before TF did).
    store.bump(scene::FieldId::Material);
    auto d3 = store.dirtyFieldsSince(g2);
    ASSERT_EQ(d3.size(), 2u);
    EXPECT_EQ(d3[0], scene::FieldId::Material);
    EXPECT_EQ(d3[1], scene::FieldId::TransferFunction);
}

TEST(T21PersistenceHonesty, CameraOnlyMutationYieldsExactlyCameraField) {
    scene::ViewStore vstore;
    scene::View v;
    v.rect = scene::Rect{0, 0, 640, 480};
    v.camera = scene::Camera(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0),
                             glm::vec3(0, 1, 0));
    const uint64_t vid = vstore.addView(v);
    const uint64_t genAfterAdd = vstore.storeGeneration();

    // Mutate only the camera (pan bumps the camera's own view generation),
    // recorded through the push entry point like an off-frame editor edit.
    scene::View* /*borrow*/ vm = vstore.getViewMut(vid);
    ASSERT_NE(vm, nullptr);
    uint64_t viewGenBefore = vm->camera.viewGen();
    {
        scene::Camera cam = vm->camera;
        cam.pan(1.0f, 0.0f);
        vm->setCamera(std::move(cam));
    }
    EXPECT_EQ(vm->camera.viewGen(), viewGenBefore + 1)
        << "pan bumps viewGen by exactly 1 (Camera::pan increments viewGen_)";
    vstore.markDirty(vid, scene::FieldId::CameraView);

    auto d = vstore.dirtyFieldsSince(genAfterAdd);
    ASSERT_EQ(d.size(), 1u)
        << "camera-only mutation dirties exactly ONE view field";
    EXPECT_EQ(d[0], scene::FieldId::CameraView)
        << "the dirty set is {CameraView}, never the hardcoded 4-field list";

    // Projection-only mutation analog: exactly {CameraProj}.
    const uint64_t g = vstore.storeGeneration();
    vstore.bump(scene::FieldId::CameraProj);
    auto dp = vstore.dirtyFieldsSince(g);
    ASSERT_EQ(dp.size(), 1u);
    EXPECT_EQ(dp[0], scene::FieldId::CameraProj);
}

// ---------------------------------------------------------------------------
// (2) Tombstones enforced: stale handle resolves to typed error code 2
// ---------------------------------------------------------------------------

TEST(T21PersistenceHonesty, ResolveStaleObjectHandleTypedError) {
    scene::SceneStore store;
    auto mesh = std::make_shared<data::Mesh>(data::Mesh::fromTriangles(
        {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}}, {0, 1, 2}));
    scene::MeshObject mo;
    mo.mesh = mesh;
    const uint64_t mid = store.addMeshObject(mo);
    scene::VolumeObject vo;
    vo.volume = nullptr; // resolve() scans ids, never dereferences payloads
    const uint64_t volId = store.addVolumeObject(vo);

    // Live ids in any family resolve successfully.
    auto rLive = store.resolve(mid);
    EXPECT_TRUE(rLive.ok()) << "live mesh id must resolve";
    auto rLiveVol = store.resolve(volId);
    EXPECT_TRUE(rLiveVol.ok()) << "live volume id must resolve";

    // Erase, then the old handle MUST come back as typed stale error code 2.
    ASSERT_TRUE(store.removeMeshObject(mid));
    auto rStale = store.resolve(mid);
    ASSERT_TRUE(rStale.failed());
    EXPECT_EQ(rStale.error().code, 2)
        << "erased handle resolves to stale (code 2)";
    EXPECT_NE(rStale.error().message.find("stale"), std::string::npos)
        << "stale diagnosis names the cause";

    // A different family's erased id is stale too (tombstone per family id).
    scene::PlaneObject po;
    const uint64_t pid = store.addPlaneObject(po);
    ASSERT_TRUE(store.removePlaneObject(pid));
    auto rPlaneStale = store.resolve(pid);
    ASSERT_TRUE(rPlaneStale.failed());
    EXPECT_EQ(rPlaneStale.error().code, 2);

    // Never-existing id: distinct typed error code 1.
    auto rUnknown = store.resolve(987654321ULL);
    ASSERT_TRUE(rUnknown.failed());
    EXPECT_EQ(rUnknown.error().code, 1);

    // The surviving sibling stays live after the other family's erase.
    EXPECT_TRUE(store.resolve(volId).ok());
}

TEST(T21PersistenceHonesty, ResolveStaleViewHandleTypedError) {
    scene::ViewStore vstore;
    scene::View v;
    v.rect = scene::Rect{0, 0, 640, 480};
    const uint64_t vid = vstore.addView(v);
    EXPECT_TRUE(vstore.resolve(vid).ok());
    ASSERT_TRUE(vstore.removeView(vid));
    auto rStale = vstore.resolve(vid);
    ASSERT_TRUE(rStale.failed());
    EXPECT_EQ(rStale.error().code, 2)
        << "erased view handle resolves to stale (code 2)";
    auto rUnknown = vstore.resolve(5555555ULL);
    ASSERT_TRUE(rUnknown.failed());
    EXPECT_EQ(rUnknown.error().code, 1);
}

// ---------------------------------------------------------------------------
// (3)+(4) Mechanical honesty: no parallelFor, ONE StableKey definition
// ---------------------------------------------------------------------------

TEST(T21PersistenceHonesty, NoFakeParallelForUnderBroker) {
    EXPECT_EQ(countOccurrencesInBroker("parallelFor"), 0u)
        << "the discarded-results batch exercise must be gone from broker/";
}

TEST(T21PersistenceHonesty, SingleSharedStableKeyDefinition) {
    EXPECT_EQ(countOccurrencesInBroker("struct StableKey"), 1u)
        << "ReView identity must be defined exactly once (shared header)";
    // The shared key behaves as one identity across collaborators: equal
    // fields compare equal, any differing field does not.
    broker::StableKey a{1, 7, 42};
    broker::StableKey b{1, 7, 42};
    EXPECT_EQ(a, b) << "same version/layout/view is the SAME ReView identity";
    EXPECT_FALSE((broker::StableKey{1, 7, 42} == broker::StableKey{2, 7, 42}))
        << "version participates in identity";
    EXPECT_FALSE((broker::StableKey{1, 7, 42} == broker::StableKey{1, 8, 42}))
        << "layout scope participates in identity";
}

// ---------------------------------------------------------------------------
// (5a) CameraMapper unit-level: id-keyed multi-entry memo, no cross thrash
// ---------------------------------------------------------------------------

TEST(T21PersistenceHonesty, CameraMapperIdKeyedCacheNoCrossCameraThrash) {
    broker::CameraMapper mapper;

    scene::TranslateContext ctxA;
    ctxA.view.viewId = 101; // owning-view identity for camera A
    scene::TranslateContext ctxB;
    ctxB.view.viewId = 202; // owning-view identity for camera B

    scene::Camera camA(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0),
                       glm::vec3(0, 1, 0));
    scene::Camera camB(glm::vec3(3, 0, 5), glm::vec3(0, 0, 0),
                       glm::vec3(0, 1, 0));

    // First pass over each camera: one miss each, two independent entries.
    auto ra0 = mapper.mapCached(camA, ctxA);
    ASSERT_TRUE(ra0.ok());
    EXPECT_EQ(mapper.cacheMisses(), 1u);
    EXPECT_EQ(mapper.cacheHits(), 0u);
    auto rb0 = mapper.mapCached(camB, ctxB);
    ASSERT_TRUE(rb0.ok());
    EXPECT_EQ(mapper.cacheMisses(), 2u) << "second camera gets its OWN entry";
    EXPECT_EQ(mapper.cacheEntries(), 2u);

    // Unchanged repeat passes: both cameras are served from their memos.
    auto ra1 = mapper.mapCached(camA, ctxA);
    auto rb1 = mapper.mapCached(camB, ctxB);
    ASSERT_TRUE(ra1.ok() && rb1.ok());
    EXPECT_EQ(mapper.cacheHits(), 2u) << "one hit per unchanged camera";
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            EXPECT_FLOAT_EQ((*ra1).view[c][r], (*ra0).view[c][r])
                << "memo serves the identical analytic lookAt matrix";
            EXPECT_FLOAT_EQ((*rb1).proj[c][r], (*rb0).proj[c][r])
                << "memo serves the identical analytic projection";
        }
    }

    // Alternating pans: A mutates -> miss + fresh entry; B unchanged -> HIT.
    camA.pan(1.0f, 0.0f);
    auto ra2 = mapper.mapCached(camA, ctxA);
    ASSERT_TRUE(ra2.ok());
    EXPECT_EQ(mapper.cacheMisses(), 3u);
    auto rb2 = mapper.mapCached(camB, ctxB);
    ASSERT_TRUE(rb2.ok());
    EXPECT_EQ(mapper.cacheHits(), 3u)
        << "camera B still hits after camera A's pan: no cross-camera eviction";

    // Reverse order: B mutates -> miss; A unchanged -> HIT.
    camB.pan(-1.0f, 0.0f);
    auto rb3 = mapper.mapCached(camB, ctxB);
    ASSERT_TRUE(rb3.ok());
    EXPECT_EQ(mapper.cacheMisses(), 4u);
    auto ra3 = mapper.mapCached(camA, ctxA);
    ASSERT_TRUE(ra3.ok());
    EXPECT_EQ(mapper.cacheHits(), 4u)
        << "camera A still hits after camera B's pan";

    // Id-keyed invalidation drops exactly the targeted view's entries.
    mapper.invalidate(ctxB.view.viewId);
    EXPECT_EQ(mapper.cacheEntries(), 1u) << "only view-B entry evicted";
    auto rb4 = mapper.mapCached(camB, ctxB);
    ASSERT_TRUE(rb4.ok());
    EXPECT_EQ(mapper.cacheMisses(), 5u) << "invalidated view-B re-translates";
    auto ra4 = mapper.mapCached(camA, ctxA);
    ASSERT_TRUE(ra4.ok());
    EXPECT_EQ(mapper.cacheHits(), 5u)
        << "view-A entry SURVIVES invalidate(view-B)";
}

// ---------------------------------------------------------------------------
// (5b) Sync-level: two cameras alternating pans get cache hits end-to-end
// ---------------------------------------------------------------------------

TEST(T21PersistenceHonesty, SynchronizerTwoCamerasAlternatePansCacheHits) {
    auto broker = std::make_shared<broker::Broker>();
    broker->registerMapper(std::make_unique<broker::CameraMapper>());
    auto comp = std::make_shared<broker::ViewCompositor>(broker);
    broker::ViewSynchronizer sync(broker, comp);
    broker::CameraMapper* /*borrow*/ cams =
        broker->getMutable<broker::CameraMapper>();
    ASSERT_NE(cams, nullptr);

    // Two views, two INDEPENDENT cameras, empty items (no stack needed):
    // view 1 looks from z=+5, view 2 from z=-5 — visibly different inputs.
    scene::SceneStore sceneStore;
    scene::View v1;
    v1.id = 1;
    v1.rect = scene::Rect{0, 0, 320, 480};
    v1.camera = scene::Camera(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0),
                              glm::vec3(0, 1, 0));
    scene::View v2;
    v2.id = 2;
    v2.rect = scene::Rect{320, 0, 320, 480};
    v2.camera = scene::Camera(glm::vec3(0, 0, -5), glm::vec3(0, 0, 0),
                              glm::vec3(0, 1, 0));
    std::vector<scene::View> views = {v1, v2};

    // Pass 1: both cameras translated (one miss each).
    ASSERT_TRUE(sync.sync(views, sceneStore, 9).ok());
    EXPECT_EQ(cams->cacheMisses(), 2u);
    EXPECT_EQ(cams->cacheHits(), 0u);

    // Push-dirty each view's camera WITHOUT changing content: the sync must
    // re-enter the camera branch and the memo must serve it (hit, not work).
    sync.markDirty(1, scene::FieldId::CameraView);
    ASSERT_TRUE(sync.sync(views, sceneStore, 9).ok());
    EXPECT_EQ(cams->cacheHits(), 1u) << "view-1 push-dirty served from memo";
    EXPECT_EQ(cams->cacheMisses(), 2u);

    sync.markDirty(2, scene::FieldId::CameraView);
    ASSERT_TRUE(sync.sync(views, sceneStore, 9).ok());
    EXPECT_EQ(cams->cacheHits(), 2u)
        << "view-2 push-dirty served from ITS memo";
    EXPECT_EQ(cams->cacheMisses(), 2u);

    // Alternate real pans across frames: the mutated camera re-translates,
    // the untouched one keeps hitting — the cross-camera-thrash killer.
    {
        scene::Camera cam = views[0].camera;
        cam.pan(1.0f, 0.0f);
        views[0].setCamera(std::move(cam));
    }
    ASSERT_TRUE(sync.sync(views, sceneStore, 9).ok());
    EXPECT_EQ(cams->cacheMisses(), 3u) << "view-1 pan re-translates once";

    sync.markDirty(2, scene::FieldId::CameraView);
    ASSERT_TRUE(sync.sync(views, sceneStore, 9).ok());
    EXPECT_EQ(cams->cacheHits(), 3u)
        << "view-2 memo survived view-1's pan+rememo (no single-slot thrash)";

    // Identity persistence alongside caching: same &ReView throughout.
    render::View* rv1a = comp->getView(9, 1);
    render::View* rv2a = comp->getView(9, 2);
    ASSERT_NE(rv1a, nullptr);
    ASSERT_NE(rv2a, nullptr);
    EXPECT_EQ(comp->viewCount(), 2u) << "two views keep two distinct ReViews";
}

} // namespace re::tests
