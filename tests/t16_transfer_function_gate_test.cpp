// tests/t16_transfer_function_gate_test.cpp — T16 gate: TransferFunction
// valid default + defensive sample + toByte clamp.
//
// Asserts the T16 deliverable constants (R4 explainable values):
//   - default TransferFunction is the degenerate ramp
//     vec4(0,0,0,0) at 0.0 -> vec4(1,1,1,1) at 1.0 (pinned endpoints);
//   - sample(0.0)==(0,0,0,0), sample(0.5)==(0.5,0.5,0.5,0.5),
//     sample(1.0)==(1,1,1,1) within 1e-6 (linear ramp analytic);
//   - an explicitly empty control-point list returns transparent black
//     (defensive, no UB on front()/back());
//   - toByteClamped(1.5)==255 and toByteClamped(-0.2)==0 (clamp before
//     float->uint8 cast, no UB), plus boundary 0->0 and 1->255.

#include <gtest/gtest.h>

#include <vector>

#include "app/mpr_slice.hpp"
#include "volume/color.hpp"
#include "volume/transfer_function.hpp"

namespace re::tests {
namespace {

constexpr float kTol = 1e-6f;

void expectColorNear(const re::volume::RgbaColor& actual,
                     const re::volume::RgbaColor& expected, float tol) {
    EXPECT_NEAR(actual.r, expected.r, tol);
    EXPECT_NEAR(actual.g, expected.g, tol);
    EXPECT_NEAR(actual.b, expected.b, tol);
    EXPECT_NEAR(actual.a, expected.a, tol);
}

} // namespace

TEST(T16TransferFunctionGate, DefaultRampIsPinnedBlackToWhite) {
    // Default ctor must produce exactly the pinned degenerate ramp
    // vec4(0,0,0,0) at value 0 -> vec4(1,1,1,1) at value 1 (2 control points).
    const re::volume::TransferFunction tf;
    ASSERT_EQ(tf.size(), 2u) << "default ramp has exactly 2 pinned points";
    ASSERT_EQ(tf.controlPoints().size(), 2u);
    expectColorNear(tf.controlPoints()[0].color, {0.0f, 0.0f, 0.0f, 0.0f}, 0.0f);
    expectColorNear(tf.controlPoints()[1].color, {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f);
    EXPECT_FLOAT_EQ(tf.controlPoints()[0].value, 0.0f);
    EXPECT_FLOAT_EQ(tf.controlPoints()[1].value, 1.0f);
}

TEST(T16TransferFunctionGate, DefaultSampleAtEndpointsAndMidpoint) {
    // Analytic linear ramp: sample(0)=0, sample(1)=1, sample(0.5)=0.5 on every channel.
    const re::volume::TransferFunction tf;
    expectColorNear(tf.sample(0.0f), {0.0f, 0.0f, 0.0f, 0.0f}, kTol);
    expectColorNear(tf.sample(0.5f), {0.5f, 0.5f, 0.5f, 0.5f}, kTol);
    expectColorNear(tf.sample(1.0f), {1.0f, 1.0f, 1.0f, 1.0f}, kTol);
}

TEST(T16TransferFunctionGate, SampleClampsOutsideRange) {
    // Outside [0,1] the nearest endpoint is returned (clamping, not extrapolation).
    const re::volume::TransferFunction tf;
    expectColorNear(tf.sample(-1.0f), {0.0f, 0.0f, 0.0f, 0.0f}, 0.0f);
    expectColorNear(tf.sample(2.0f), {1.0f, 1.0f, 1.0f, 1.0f}, 0.0f);
}

TEST(T16TransferFunctionGate, EmptyControlPointsReturnsTransparentBlack) {
    // Defensive: an explicitly empty vector must not UB on front()/back().
    re::volume::TransferFunction empty(std::vector<re::volume::TransferFunction::ControlPoint>{});
    ASSERT_EQ(empty.size(), 0u);
    expectColorNear(empty.sample(0.0f), {0.0f, 0.0f, 0.0f, 0.0f}, 0.0f);
    expectColorNear(empty.sample(0.5f), {0.0f, 0.0f, 0.0f, 0.0f}, 0.0f);
    expectColorNear(empty.sample(1.0f), {0.0f, 0.0f, 0.0f, 0.0f}, 0.0f);
}

TEST(T16TransferFunctionGate, ToByteClampedAtBoundaries) {
    // toByte must clamp before float->int conversion — out-of-range values are
    // defined and map to the nearest endpoint byte.
    EXPECT_EQ(re::app::detail::toByteClamped(1.5f), 255u) << "1.5 clamped to 1.0 -> 255";
    EXPECT_EQ(re::app::detail::toByteClamped(-0.2f), 0u) << "-0.2 clamped to 0.0 -> 0";
    EXPECT_EQ(re::app::detail::toByteClamped(0.0f), 0u) << "0.0 -> 0";
    EXPECT_EQ(re::app::detail::toByteClamped(1.0f), 255u) << "1.0 -> 255";
    // Mid-gray 0.5 -> round(0.5*255)=128 (explainable midpoint byte).
    EXPECT_EQ(re::app::detail::toByteClamped(0.5f), 128u) << "0.5 -> 128";
    // Slightly above 1 still clamps to 255, slightly below 0 to 0.
    EXPECT_EQ(re::app::detail::toByteClamped(1.001f), 255u);
    EXPECT_EQ(re::app::detail::toByteClamped(-0.001f), 0u);
}

} // namespace re::tests
