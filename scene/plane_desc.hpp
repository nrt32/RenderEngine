#pragma once

// scene/plane_desc.hpp — abstract plane equation for scene value library (SPEC §3.1).
//
// Plane lives on View for 2D, not on per-item. Space::World vs VoxelIndex
// selects the coordinate frame; conversion to world ClipPlane is broker's job.
// Pure value type, GL-free, RE-free.

#include <cstdint>

#include <glm/vec3.hpp>

namespace re::scene {

/// Coordinate space of a plane point.
enum class Space : uint8_t {
    World = 0,      ///< point/normal in world space.
    VoxelIndex = 1, ///< point in voxel-index space, normal in world.
};

/// Abstract plane equation { normal, point } + space tag.
///
/// Normal is stored normalized. Generation bumps when plane mutates (per-field
/// gen per SPEC §10.4).
struct PlaneDesc {
    /// Plane normal (world or voxel-index interpretation per space).
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    /// A point on the plane.
    glm::vec3 point{0.0f, 0.0f, 0.0f};
    /// Coordinate space of point (and interpretation of normal).
    Space space{Space::World};
    /// Per-field generation — bumped by mutators.
    uint64_t generation{0};

    /// Set normal (normalized internally) and bump generation.
    void setNormal(glm::vec3 n) noexcept;
    /// Set point and bump generation.
    void setPoint(glm::vec3 p) noexcept;
    /// Set space and bump generation.
    void setSpace(Space s) noexcept;

    bool operator==(const PlaneDesc& o) const noexcept {
        return normal == o.normal && point == o.point && space == o.space;
    }
    bool operator!=(const PlaneDesc& o) const noexcept { return !(*this == o); }
};

inline void PlaneDesc::setNormal(glm::vec3 n) noexcept {
    float len = glm::length(n);
    if (len > 1e-6f) {
        normal = n / len;
    } else {
        normal = glm::vec3{0.0f, 0.0f, 1.0f};
    }
    ++generation;
}

inline void PlaneDesc::setPoint(glm::vec3 p) noexcept {
    point = p;
    ++generation;
}

inline void PlaneDesc::setSpace(Space s) noexcept {
    if (space != s) {
        space = s;
        ++generation;
    }
}

} // namespace re::scene
