// tests/t6_volume_math_test.cpp — T6 gate tests (volume/ pure math).
//
// Asserts the volume/ requirements from TASKS.md T6:
//   (1) TransferFunction is exact at control points and a linear ramp between
//       them within 1e-6 (FR-vol.1);
//   (2) front-to-back compositing of a known (color, alpha) sample sequence
//       matches the closed-form alpha-blend result within 1e-6 (FR-vol.2);
//   (3) ray/AABB sampling step positions for a given AABB + ray are analytic
//       (FR-vol.3).
//
// All acceptance constants are hand-computed closed forms documented in
// docs/volume.md. Tolerances follow SPEC §4: pure math within 1e-6.

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include "volume/color.hpp"
#include "volume/ray_caster.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

// Compare two colors channel-wise; every gate assertion is against a
// hand-computed closed-form constant (R4: never a bare ">0" / "non-empty").
void expectColorNear(const re::volume::RgbaColor& actual,
                     const re::volume::RgbaColor& expected, float tol) {
    EXPECT_NEAR(actual.r, expected.r, tol);
    EXPECT_NEAR(actual.g, expected.g, tol);
    EXPECT_NEAR(actual.b, expected.b, tol);
    EXPECT_NEAR(actual.a, expected.a, tol);
}

} // namespace

// ---------------------------------------------------------------------------
// (1) FR-vol.1 — transfer function: exact at control points; linear ramp.
// ---------------------------------------------------------------------------

// A 3-breakpoint ramp: (0 -> opaque red), (0.5 -> {0,1,0,0.5}),
// (1 -> {0,0,1,0.25}). The control-point colors are straight RGBA.
re::volume::TransferFunction makeRamp() {
    return re::volume::TransferFunction({
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {0.5f, {0.0f, 1.0f, 0.0f, 0.5f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 0.25f}},
    });
}

// Sampling is EXACT at every control point (FR-vol.1 acceptance "exact at
// control points").
TEST(T6Volume, TransferFunctionIsExactAtControlPoints) {
    const auto tf = makeRamp();
    expectColorNear(tf.sample(0.0f), {1.0f, 0.0f, 0.0f, 1.0f}, 0.0f);
    expectColorNear(tf.sample(0.5f), {0.0f, 1.0f, 0.0f, 0.5f}, 0.0f);
    expectColorNear(tf.sample(1.0f), {0.0f, 0.0f, 1.0f, 0.25f}, 0.0f);
}

// Sampling between two control points is a linear ramp (FR-vol.1 acceptance
// "linear ramp between them within 1e-6"). Hand-computed closed forms:
//   On [0, 0.5], t = 2v:
//     r = 1 - 2v, g = 2v, b = 0, a = 1 - v.
//     At v = 0.25: (0.5, 0.5, 0, 0.75).
//   On [0.5, 1], t = 2(v - 0.5):
//     r = 0, g = 1 - t, b = t, a = 0.5 - 0.25t.
//     At v = 0.75 (t = 0.5): (0, 0.5, 0.5, 0.375).
TEST(T6Volume, TransferFunctionIsLinearBetweenControlPoints) {
    const auto tf = makeRamp();
    expectColorNear(tf.sample(0.25f), {0.5f, 0.5f, 0.0f, 0.75f}, 1e-6f);
    expectColorNear(tf.sample(0.75f), {0.0f, 0.5f, 0.5f, 0.375f}, 1e-6f);
}

// Outside the control-point range the nearest endpoint color is returned
// (clamping), so the ramp never extrapolates out of [0, 1].
TEST(T6Volume, TransferFunctionClampsOutsideRange) {
    const auto tf = makeRamp();
    expectColorNear(tf.sample(-1.0f), {1.0f, 0.0f, 0.0f, 1.0f}, 0.0f);
    expectColorNear(tf.sample(2.0f), {0.0f, 0.0f, 1.0f, 0.25f}, 0.0f);
}

// A two-breakpoint ramp red->transparent verifies that the ALPHA channel also
// interpolates linearly (not just RGB) — a ramp that lerped only color would
// produce the wrong alpha here.
TEST(T6Volume, TransferFunctionAlphaRampsLinearly) {
    // (0 -> opaque white), (1 -> fully transparent white).
    re::volume::TransferFunction tf({
        {0.0f, {1.0f, 1.0f, 1.0f, 1.0f}},
        {1.0f, {1.0f, 1.0f, 1.0f, 0.0f}},
    });
    // On [0, 1], t = v; alpha = 1 - t. At v = 0.4: alpha = 0.6, rgb stays 1.
    const auto c = tf.sample(0.4f);
    expectColorNear(c, {1.0f, 1.0f, 1.0f, 0.6f}, 1e-6f);
    // At a control point the color is exact.
    expectColorNear(tf.sample(1.0f), {1.0f, 1.0f, 1.0f, 0.0f}, 0.0f);
}

