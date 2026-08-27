#pragma once

// data/aabb.hpp — canonical Axis-Aligned Bounding Box (VG8 single definition).
//
// VG8: single canonical Aabb type with one default (min 0 max 0). All other
// aliases (volume, scene, render::re_scene) are now type-aliases to the
// canonical definition, so the definition count is 1.
// The canonical default is min {0,0,0} max {0,0,0} (empty box); call sites
// that need a unit cube build Aabb{{0,0,0},{1,1,1}} explicitly.

#include <glm/vec3.hpp>

namespace re::data {

/// Axis-aligned bounding box (canonical, VG8).
struct Aabb {
    glm::vec3 min{0.0f}; ///< Minimum corner (component-wise).
    glm::vec3 max{0.0f}; ///< Maximum corner (component-wise).

    bool operator==(const Aabb& o) const noexcept {
        return min == o.min && max == o.max;
    }
    bool operator!=(const Aabb& o) const noexcept { return !(*this == o); }
};

} // namespace re::data
