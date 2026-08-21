#pragma once

// render/slice_renderer.hpp — SliceRenderer: geometry-shader plane clip of a
// mesh (SPEC §3, FR-render.4).
//
// A mesh-family technique: the geometry shader clips each triangle against a
// plane in world space and emits the clipped mesh (the kept side) for
// rendering, plus the on-plane cross-section polygon. It reuses the shared
// MeshGeometry (mesh geometry handling) and the IMaterial interface, exactly
// like MeshRenderer, and does NOT use OIT in v1 (SPEC §3 "Slicing is geometry,
// not compositing").
//
// Stateless rendering: render()/captureCrossSection() receive all of their data
// per call; the renderer owns only GL resources (its cached clip shader
// program, its transform-feedback capture program, and the GPU geometries of
// the meshes it has drawn). One mesh can be drawn by several views without
// duplication.
//
// render/ is GL-call-free: it draws through the core::Draw API and core/ RAII
// objects, and captures cross-section vertices through core::TransformFeedback
// (guardrail gpu_api_ownership). captureCrossSection() is a test-consumed
// readback path (guardrail no_production_readback): the render path never reads
// back from the GPU.

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/shader_program.hpp"
#include "core/transform_feedback.hpp"
#include "core/vertex_buffer.hpp"
#include "data/mesh.hpp"
#include "data/result.hpp"
#include "render/mesh_geometry.hpp"
#include "render/mesh_renderer.hpp"

namespace re::render {

/// A plane used to clip a mesh, defined in world space by a unit normal and a
/// point on the plane. The kept side is the half-space `dot(normal, p - point)
/// >= 0`; the cross-section is the set of surface points where the plane cuts
/// the mesh (all lying on the plane, FR-render.4).
struct ClipPlane {
    glm::vec3 normal{0.0f, 0.0f, 1.0f}; ///< Unit plane normal (world space).
    glm::vec3 point{0.0f, 0.0f, 0.0f};  ///< A point on the plane (world space).
};

/// A scene of mesh instances to clip and slice (reuses MeshInstance from
/// MeshRenderer; CPU-side, app/ builds these).
struct SliceScene {
    std::vector<MeshInstance> meshes;
};

/// Stateless geometry-shader plane-clip renderer (SPEC §3).
///
/// Owns only GL resources: the cached clip shader program (vertex + geometry +
/// fragment), the cached transform-feedback capture program, a transform
/// feedback object, and a geometry cache keyed by mesh pointer (shared with the
/// mesh-family renderers, so a data::Mesh is uploaded to the GPU once).
class SliceRenderer {
   public:
    /// Construct with no dependencies (slicing does not use OIT in v1).
    SliceRenderer() = default;

    SliceRenderer(const SliceRenderer&) = delete;
    SliceRenderer& operator=(const SliceRenderer&) = delete;

    /// Render the meshes of `scene` clipped against `plane` into `target` from
    /// `camera`. Each mesh is clipped to the kept side
    /// (`dot(normal, p - point) >= 0`); the emitted clipped-mesh triangles are
    /// shaded with each instance's material (deterministic v1 flat lighting,
    /// see docs/render.md). On success the target framebuffer is left bound (so
    /// tests can read it back). Returns a typed error if the clip shader fails
    /// to build or a draw cannot be issued.
    data::Result<void> render(const SliceScene& scene, const Camera& camera,
                              const ClipPlane& plane,
                              const RenderTarget& target);

    /// Capture the on-plane cross-section vertices emitted by the geometry
    /// shader for `scene` clipped against `plane` into `out` (world-space
    /// positions). This is a test-consumed readback path (guardrail
    /// no_production_readback) used by the FR-render.4 gate to assert every
    /// emitted cross-section vertex lies on the clip plane. Returns a typed
    /// error if the capture program fails to build or the capture cannot be
    /// issued.
    data::Result<void> captureCrossSection(const SliceScene& scene,
                                           const ClipPlane& plane,
                                           std::vector<glm::vec3>& out);

   private:
    /// Build (and cache) the clip shader program, returning a pointer to the
    /// cached program (non-null on success).
    data::Result<core::ShaderProgram*> clipProgram();

    /// Build (and cache) the transform-feedback capture program, returning a
    /// pointer to the cached program (non-null on success).
    data::Result<core::ShaderProgram*> captureProgram();

    /// Ensure the shared transform-feedback object and capture buffer exist,
    /// returning a pointer to the transform-feedback object.
    data::Result<core::TransformFeedback*> captureFeedback();

    /// Upload the GPU geometry for `mesh` if not already cached, returning a
    /// pointer to it (shared with MeshRenderer; reuses MeshGeometry).
    data::Result<MeshGeometry*> geometryFor(const data::Mesh& mesh);

    std::optional<core::ShaderProgram> clipProgram_;
    std::optional<core::ShaderProgram> captureProgram_;
    std::optional<core::TransformFeedback> captureFeedback_;
    std::optional<core::VertexBuffer> captureBuffer_;
    std::unordered_map<const data::Mesh*, MeshGeometry> geometries_;
};

} // namespace re::render