// A single control point returns that color for every input.
TEST(T6Volume, TransferFunctionSinglePointConstant) {
    re::volume::TransferFunction tf({{0.0f, {0.2f, 0.4f, 0.6f, 0.8f}}});
    expectColorNear(tf.sample(-5.0f), {0.2f, 0.4f, 0.6f, 0.8f}, 0.0f);
    expectColorNear(tf.sample(0.0f), {0.2f, 0.4f, 0.6f, 0.8f}, 0.0f);
    expectColorNear(tf.sample(7.0f), {0.2f, 0.4f, 0.6f, 0.8f}, 0.0f);
}

// ---------------------------------------------------------------------------
// (2) FR-vol.2 — front-to-back compositing matches the closed-form blend.
// ---------------------------------------------------------------------------

// Three straight (color, alpha) samples, front-to-back:
//   A = {1, 0, 0, 0.25}, B = {0, 1, 0, 0.5}, C = {0, 0, 1, 1}.
// Closed form (premultiplied alpha, docs/volume.md):
//   out.a   = 1 - (1-0.25)(1-0.5)(1-1)        = 1
//   out.r   = 0.25 * 1                        = 0.25
//   out.g   = (1-0.25) * 0.5 * 1              = 0.375
//   out.b   = (1-0.25)(1-0.5) * 1 * 1         = 0.375
TEST(T6Volume, CompositeMatchesClosedFormBlend) {
    const std::vector<re::volume::RgbaColor> samples = {
        {1.0f, 0.0f, 0.0f, 0.25f},
        {0.0f, 1.0f, 0.0f, 0.5f},
        {0.0f, 0.0f, 1.0f, 1.0f},
    };
    const auto out = re::volume::compositeFrontToBack(samples);
    expectColorNear(out, {0.25f, 0.375f, 0.375f, 1.0f}, 1e-6f);
}

// Front-to-back ordering matters: the same colors in reverse order blend
// differently. The closed form over (C, B, A) is
//   out.a = 1 - (1-1)(1-0.5)(1-0.25) = 1
//   out.b = 1 * 1                    = 1            (C first, opaque)
//   out.g = (1-1) * 0.5 * 1          = 0
//   out.r = (1-1)(1-0.5) * 0.25 * 1  = 0
TEST(T6Volume, CompositeOrderMattersMatchesClosedForm) {
    const std::vector<re::volume::RgbaColor> samples = {
        {0.0f, 0.0f, 1.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, 0.5f},
        {1.0f, 0.0f, 0.0f, 0.25f},
    };
    const auto out = re::volume::compositeFrontToBack(samples);
    expectColorNear(out, {0.0f, 0.0f, 1.0f, 1.0f}, 1e-6f);
}

// An empty sequence is fully transparent (0, 0, 0, 0) — the identity of the
// front-to-back accumulator.
TEST(T6Volume, CompositeEmptyIsTransparent) {
    const std::vector<re::volume::RgbaColor> samples;
    const auto out = re::volume::compositeFrontToBack(samples);
    expectColorNear(out, {0.0f, 0.0f, 0.0f, 0.0f}, 0.0f);
}

// A longer sequence of uniform (white, alpha=0.1) samples verifies the
// geometric-opacity closed form over N steps:
//   out.a = 1 - (1 - 0.1)^N;  out.rgb = out.a (white).
// For N = 8: (1 - 0.1)^8 = 0.9^8 = 0.43046721, so out.a = 0.56953279.
// This exercises the recursion more than the 3-sample case.
TEST(T6Volume, CompositeGeometricOpacityClosedForm) {
    std::vector<re::volume::RgbaColor> samples(
        8, re::volume::RgbaColor{1.0f, 1.0f, 1.0f, 0.1f});
    const auto out = re::volume::compositeFrontToBack(samples);
    // 0.9^8 exactly: 0.43046721.
    const float expectedAlpha = 1.0f - 0.43046721f; // 0.56953279
    expectColorNear(
        out, {expectedAlpha, expectedAlpha, expectedAlpha, expectedAlpha},
        1e-6f);
}

// ---------------------------------------------------------------------------
// (3) FR-vol.3 — analytic ray/AABB sampling step positions.
// ---------------------------------------------------------------------------

