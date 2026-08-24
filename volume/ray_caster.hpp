#pragma once

// volume/ray_caster.hpp — ray/AABB intersection, sampling-step computation and
// front-to-back ray-cast compositing math (SPEC §3 "volume/ pure math",
// FR-vol.2/FR-vol.3).
//
// volume/ is GL-free pure math, headless-testable: these functions compute the
// closed-form quantities a volume ray caster needs and touch no GL:
//   - intersectRayAabb: the parametric entry/exit of a ray through an
//     axis-aligned box (slab method, FR-vol.3);
//   - computeRaySampleSteps: the analytic step positions along that segment
//     (FR-vol.3);
//   - compositeFrontToBack: accumulating a (color, alpha) sample sequence
//     nearest-first with the closed-form alpha-blend result (FR-vol.2).
//
// Compositing convention (FR-vol.2): *premultiplied alpha*. Each sample's RGB
// is weighted by its alpha inside the accumulator; the returned color is the
// premultiplied result. Divide by `.a` for the straight color (undefined for
// an all-transparent sequence, which yields (0,0,0,0)).

#include <cstddef>
#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <span>
#include <vector>

#include "volume/color.hpp"

namespace re::volume {

/// An axis-aligned box in world space (`min <= max` component-wise).
struct Aabb {
    glm::vec3 min{0.0f}; ///< Minimum corner (component-wise).
    glm::vec3 max{1.0f}; ///< Maximum corner (component-wise).
};

/// A ray: an origin and a direction. The direction may be any non-zero
/// vector — the slab math is parametric, so scaling `direction` scales the t
/// values inversely (unit length gives t in world units).
struct Ray {
    glm::vec3 origin{0.0f};                 ///< Ray origin.
    glm::vec3 direction{0.0f, 0.0f, -1.0f}; ///< Ray direction.
};

/// Closed-form ray/AABB intersection (FR-vol.3, slab method).
///
/// On a hit, `tEntry`/`tExit` are the parametric distances at which the ray
/// enters and leaves the box (`tEntry == 0` when the origin is inside the
/// box). Returns false when the ray misses the box, or when the box lies
/// entirely behind the origin (`tExit < 0`). A ray parallel to a slab axis is
/// a hit only if its origin lies inside that slab.
bool intersectRayAabb(const Ray& ray, const Aabb& aabb, float& tEntry,
                      float& tExit) noexcept;

/// The analytic sampling steps of a ray segment inside an AABB (FR-vol.3).
///
/// The ray/box segment [tEntry, tExit] is sampled every `stepLength` units of
/// t at the *centers* of the steps:
///
///   count = floor((tExit - tEntry) / stepLength);
///   t[k]  = tEntry + (k + 0.5) * stepLength     for k in [0, count).
///
/// Every sample position therefore lies strictly inside the segment and the
/// spacing between consecutive positions is exactly `stepLength`. A miss
/// (or `stepLength <= 0`) yields an empty result with `tEntry == tExit == 0`.
struct RaySampleSteps {
    float tEntry{0.0f};           ///< Entry t of the segment (0 on miss).
    float tExit{0.0f};            ///< Exit t of the segment (0 on miss).
    std::vector<float> positions; ///< Step positions t[0..count), ascending.
};

/// Compute the analytic step positions of `ray` inside `aabb` (FR-vol.3; see
/// RaySampleSteps for the closed form). Precondition: `stepLength > 0`.
RaySampleSteps computeRaySampleSteps(const Ray& ray, const Aabb& aabb,
                                     float stepLength);

/// Front-to-back ray-cast compositing (FR-vol.2).
///
/// Accumulates `samples` nearest-first (the caller supplies them in front-to-
/// back order) with the front-to-back premultiplied-alpha rule
///
///   out += (1 - out.a) * (s.a * s.rgb, s.a),
///
/// whose closed form over a sequence (Ci, Ai), i = 0..n-1 is
///
///   out.a   = 1 - prod_{i} (1 - Ai)
///   out.rgb = sum_{i} [ prod_{j<i} (1 - Aj) * Ai * Ci ].
///
/// Sample colors and alpha are expected in [0, 1]; an empty sequence yields
/// the fully-transparent (0, 0, 0, 0) result.
RgbaColor compositeFrontToBack(std::span<const RgbaColor> samples) noexcept;

} // namespace re::volume
