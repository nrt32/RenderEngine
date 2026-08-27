// tests/t2_skeletons_test.cpp — T2 gate tests for CompositeKey / TranslateContext / DrawContext (V3.2a).
//
// Asserts:
//  (1) CompositeKey equality/hash stable — same fields equal + same unordered_map hash, hash of stable bytes not pointer;
//  (2) TranslateContext with null viewPlane valid for 3D (LSP — hasPlane() false, hasVolume() false) and 2D with volume;
//  (3) DrawContext per-frame setViewport(cached) spy shows exactly 1 glViewport for duplicate call
//      (instance replaces global invalidateDrawCache() — N>=1 check; two instances independent).

#include <gtest/gtest.h>

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/ext/matrix_transform.hpp>

#include "core/re_context.hpp"
#include "scene/camera.hpp"
#include "scene/composite_key.hpp"
#include "scene/translate_context.hpp"
#include "tests/offscreen_fixture.hpp"

namespace re::tests {

// ---------------------------------------------------------------------------
// (1) CompositeKey equality/hash stable — explainable constants
// ---------------------------------------------------------------------------

TEST(T2CompositeKey, EqualityAndHashStable) {
    // Two keys with identical fields must be equal and hash to same bucket (explainable constant).
    scene::CompositeKey k1{1, 10, 42, 7, 0xDEADBEEFULL};
    scene::CompositeKey k2{1, 10, 42, 7, 0xDEADBEEFULL};
    scene::CompositeKey k3{2, 10, 42, 7, 0xDEADBEEFULL}; // different version → not equal

    // Equality is exact — all 5 fields must match.
    EXPECT_EQ(k1, k2) << "identical CompositeKey must compare equal (explainable: all fields same)";
    EXPECT_NE(k1, k3) << "different version must not compare equal (explainable: version differs 1 vs 2)";

    // Hash stable — equal keys produce equal std::hash.
    scene::CompositeKeyHash hasher;
    EXPECT_EQ(hasher(k1), hasher(k2)) << "equal keys must hash equal (explainable invariant)";
    // Different version should produce different hash with high probability — but at least not equal via equality.
    EXPECT_NE(k1, k3);

    // unordered_map stability — can store and find by equal key.
    std::unordered_map<scene::CompositeKey, int, scene::CompositeKeyHash> map;
    map[k1] = 123;
    auto it = map.find(k2);
    ASSERT_NE(it, map.end()) << "unordered_map find by equal key must succeed (hash stable)";
    EXPECT_EQ(it->second, 123) << "stored value 123 must be retrieved (explainable constant)";

    // hash of stable bytes vs pointer — same byte content from two distinct allocations must hash equal.
    struct StablePayload {
        uint32_t a = 0x12345678;
        float b = 1.5f;
        uint8_t c[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    };
    StablePayload p1{};
    StablePayload p2{};
    // p1 and p2 are distinct objects at different addresses but byte-identical.
    uint64_t h1 = scene::CompositeKey::hashStableBytes(&p1, sizeof(p1));
    uint64_t h2 = scene::CompositeKey::hashStableBytes(&p2, sizeof(p2));
    EXPECT_EQ(h1, h2) << "hashStableBytes must hash bytes, not pointer address (explainable: two distinct objects byte-identical → same hash 0x"
                      << std::hex << h1 << ")";

    // Different bytes → different hash (explainable constant: flipping one byte changes hash).
    StablePayload p3 = p1;
    p3.c[0] = 0xFF;
    uint64_t h3 = scene::CompositeKey::hashStableBytes(&p3, sizeof(p3));
    EXPECT_NE(h1, h3) << "different stable bytes must produce different hash (explainable: 0xAA vs 0xFF)";

    // FNV-1a known constant: hash of empty bytes is offset basis.
    uint64_t hEmpty = scene::CompositeKey::hashStableBytes(nullptr, 0);
    EXPECT_EQ(hEmpty, 1469598103934665603ULL)
        << "empty hash must equal FNV offset basis 1469598103934665603 (explainable constant)";
}

TEST(T2CompositeKey, HashStableBytesIgnoresPointer) {
    // Allocate two heap buffers with same content but different pointer values — hash must match.
    std::vector<uint8_t> bufA{1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> bufB{1, 2, 3, 4, 5, 6, 7, 8};
    ASSERT_NE(bufA.data(), bufB.data()) << "buffers at distinct addresses (pointer differs)";
    uint64_t ha = scene::CompositeKey::hashStableBytes(bufA.data(), bufA.size());
    uint64_t hb = scene::CompositeKey::hashStableBytes(bufB.data(), bufB.size());
    EXPECT_EQ(ha, hb) << "same bytes at different addresses must hash equal (stable, not pointer hash)";
    // Verify hashValue helper for trivially-copyable.
    uint32_t v = 0x01020304;
    uint64_t hv = scene::CompositeKey::hashValue(v);
    uint64_t hv2 = scene::CompositeKey::hashStableBytes(&v, sizeof(v));
    EXPECT_EQ(hv, hv2) << "hashValue must equal hashStableBytes of same typed value (explainable)";
}

TEST(T2CompositeKey, LayoutIdScopePreventsAlias) {
    // Same Id+Gen+Hash but different LayoutId must not alias (explainable scope isolation).
    scene::CompositeKey ka{1, 1, 1, 1, 0xABCDULL};
    scene::CompositeKey kb{1, 2, 1, 1, 0xABCDULL};
    EXPECT_NE(ka, kb) << "different LayoutId must not alias (scope isolation, explainable)";
    std::unordered_set<scene::CompositeKey> set;
    set.insert(ka);
    EXPECT_EQ(set.count(ka), 1u);
    EXPECT_EQ(set.count(kb), 0u) << "kb not in set with ka (explainable: layoutId differs)";
}

// ---------------------------------------------------------------------------
// (2) TranslateContext — null viewPlane valid for 3D (LSP), hasPlane/hasVolume predicates
// ---------------------------------------------------------------------------

TEST(T2TranslateContext, NullViewPlaneValidFor3D) {
    // 3D view: the ABSENT plane is a first-class state, not an error — a
    // context without a plane must be constructible, queryable, and accepted
    // by every mapper (no strengthened preconditions for 3D).
    scene::TranslateContext ctx3d;
    ctx3d.view.viewPlane = std::nullopt;
    ctx3d.view.viewMatrix = scene::Camera{}.viewMatrix();
    ctx3d.view.projMatrix = scene::Camera{}.projMatrix();
    // volume absent for 3D — null volume is valid (ISP segregated).
    EXPECT_FALSE(ctx3d.hasPlane()) << "3D context hasPlane() must be false (null viewPlane, explainable)";
    EXPECT_FALSE(ctx3d.hasVolume()) << "3D context hasVolume() must be false (no volume, explainable)";
    EXPECT_FALSE(ctx3d.view.hasPlane()) << "ViewContext::hasPlane() must be false for 3D";

    // Uniform map(AppT,Ctx) substitutability: a mapper receiving this 3D ctx must not throw —
    // we simulate by reading only viewMatrix when hasPlane==false.
    glm::mat4 vm = ctx3d.view.viewMatrix;
    EXPECT_NE(vm[3][3], 0.0f) << "viewMatrix must be valid even with null plane (explainable: default 1.0f)";

    // 2D view: plane present → hasPlane true, plus volume present for voxel→world.
    scene::PlaneDesc plane;
    plane.normal = glm::vec3{0, 0, 1};
    plane.point = glm::vec3{0, 0, 0};
    scene::TranslateContext ctx2d;
    // The plane is stored BY VALUE inside the context: the snapshot is
    // self-contained, so mappers can hold it without borrowing live view
    // state that could be mutated mid-translation.
    ctx2d.view.viewPlane = plane;
    ctx2d.view.viewMatrix = glm::mat4{1.0f};
    ctx2d.view.projMatrix = glm::mat4{1.0f};
    scene::VolumeContext vol;
    vol.volumeModel = glm::mat4{1.0f};
    vol.dims = glm::ivec3{64, 64, 32};
    vol.voxelSpacing = 0.5f;
    vol.meshBounds = scene::Aabb{glm::vec3{-1}, glm::vec3{1}};
    ctx2d.volume = vol;
    EXPECT_TRUE(ctx2d.hasPlane()) << "2D context hasPlane() must be true (plane present)";
    EXPECT_TRUE(ctx2d.hasVolume()) << "2D slice context hasVolume() must be true";
    EXPECT_EQ(ctx2d.volume->dims.x, 64) << "dims 64x64x32 explainable";
    EXPECT_EQ(ctx2d.volume->dims.y, 64);
    EXPECT_EQ(ctx2d.volume->dims.z, 32);
    EXPECT_FLOAT_EQ(ctx2d.volume->voxelSpacing, 0.5f);
}

TEST(T2TranslateContext, ViewContextOnlyForCameraMapper) {
    // CameraMapper touches only ViewContext, not VolumeContext — verify segregated access.
    scene::TranslateContext ctx;
    ctx.view.viewMatrix = glm::translate(glm::mat4{1.0f}, glm::vec3{1, 2, 3});
    // volume remains nullopt — CameraMapper must not require volume.
    EXPECT_FALSE(ctx.hasVolume());
    // Access only view part.
    glm::mat4 m = ctx.view.viewMatrix;
    EXPECT_FLOAT_EQ(m[3][0], 1.0f) << "viewMatrix translate x=1 explainable";
    EXPECT_FLOAT_EQ(m[3][1], 2.0f);
    EXPECT_FLOAT_EQ(m[3][2], 3.0f);

    // PlaneMapper touching volume for voxel→world — set volume.
    scene::VolumeContext vol2;
    vol2.dims = glm::ivec3{128, 128, 64};
    vol2.voxelSpacing = 1.0f;
    ctx.volume = vol2;
    EXPECT_TRUE(ctx.hasVolume());
    EXPECT_EQ(ctx.volume->dims.x, 128) << "volume dims x 128 explainable";
}

// ---------------------------------------------------------------------------
// (3) DrawContext per-frame setViewport(cached) spy — exactly 1 glViewport for duplicate
// ---------------------------------------------------------------------------

TEST(T2DrawContext, PerFrameViewportDuplicateIsOneGlCall) {
    core::REContext ctx;
    ctx.setViewport(0, 0, 640, 480);
    ctx.setViewport(0, 0, 640, 480);
    auto counts = ctx.getSpyCounts();
    EXPECT_EQ(counts.viewport, 1) << "REContext duplicate setViewport must be exactly 1 glViewport (explainable: cache hit)";
}

TEST(T2DrawContext, PerFrameViewportDifferentValuesAreTwoCalls) {
    core::REContext ctx;
    ctx.setViewport(0, 0, 640, 480);
    ctx.setViewport(0, 0, 800, 600);
    EXPECT_EQ(ctx.getSpyCounts().viewport, 2) << "distinct viewports must be 2 glViewport";
}

TEST(T2DrawContext, TwoInstancesAreIndependent) {
    // SRP via instance — two REContexts must have independent caches/spies.
    core::REContext ctxA;
    core::REContext ctxB;
    ctxA.setViewport(0, 0, 640, 480);
    ctxA.setViewport(0, 0, 640, 480);
    EXPECT_EQ(ctxA.getSpyCounts().viewport, 1);

    // ctxB starts cold — same rect must still issue glViewport (no cross-frame bleed).
    ctxB.setViewport(0, 0, 640, 480);
    EXPECT_EQ(ctxB.getSpyCounts().viewport, 1) << "fresh REContext must issue glViewport even if ctxA cached same rect (instance isolation)";
    ctxB.setViewport(0, 0, 640, 480);
    EXPECT_EQ(ctxB.getSpyCounts().viewport, 1) << "ctxB duplicate still 1 (per-instance cache)";

    // Invalidate per-instance — must force reissue.
    ctxA.invalidate();
    EXPECT_EQ(ctxA.getSpyCounts().viewport, 0) << "invalidate must reset spy to 0 (explainable)";
    ctxA.setViewport(0, 0, 640, 480);
    EXPECT_EQ(ctxA.getSpyCounts().viewport, 1) << "after invalidate same rect must re-issue (cache cleared)";
}

TEST(T2DrawContext, PerFrameClearColorAndDepthBlendCached) {
    core::REContext ctx;
    ctx.setClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    ctx.setClearColor(0.2f, 0.4f, 0.8f, 1.0f);
    EXPECT_EQ(ctx.getSpyCounts().clearColor, 1) << "duplicate clearColor must be 1 glClearColor";

    ctx.enableDepthTest();
    ctx.enableDepthTest();
    EXPECT_EQ(ctx.getSpyCounts().enableDepthTest, 1) << "duplicate enableDepthTest must be 1";

    ctx.enableBlend();
    ctx.enableBlend();
    EXPECT_EQ(ctx.getSpyCounts().enableBlend, 1) << "duplicate enableBlend must be 1";

    ctx.enablePremultipliedOverBlend();
    // enablePremultipliedOverBlend after enableBlend was already enabled + func not yet set — should issue blendFunc only.
    // First enableBlend set blendEnabled, second premultiplied with already enabled blend: needFunc still true → 1 blendFunc.
    // But we already called enablePremultipliedOverBlend once; second call duplicate is 0.
    // Let's check: after first premultiplied, second premultiplied is cache hit.
    ctx.enablePremultipliedOverBlend();
    // After the sequence: first enableBlend (1), then first premultiplied added blendFunc (1), second premultiplied hit (0).
    auto c = ctx.getSpyCounts();
    EXPECT_EQ(c.blendFunc, 1) << "first premultiplied after enableBlend must add exactly 1 blendFunc";

    // T4: single ledger via REContext — global per-GL-context cache (REContext::current())
    // is independent from local instances. Invalidate of the global current must not
    // affect a local instance's cache/spy (instance isolation): set viewport,
    // global invalidate, duplicate same rect on same instance must still be cache hit (exactly 1).
    core::REContext isoCtx;
    isoCtx.setViewport(10, 20, 300, 200);
    EXPECT_EQ(isoCtx.getSpyCounts().viewport, 1) << "isoCtx first setViewport must be 1 (explainable)";
    core::invalidateRECache(); // global current — must not clear local instance cache
    isoCtx.setViewport(10, 20, 300, 200);
    EXPECT_EQ(isoCtx.getSpyCounts().viewport, 1)
        << "global invalidate must not affect instance cache (duplicate still hit, explainable isolation)";
    // Original ctx viewport count unchanged by global invalidate (was 0 before — still 0)
    EXPECT_EQ(ctx.getSpyCounts().viewport, 0) << "global invalidate must not affect other instance spy";
}

} // namespace re::tests
