#pragma once

// render/mesh_geometry.hpp — GPU-resident mesh geometry (SPEC §3).
//
// Uploads a data::Mesh (positions + triangle indices) into core/ RAII GL
// buffers (VBO/EBO/VAO), computing per-vertex normals from the mesh's face
// normals. This is the shared "mesh geometry handling" used by every
// mesh-family renderer (MeshRenderer now, SliceRenderer in T11) so a mesh is
// uploaded to the GPU once and reused.
//
// render/ is GL-call-free: MeshGeometry owns core/ RAII objects and issues
// draws through the core::Draw API (guardrail gpu_api_ownership).

#include <cstddef>
#include <cstdint>
#include <glm/vec3.hpp>
#include <vector>

#include "core/element_buffer.hpp"
#include "core/vertex_array.hpp"
#include "core/vertex_buffer.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"

namespace re::render {

/// GPU-resident geometry of a single data::Mesh: interleaved
/// (position, normal) vertices + 32-bit triangle indices, held in a VAO/VBO/
/// EBO. Movable but not copyable (its members are movable-only GL objects).
class MeshGeometry {
   public:
    /// Build GPU buffers from `mesh`. Returns an error if no GL context is
    /// current.
    static data::Result<MeshGeometry> create(const data::Mesh& mesh);

    MeshGeometry(const MeshGeometry&) = delete;
    MeshGeometry& operator=(const MeshGeometry&) = delete;

    MeshGeometry(MeshGeometry&& other) noexcept = default;
    MeshGeometry& operator=(MeshGeometry&& other) noexcept = default;

    /// Issue an indexed triangle draw of this mesh. The program must already
    /// be installed (ShaderProgram::use). Returns an error if the draw call
    /// cannot be issued (no GL context).
    data::Result<void> draw() const;

    /// Number of triangles.
    std::size_t triangleCount() const noexcept {
        return indexCount_ / 3u;
    }

    /// Number of vertices.
    std::size_t vertexCount() const noexcept {
        return vertexCount_;
    }

   private:
    /// Build per-vertex normals from `mesh`: each vertex takes the
    /// area-weighted average of the normalized face normals of its incident
    /// faces (a smooth-shading approximation; exact for a flat face).
    static std::vector<glm::vec3> computeVertexNormals(const data::Mesh& mesh);

    /// Construct from already-created GL objects (used by create()).
    MeshGeometry(core::VertexArray vao, core::VertexBuffer vbo,
                 core::ElementBuffer ebo, std::size_t indexCount,
                 std::size_t vertexCount) noexcept;

    core::VertexArray vao_;
    core::VertexBuffer vbo_;
    core::ElementBuffer ebo_;
    std::size_t indexCount_{0u};
    std::size_t vertexCount_{0u};
};

} // namespace re::render
