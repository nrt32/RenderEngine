// volume/ray_caster.cpp — ray/AABB intersection, sampling steps and
// front-to-back compositing implementation (FR-vol.2/FR-vol.3).

#include "volume/ray_caster.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace re::volume {

bool intersectRayAabb(const Ray& ray, const Aabb& aabb, float& tEntry,
                      float& tExit) noexcept {
    // Slab method: on each axis the ray/box entry and exit parameters are
    // t = (plane - origin) / direction; the box intersection is the overlap
    // of the three axis intervals. A direction component near zero means the
    // ray is parallel to that slab: it is a hit on that axis only if the
    // origin lies inside the slab.
    constexpr float kParallelEps = 1e-7f;
    float tNear = -std::numeric_limits<float>::infinity();
    float tFar = std::numeric_limits<float>::infinity();

    for (int axis = 0; axis < 3; ++axis) {
        const float dir = ray.direction[axis];
        const float orig = ray.origin[axis];
        if (std::abs(dir) < kParallelEps) {
            if (orig < aabb.min[axis] || orig > aabb.max[axis]) {
                return false; // parallel to the slab and outside it: no hit
            }
            continue;
        }
        float t1 = (aabb.min[axis] - orig) / dir;
        float t2 = (aabb.max[axis] - orig) / dir;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tNear = std::max(tNear, t1);
        tFar = std::min(tFar, t2);
        if (tNear > tFar) {
            return false;
        }
    }

    if (tFar < 0.0f) {
        return false; // the box lies entirely behind the ray origin
    }
    tEntry = std::max(tNear, 0.0f); // origin inside the box -> entry at 0
    tExit = tFar;
    return true;
}

RaySampleSteps computeRaySampleSteps(const Ray& ray, const Aabb& aabb,
                                     float stepLength) {
    RaySampleSteps steps;
    if (stepLength <= 0.0f) {
        return steps; // precondition: stepLength > 0 (empty result on misuse)
    }

    float tEntry = 0.0f;
    float tExit = 0.0f;
    if (!intersectRayAabb(ray, aabb, tEntry, tExit)) {
        return steps;
    }
    steps.tEntry = tEntry;
    steps.tExit = tExit;

    // Closed form (FR-vol.3): count = floor(span / stepLength) samples at the
    // centers of the steps, t[k] = tEntry + (k + 0.5) * stepLength. Every
    // position lies strictly inside (tEntry, tExit).
    const float span = tExit - tEntry;
    if (span <= 0.0f) {
        return steps;
    }
    const int count = static_cast<int>(std::floor(span / stepLength));
    steps.positions.reserve(static_cast<std::size_t>(count));
    for (int k = 0; k < count; ++k) {
        steps.positions.push_back(tEntry +
                                  (static_cast<float>(k) + 0.5f) * stepLength);
    }
    return steps;
}

RgbaColor compositeFrontToBack(std::span<const RgbaColor> samples) noexcept {
    // Front-to-back premultiplied-alpha accumulation (FR-vol.2):
    //   out += (1 - out.a) * (s.a * s.rgb, s.a)
    // The accumulator starts fully transparent; the loop order matches the
    // caller's front-to-back sample order.
    RgbaColor out{};
    for (const RgbaColor& s : samples) {
        const float opacity = 1.0f - out.a; // remaining transparency
        const float weighted = opacity * s.a;
        out.r += weighted * s.r;
        out.g += weighted * s.g;
        out.b += weighted * s.b;
        out.a += weighted;
    }
    return out;
}

} // namespace re::volume
