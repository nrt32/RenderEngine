#pragma once

// volume/transfer_function.hpp — scalar-value -> RGBA transfer function
// (SPEC §3 "volume/ pure math", FR-vol.1).
//
// volume/ is GL-free pure math, headless-testable. A TransferFunction maps a
// scalar value (e.g. a sampled voxel intensity) to an RGBA color through a
// piecewise-linear ramp defined by control points:
//   - sampling is EXACT at each control point, and
//   - between two adjacent control points every RGBA channel interpolates
//     linearly with the fraction t = (value - v0) / (v1 - v0) (FR-vol.1),
//   - outside the control-point range the nearest endpoint color is returned
//     (clamping).

#include <cstddef>
#include <vector>

#include "data/result.hpp"
#include "volume/color.hpp"

namespace re::volume {

/// A scalar-value -> RGBA mapping defined by control points (FR-vol.1).
class TransferFunction {
   public:
    /// A breakpoint of the piecewise-linear ramp: a scalar `value` and its
    /// straight (non-premultiplied) RGBA `color`.
    struct ControlPoint {
        float value{0.0f}; ///< Scalar value at the breakpoint.
        RgbaColor color{}; ///< Straight RGBA color at the breakpoint.
    };

    /// Default-construct a valid degenerate ramp: transparent black at 0.0
    /// to opaque white at 1.0. This makes a value-initialized transfer
    /// function usable without UB — sample() linearly interpolates between
    /// these two pinned endpoints, so sample(0)=transparent black,
    /// sample(1)=opaque white, and sample(0.5) is the exact midpoint.
    TransferFunction();

    /// Build a transfer function from `points`.
    ///
    /// Precondition (caller-validated, matching the data/ container style):
    /// `points` is non-empty and sorted by strictly increasing `value` — a
    /// duplicate value would make the ramp between the two breakpoints
    /// ambiguous (the interpolation denominator would be zero). T11a asserts
    /// this and provides tryCreate() returning typed error.
    explicit TransferFunction(std::vector<ControlPoint> points);

    /// Validated factory: returns typed error if points empty or not strictly
    /// increasing (duplicate → divide-by-0 inf/NaN guard, T11a).
    static data::Result<TransferFunction> tryCreate(std::vector<ControlPoint> points);

    /// The control points, in ascending value order.
    const std::vector<ControlPoint>& controlPoints() const noexcept {
        return points_;
    }

    /// Number of control points.
    std::size_t size() const noexcept {
        return points_.size();
    }

    /// Sample the ramp at `value` (FR-vol.1): exactly the control-point color
    /// at a breakpoint, a linear RGBA interpolation between the two adjacent
    /// breakpoints in between, and the nearest endpoint color outside the
    /// control-point range.
    RgbaColor sample(float value) const noexcept;

   private:
    std::vector<ControlPoint> points_;
};

} // namespace re::volume
