#pragma once

// render/plane_renderer.hpp — PlaneRenderer: textured quads/planes (SPEC §3,
// FR-render.5).
//
// Sole owner of every textured-plane draw (V3.4b T12 audit): all textured-plane
// displays — the plane sample and the MPR slice views — reach the GPU ONLY
// through this renderer (drawLayer(PlaneScene, Camera, DrawContext&) inside a
// ReView's IRenderable list, or render() for direct single-target tests). The
// app side sends only scene::PlaneObject{image asset ref, transform,
// presentation}; broker/ mediates that value into the render::PlaneInstance
// consumed here. app/ never names PlaneGeometry and never parses quad
// corners/UVs into vertex buffers — the unit-quad VAO is built and owned by
// quadGeometry() below, PlaneGeometry::unitQuadXY() stays a render/-internal
// detail (reached through broker, not re-parsed by callers), and the
// data::Image → core::Texture2D upload stays inside textureFor()
// (imageToRgba8's CPU row-flip feeds that GPU upload; it is NOT an app-side
// quad path).
//
// Stateless rendering: PlaneRenderer::render(scene, camera, target) receives
// all of its data per call; the renderer owns only GL resources (its cached
// textured-plane shader program, one shared unit-quad VAO/VBO, and the GPU
// textures it has uploaded, keyed by a weak observer of the shared CPU image
// — see texture_cache_key.hpp). The same image is
// uploaded to the GPU once and reused across plane instances and views — this
// is the renderer MPRView drives for its Transverse/Coronal/Sagittal slice
// views (T14).
//
// Plane geometry is defined per-instance by PlaneGeometry (four world-space
// corners + per-corner UVs + a precomputed normal) and transformed by the
// instance's model matrix, so the renderer handles planes of any orientation.
// UV mapping convention (analytic, verified by the T8 gate): the plane's UV
// space maps the source image exactly once across the quad, with (u,v) = (0,0)
// at corner0 and (1,1) at corner2; the image's top-left pixel appears at the
// quad's top-left when viewed from the side the normal points toward (see
// imageToRgba8). The model matrix maps the geometry's corners into world
// space (applied after the affine corner-box map the renderer builds).
//
// render/ is GL-call-free: it draws through the core::Draw API and core/ RAII
// objects (guardrail gpu_api_ownership).

#include <array>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/draw.hpp"
#include "core/element_buffer.hpp"
#include "core/framebuffer.hpp"
#include "core/shader_program.hpp"
#include "core/texture2d.hpp"
#include "core/vertex_array.hpp"
#include "core/vertex_buffer.hpp"
#include "data/image.hpp"
#include "data/result.hpp"
#include "render/texture_cache_key.hpp"
#include "render/types.hpp" // render::Camera / render::RenderTarget

namespace re::render {

/// A planar quad in world space (before the instance's model transform):
/// four corners, per-corner texture coordinates, and the plane's unit normal.
///
/// Corner/UV binding (the plane's UV mapping, FR-render.5): corner `i` carries
/// UV `uv[i]`, so (u,v) = (0,0) at corner0 and (1,1) at corner2. The normal is
/// precomputed (analytic cross product of the corner0->corner1 and
/// corner0->corner3 edges, normalized); the rendered surface is the two
/// triangles (corner0, corner1, corner2) and (corner0, corner2, corner3).
struct PlaneGeometry {
    /// The four corners in order corner0..corner3.
    std::array<glm::vec3, 4> corners;

    /// Per-corner UVs, indexed like corners.
    std::array<glm::vec2, 4> uv;

    /// Unit normal of the plane (analytic: normalized cross product of the
    /// edges corner0->corner1 and corner0->corner3).
    glm::vec3 normal{0.0f, 0.0f, 1.0f};