// Unit-cube AABB [0,1]^3, ray origin (-1, 0.5, 0.5) travelling +x.
// Closed form (slab method): entry at x=0 -> t=1, exit at x=1 -> t=2.
// With stepLength=0.25, span = 1, count = 4, positions
//   t[k] = 1 + (k + 0.5) * 0.25 = {1.125, 1.375, 1.625, 1.875}.
TEST(T6Volume, RaySampleStepsAreAnalytic) {
    const re::volume::Ray ray{{-1.0f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}};
    const re::volume::Aabb aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    const auto steps = re::volume::computeRaySampleSteps(ray, aabb, 0.25f);

    EXPECT_NEAR(steps.tEntry, 1.0f, 1e-6f);
    EXPECT_NEAR(steps.tExit, 2.0f, 1e-6f);
    ASSERT_EQ(steps.positions.size(), 4u);
    EXPECT_NEAR(steps.positions[0], 1.125f, 1e-6f);
    EXPECT_NEAR(steps.positions[1], 1.375f, 1e-6f);
    EXPECT_NEAR(steps.positions[2], 1.625f, 1e-6f);
    EXPECT_NEAR(steps.positions[3], 1.875f, 1e-6f);
}

// The step count is floor(span / stepLength), not ceil — for a span that is
// not an exact multiple the trailing partial step is dropped. Span = 1,
// stepLength = 0.3 -> count = floor(3.333) = 3, positions 0.15, 0.45, 0.75
// (relative to entry at t=1 -> 1.15, 1.45, 1.75).
TEST(T6Volume, RaySampleStepsFloorPartialStep) {
    const re::volume::Ray ray{{-1.0f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}};
    const re::volume::Aabb aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    const auto steps = re::volume::computeRaySampleSteps(ray, aabb, 0.3f);

    EXPECT_NEAR(steps.tEntry, 1.0f, 1e-6f);
    EXPECT_NEAR(steps.tExit, 2.0f, 1e-6f);
    ASSERT_EQ(steps.positions.size(), 3u);
    EXPECT_NEAR(steps.positions[0], 1.15f, 1e-6f);
    EXPECT_NEAR(steps.positions[1], 1.45f, 1e-6f);
    EXPECT_NEAR(steps.positions[2], 1.75f, 1e-6f);
}

// A ray that starts inside the box has entry t = 0. Origin (0.5, 0.5, 0.5)
// moving +z: exit at z=1 -> t = 0.5. stepLength = 0.25 -> span 0.5, count 2,
// positions 0.125, 0.375.
TEST(T6Volume, RaySampleStepsOriginInsideBox) {
    const re::volume::Ray ray{{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}};
    const re::volume::Aabb aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    const auto steps = re::volume::computeRaySampleSteps(ray, aabb, 0.25f);

    EXPECT_NEAR(steps.tEntry, 0.0f, 1e-6f);
    EXPECT_NEAR(steps.tExit, 0.5f, 1e-6f);
    ASSERT_EQ(steps.positions.size(), 2u);
    EXPECT_NEAR(steps.positions[0], 0.125f, 1e-6f);
    EXPECT_NEAR(steps.positions[1], 0.375f, 1e-6f);
}

// A ray that misses the AABB yields an empty step set with tEntry == tExit ==
// 0 (the documented miss sentinel).
TEST(T6Volume, RayMissYieldsNoSteps) {
    // The box is [0,1]^3; a ray at y = -1 is outside the y-slab entirely.
    const re::volume::Ray ray{{-1.0f, -1.0f, 0.5f}, {1.0f, 0.0f, 0.0f}};
    const re::volume::Aabb aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    const auto steps = re::volume::computeRaySampleSteps(ray, aabb, 0.25f);

    EXPECT_EQ(steps.tEntry, 0.0f);
    EXPECT_EQ(steps.tExit, 0.0f);
    EXPECT_TRUE(steps.positions.empty());
}

// The underlying ray/AABB intersection is exact for a face-on hit along -z.
// Origin (0.5, 0.5, 2), direction (0, 0, -1): entry at z=1 -> t=1, exit at
// z=0 -> t=2.
TEST(T6Volume, IntersectRayAabbFaceOnHit) {
    const re::volume::Ray ray{{0.5f, 0.5f, 2.0f}, {0.0f, 0.0f, -1.0f}};
    const re::volume::Aabb aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    float tEntry = 0.0f;
    float tExit = 0.0f;
    ASSERT_TRUE(re::volume::intersectRayAabb(ray, aabb, tEntry, tExit));
    EXPECT_NEAR(tEntry, 1.0f, 1e-6f);
    EXPECT_NEAR(tExit, 2.0f, 1e-6f);
}

// A ray pointing AWAY from the box misses (the box is entirely behind the
// origin, tExit < 0) — the closed-form tFar < 0 rejection.
TEST(T6Volume, IntersectRayAabbBoxBehindOriginMisses) {
    const re::volume::Ray ray{{0.5f, 0.5f, -1.0f}, {0.0f, 0.0f, -1.0f}};
    const re::volume::Aabb aabb{{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    float tEntry = 0.0f;
    float tExit = 0.0f;
    EXPECT_FALSE(re::volume::intersectRayAabb(ray, aabb, tEntry, tExit));
}

} // namespace re::tests
