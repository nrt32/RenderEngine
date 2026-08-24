// render/mesh_geometry.cpp — GPU-resident mesh geometry: upload CPU vertex/
// index bytes once into GL buffers and draw them by handle. Uploads go through
// the core/ RAII wrappers (render/ never issues raw GL calls), so buffer
// lifetime is tied to this object and GL state errors surface as typed
// results instead of silent no-ops.

#include "render/mesh_geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

#include "core/draw.hpp"

namespace re::render {

namespace {

/// Interleaved vertex stride: position (3 floats) + normal (3 floats).
constexpr std::size_t kStrideBytes = 6u * sizeof(float);
constexpr std::size_t kNormalOffsetBytes = 3u * sizeof(float);

} // namespace

std::vector<glm::vec3> MeshGeometry::computeVertexNormals(
    const data::Mesh& mesh) {
    std::vector<glm::vec3> normals(mesh.vertexCount(), glm::vec3(0.0f));
    std::vector<float> weights(mesh.vertexCount(), 0.0f);
    // Area-weighted average of the incident face normals per vertex.
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const std::uint32_t i0 = mesh.indices()[3 * t + 0];
        const std::uint32_t i1 = mesh.indices()[3 * t + 1];
        const std::uint32_t i2 = mesh.indices()[3 * t + 2];
        const glm::vec3 n = mesh.faceNormals()[t];
        if (glm::dot(n, n) == 0.0f) {
            continue; // degenerate face: contributes no normal.
        }
        // Area weight = magnitude of the (unnormalized) cross product. The
        // stored face normal is already normalized, so scale by the face
        // area instead of recomputing the cross product.
        const glm::vec3& p0 = mesh.positions()[i0];
        const glm::vec3& p1 = mesh.positions()[i1];
        const glm::vec3& p2 = mesh.positions()[i2];
        const float area = 0.5f * glm::length(glm::cross(p1 - p0, p2 - p0));
        normals[i0] += n * area;
        normals[i1] += n * area;
        normals[i2] += n * area;
        weights[i0] += area;
        weights[i1] += area;
        weights[i2] += area;
    }
    for (std::size_t v = 0; v < normals.size(); ++v) {
        if (weights[v] > 0.0f) {
            normals[v] = glm::normalize(normals[v]);
        }
    }
    return normals;
}

data::Result<MeshGeometry> MeshGeometry::create(const data::Mesh& mesh) {
    auto vao = core::VertexArray::create();
    if (vao.failed()) {
        return data::makeError<MeshGeometry>(vao.error().code,
                                             vao.error().message);
    }
    auto vbo = core::VertexBuffer::create();
    if (vbo.failed()) {
        return data::makeError<MeshGeometry>(vbo.error().code,
                                             vbo.error().message);
    }
    auto ebo = core::ElementBuffer::create();
    if (ebo.failed()) {
        return data::makeError<MeshGeometry>(ebo.error().code,
                                             ebo.error().message);
    }

    const std::vector<glm::vec3> normals = computeVertexNormals(mesh);
    std::vector<float> interleaved;
    interleaved.reserve(mesh.vertexCount() * 6u);
    for (std::size_t v = 0; v < mesh.vertexCount(); ++v) {
        const glm::vec3& p = mesh.positions()[v];
        const glm::vec3& n = normals[v];
        interleaved.insert(interleaved.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
    }

    vao->bind();
    vbo->bind();
    vbo->upload(interleaved.data(), interleaved.size() * sizeof(float),
                core::BufferUsage::StaticDraw);
    ebo->bind();
    ebo->upload(mesh.indices().data(), mesh.indices().size(),
                core::BufferUsage::StaticDraw);
    vao->setAttribute(0u, 3, /*normalized=*/false, kStrideBytes, 0u);
    vao->setAttribute(1u, 3, /*normalized=*/false, kStrideBytes,
                      kNormalOffsetBytes);
    vao->unbind();

    return data::makeValue<MeshGeometry>(
        MeshGeometry(std::move(*vao), std::move(*vbo), std::move(*ebo),
                     mesh.indices().size(), mesh.vertexCount()));
}

MeshGeometry::MeshGeometry(core::VertexArray vao, core::VertexBuffer vbo,
                           core::ElementBuffer ebo, std::size_t indexCount,
                           std::size_t vertexCount) noexcept
    : vao_(std::move(vao)),
      vbo_(std::move(vbo)),
      ebo_(std::move(ebo)),
      indexCount_(indexCount),
      vertexCount_(vertexCount) {}

data::Result<void> MeshGeometry::draw() const {
    return core::drawElements(vao_, indexCount_);
}

} // namespace re::render