    /// Build the unit quad: the XY plane square [-1,1]^2 at z = 0, normal
    /// (0,0,1), with the standard UV mapping corner0=(-1,-1)->(0,0),
    /// corner1=(1,-1)->(1,0), corner2=(1,1)->(1,1), corner3=(-1,1)->(0,1).
    static PlaneGeometry unitQuadXY();
};

/// A single plane in a scene: its geometry, the image to texture it with, and
/// its model transform. The model matrix places the quad corners in world
/// space and orients the plane (applied to vertices and normals).
///
/// Produced by broker::PlaneMapper from scene::PlaneObject (V3.4b T12) — app/
/// code does not assemble instances by hand.
///
/// Ownership (T13): both references are SHARED (`shared_ptr<const T>`) — the
/// instance co-owns the mapper's shared unit quad and the caller's image, so
/// a stored scene can never outlive the bytes it draws.
struct PlaneInstance {
    /// Shared reference to the plane geometry (PlaneMapper's program-duration
    /// shared unit quad). Null is invalid: render/drawLayer return a typed
    /// error instead of dereferencing (SPEC §5).
    std::shared_ptr<const PlaneGeometry> geometry = nullptr;
    /// Shared reference to the source image (RGBA8/RGB/gray). Null is
    /// invalid: render/drawLayer return a typed error (SPEC §5).
    std::shared_ptr<const data::Image> image = nullptr;
    glm::mat4 model{1.0f};
};

/// A scene of plane instances to render (CPU-side; built by mapping
/// scene::PlaneObject values through broker::PlaneMapper).
struct PlaneScene {
    std::vector<PlaneInstance> planes;
};

/// Stateless textured-plane renderer (SPEC §3; feeds MPR, T14).
///
/// Owns only GL resources: the cached textured-plane shader program, one
/// shared unit-quad vertex array (all planes are unit quads transformed by
/// their model matrix), and a texture cache keyed by a weak observer of the
/// shared CPU image so each data::Image is uploaded to the GPU once. Textures
/// are sampled with GL_LINEAR and CLAMP_TO_EDGE (core::Texture2D defaults),
/// so a quad mapped 1:1 onto the viewport reproduces the source texels
/// exactly (FR-render.5).
class PlaneRenderer : public IRenderer {
   public:
    /// Render `scene` into `target` from `camera`. On success the target
    /// framebuffer is left bound (so tests can read it back). Returns a typed
    /// error if the shader fails to build, a texture upload fails, or a draw
    /// cannot be issued.
    data::Result<void> render(const PlaneScene& scene, const Camera& camera,
                              const RenderTarget& target);

    /// IRenderer dispatch (SPEC §9 V2.3): renders when `scene` holds a
    /// PlaneScene; returns a typed error when it holds a different technique
    /// (SPEC §5, no exceptions).
    data::Result<void> render(const Scene& scene, const Camera& camera,
                               const RenderTarget& target) override;

    /// Draw one layer into the currently-bound framebuffer (ReView's ViewTarget),
    /// assuming ReView already performed bind+viewport+clear via the same
    /// DrawContext. Does not clear — second layer must not clear away the first.
    data::Result<void> drawLayer(const PlaneScene& scene, const Camera& camera,
                                 core::DrawContext& ctx);

   private:
    /// Convert an image's pixels to RGBA8 bytes for GL upload: 4-channel
    /// images pass through, 3-channel images get alpha 255, 1-channel
    /// (grayscale) images replicate the value to RGB with alpha 255. The
    /// result is laid out like core::Texture2D expects (row 0 = bottom).
    static std::vector<std::uint8_t> imageToRgba8(const data::Image& image);

    /// Build (and cache) the textured-plane shader program, returning a
    /// pointer to the cached program (non-null on success).
    data::Result<core::ShaderProgram*> planeProgram();

    /// Ensure the shared unit-quad geometry is uploaded, returning a pointer
    /// to the cached vertex array (non-null on success).
    /// @note lifetime: non-owning view of renderer-owned storage (the
    /// quadVao_ `optional<>` member) — valid while this renderer is.
    data::Result<core::VertexArray*> quadGeometry();

    /// Ensure `image` is uploaded as a GPU texture (cached by weak OBSERVER
    /// of the shared asset — see texture_cache_key.hpp), returning a pointer
    /// to the cached texture (non-null on success).
    /// @note lifetime: non-owning view of renderer-owned storage (the
    /// textures_ map entry) — valid until this renderer evicts it or dies.
    data::Result<core::Texture2D*> textureFor(
        const std::shared_ptr<const data::Image>& image);

    /// Upload `image` to a fresh texture bound to `*out` (used by textureFor).
    data::Result<void> uploadTexture(const data::Image& image,
                                     core::Texture2D& out);

    std::optional<core::ShaderProgram> planeProgram_;
    std::optional<core::VertexArray> quadVao_;
    std::optional<core::VertexBuffer> quadVbo_;
    std::optional<core::ElementBuffer> quadEbo_; // index buffer referenced by quadVao_
    std::size_t quadIndexCount_{6u}; // two triangles
    /// GPU texture cache keyed by a weak OBSERVER of the shared CPU image
    /// (T13): an expired image's key expires with it — the cache can never
    /// serve stale GPU data for a destroyed asset, and it never keeps a CPU
    /// asset alive by itself.
    std::unordered_map<WeakAssetKey<data::Image>, core::Texture2D,
                       WeakAssetOwnerHash<data::Image>,
                       WeakAssetOwnerEq<data::Image>>
        textures_;
};

} // namespace re::render
