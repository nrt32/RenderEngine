// volume/transfer_function.cpp — TransferFunction implementation (FR-vol.1).

#include "volume/transfer_function.hpp"

#include <cstddef>
#include <utility>

namespace re::volume {

TransferFunction::TransferFunction()
    : points_{
          ControlPoint{0.0f, RgbaColor{0.0f, 0.0f, 0.0f, 0.0f}},
          ControlPoint{1.0f, RgbaColor{1.0f, 1.0f, 1.0f, 1.0f}}} {}

TransferFunction::TransferFunction(std::vector<ControlPoint> points)
    : points_(std::move(points)) {}

data::Result<TransferFunction> TransferFunction::tryCreate(std::vector<ControlPoint> points) {
    if (points.empty()) {
        return data::makeError<TransferFunction>(data::ErrorDomain::VolumeIo, 1,
                                                 "TransferFunction: points must be non-empty");
    }
    for (std::size_t i = 1; i < points.size(); ++i) {
        if (!(points[i].value > points[i - 1].value)) {
            return data::makeError<TransferFunction>(
                data::ErrorDomain::VolumeIo, 2,
                "TransferFunction: control points must be strictly increasing (duplicate or unsorted at index " +
                    std::to_string(i) + ")");
        }
    }
    return data::makeValue<TransferFunction>(TransferFunction(std::move(points)));
}

RgbaColor TransferFunction::sample(float value) const noexcept {
    // Defensive: an empty control-point list would be UB on front()/back().
    // Return transparent black (the degenerate ramp's low endpoint) so a
    // default-constructed or otherwise empty transfer function is defined.
    if (points_.empty()) {
        return RgbaColor{0.0f, 0.0f, 0.0f, 0.0f};
    }
    // Below the first breakpoint (or a single-point ramp): the first endpoint
    // color.
    if (value <= points_.front().value) {
        return points_.front().color;
    }
    // Above the last breakpoint: the last endpoint color.
    if (value >= points_.back().value) {
        return points_.back().color;
    }

    // Binary search for the bracketing pair. Invariant:
    // points_[lo].value < value < points_[hi].value (the two early exits
    // above guarantee the strict bounds, and strictly-increasing point values
    // guarantee the search terminates with hi == lo + 1).
    std::size_t lo = 0;
    std::size_t hi = points_.size() - 1;
    while (hi - lo > 1) {
        const std::size_t mid = (lo + hi) / 2;
        if (points_[mid].value <= value) {
            lo = mid;
        } else {
            hi = mid;
        }
    }

    // Closed-form linear ramp on [points_[lo].value, points_[hi].value]:
    // every channel lerps with t in (0, 1).
    const ControlPoint& p0 = points_[lo];
    const ControlPoint& p1 = points_[hi];
    const float t = (value - p0.value) / (p1.value - p0.value);

    RgbaColor c;
    c.r = p0.color.r + t * (p1.color.r - p0.color.r);
    c.g = p0.color.g + t * (p1.color.g - p0.color.g);
    c.b = p0.color.b + t * (p1.color.b - p0.color.b);
    c.a = p0.color.a + t * (p1.color.a - p0.color.a);
    return c;
}

} // namespace re::volume
