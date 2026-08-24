#pragma once

// data/mesh.hpp — CPU triangle mesh container (SPEC §3, FR-data.1/FR-data.2).
//
// data/ is GL-free: this container stores positions and triangle indices on
// the CPU and derives two analytically-computed properties at construction:
//   - per-face normals (normalized cross product of the face's first two
//     edges), FR-data.1;
//   - the axis-aligned bounding box of all positions, FR-data.2.
//
// glm is a pure-math dependency (no GL calls), so data/ stays GL-free.

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/vec3.hpp>
#include <vector>

namespace re::data {

/// Axis-aligned bounding box of a mesh (FR-data.2).
struct Aabb {
    glm::vec3 min{0.0f}; ///< Minimum corner (component-wise).
    glm::vec3 max{0.0f}; ///< Maximum corner (component-wise).
};

/// CPU triangle mesh: positions + triangle indices + derived face normals and
/// AABB (SPEC §3 "data/ CPU containers, no GL").
///
/// Built through `fromTriangles` from pre-validated data: the io/ loaders
/// validate index ranges before constructing a Mesh, so a malformed file can
/// never yield a partially-built container (FR-io.4 "no partial state").
///
/// Face `i` spans `indices()[3*i .. 3*i+2]`; its normal is
/// `normalize(cross(p1 - p0, p2 - p0))`. A degenerate face (zero-area, both
/// edges collinear) yields the zero vector `(0,0,0)` — the normal is
/// undefined for such a face, and zero is the deterministic marker.
class Mesh {
   public:
    /// Build a mesh from `positions` and `indices` (3 indices per triangle),
    /// computing face normals and the AABB. The caller must ensure every
    /// index is `< positions.size()` and `indices.size() % 3 == 0` (io/
    /// loaders validate before calling).
    static Mesh fromTriangles(std::vector<glm::vec3> positions,
                              std::vector<std::uint32_t> indices);

    /// The vertex positions.
    const std::vector<glm::vec3>& positions() const noexcept {
        return positions_;
    }

    /// The triangle indices (3 per triangle).
    const std::vector<std::uint32_t>& indices() const noexcept {
        return indices_;
    }

    /// Per-face normals (1 per triangle, see class comment for the formula).
    const std::vector<glm::vec3>& faceNormals() const noexcept {
        return faceNormals_;
    }

    /// The axis-aligned bounding box of all positions.
    const Aabb& bounds() const noexcept {
        return bounds_;
    }

    /// Number of vertices.
    std::size_t vertexCount() const noexcept {
        return positions_.size();
    }

    /// Number of triangles (indices / 3).
    std::size_t triangleCount() const noexcept {
        return indices_.size() / 3;
    }

   private:
    /// Compute faceNormals_ from positions_ + indices_ (closed-form cross
    /// product, FR-data.1).
    void computeFaceNormals();

    /// Compute bounds_ from positions_ (FR-data.2).
    void computeBounds();

    std::vector<glm::vec3> positions_;
    std::vector<std::uint32_t> indices_;
    std::vector<glm::vec3> faceNormals_;
    Aabb bounds_;
};

} // namespace re::data