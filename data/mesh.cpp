// data/mesh.cpp — Mesh container implementation (FR-data.1/FR-data.2).

#include "data/mesh.hpp"

#include <cmath>
#include <glm/geometric.hpp>

namespace re::data {

Mesh Mesh::fromTriangles(std::vector<glm::vec3> positions,
                         std::vector<std::uint32_t> indices) {
    Mesh mesh;
    mesh.positions_ = std::move(positions);
    mesh.indices_ = std::move(indices);
    mesh.computeFaceNormals();
    mesh.computeBounds();
    return mesh;
}

void Mesh::computeFaceNormals() {
    faceNormals_.clear();
    faceNormals_.reserve(triangleCount());
    for (std::size_t t = 0; t < triangleCount(); ++t) {
        const glm::vec3& p0 = positions_[indices_[3 * t + 0]];
        const glm::vec3& p1 = positions_[indices_[3 * t + 1]];
        const glm::vec3& p2 = positions_[indices_[3 * t + 2]];
        // Closed-form face normal (FR-data.1):
        //   n = normalize(cross(p1 - p0, p2 - p0))
        const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        const float lengthSq = glm::dot(n, n);
        // A degenerate (zero-area) face has an undefined normal; mark it with
        // the zero vector deterministically instead of emitting NaN.
        faceNormals_.push_back(lengthSq > 0.0f ? n / std::sqrt(lengthSq)
                                               : glm::vec3(0.0f));
    }
}

void Mesh::computeBounds() {
    if (positions_.empty()) {
        bounds_ = Aabb{};
        return;
    }
    glm::vec3 min = positions_.front();
    glm::vec3 max = positions_.front();
    for (const glm::vec3& p : positions_) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }
    bounds_ = Aabb{min, max};
}

} // namespace re::data