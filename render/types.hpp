#pragma once

// render/types.hpp — shared render types + the IRenderer dispatch contract
// (SPEC §9 V2.3, V3.4 T5).
//
// The types every renderer shares: Camera and RenderTarget (moved here from
// mesh_renderer.hpp so renderers that need only the shared types no longer pull
// in the whole mesh renderer), the Scene dispatch variant (kept for direct
// single-item render() tests; View's heterogeneous list uses IRenderable type
// erasure instead — Scene variant will be replaced by AssetId handles in T7),
// and the pure abstract IRenderer render contract implemented by the four
// per-technique renderers (Mesh/Plane/Volume/SliceRenderer). The multi-view
// workstream (T2, SPEC §9 V2.4) dispatched scene objects to the correct renderer
// through this interface; T5 replaces it with render::View (ReView) per screen
// section + IRenderable list (render/view.hpp) and deletes ViewRenderer.
// render::ViewRect remains here as the shared window-section handle.
//
// The Scene variant holds POINTERS to the per-technique scene structs (defined
// in mesh_renderer.hpp / plane_renderer.hpp / volume_renderer.hpp /
// slice_renderer.hpp): pointer types are complete even when the pointee is
// forward-declared, so the variant can live here without pulling in the
// renderer headers (a std::variant of incomplete VALUE types would not
// compile). Scenes are owned by app/ and passed by pointer, matching the
// stateless-renderer model (SPEC §3): a render call receives all of its data
// per call and never retains the scene.
//
// render/ is GL-call-free: it draws through the core::Draw API and core/ RAII
// objects (guardrail gpu_api_ownership).

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <variant>

#include "core/framebuffer.hpp"
#include "data/result.hpp"

namespace re::render {

/// Camera: view + projection matrices plus the eye position (for lighting and
/// view-direction terms).
struct Camera {
    glm::mat4 view{1.0f};
    glm::mat4 proj{1.0f};
    glm::vec3 position{0.0f, 0.0f, 0.0f};
};

/// Offscreen render target: a color-only framebuffer plus its pixel size and
/// clear color (SPEC §3; v1 FBOs are color-only, SPEC §6 / docs/core.md). A
/// null framebuffer means the window's on-screen default framebuffer (samples,
/// T12).
struct RenderTarget {
    core::Framebuffer* framebuffer = nullptr;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 0.0f};
};

// Forward declarations of the per-technique scene structs (defined in their
// renderer headers: mesh_renderer.hpp, plane_renderer.hpp,
// volume_renderer.hpp, slice_renderer.hpp).
struct MeshScene;
struct PlaneScene;
struct VolumeScene;
struct SliceScene;

/// A scene of any of the four supported rendering techniques, held by pointer:
/// the dispatch payload of the IRenderer contract (SPEC §9 V2.3). Exactly one
/// alternative is non-null in a well-formed dispatch; a null pointer (e.g. the
/// default-constructed variant) is a valid "no scene" value that every
/// renderer rejects with a typed error (nothing to render), never a crash or
/// undefined behavior. The multi-view workstream (T2, SPEC §9 V2.4) dispatches
/// scene objects to the correct renderer by building this variant and calling
/// IRenderer::render.
using Scene = std::variant<const MeshScene*, const PlaneScene*,
                           const VolumeScene*, const SliceScene*>;

/// A window-section rectangle in GL pixel coordinates (origin bottom-left,
/// matching core::setViewport): the per-view window-section handle the app
/// front-end shares with the engine compositor (SPEC §9 V2.4). `x`/`y` are the
/// rectangle's bottom-left corner in pixels from the window's left/bottom;
/// `width`/`height` are the rectangle's size. The multi-view gate (V2 T2)
/// pins two rects of a 1280x480 window: View A = (0, 0, 640, 480) and
/// View B = (640, 0, 640, 480).
struct ViewRect {
    int x{0};      ///< Left edge, in pixels from the window's left.
    int y{0};      ///< Bottom edge, in pixels from the window's bottom.
    int width{0};  ///< Width in pixels.
    int height{0}; ///< Height in pixels.
};

/// A plane used to clip a mesh, defined in world space by a unit normal and a
/// point on the plane. The kept side is the half-space `dot(normal, p - point)
/// >= 0`; the cross-section is the set of surface points where the plane cuts
/// the mesh (all lying on the plane, FR-render.4). Lives here (not in
/// slice_renderer.hpp) so `render::View` (`ReView`) can own
/// `optional<ClipPlane>` (`2D` when present, `3D` when `nullopt`) without
/// depending on `SliceRenderer` (SRP via composition, SPEC §3.2 V3.4 T5).
struct ClipPlane {
    glm::vec3 normal{0.0f, 0.0f, 1.0f}; ///< Unit plane normal (world space).
    glm::vec3 point{0.0f, 0.0f, 0.0f};  ///< A point on the plane (world space).
};

/// Pure abstract renderer contract (SPEC §9 V2.3): the single narrow dispatch
/// method implemented by every per-technique renderer (MeshRenderer,
/// PlaneRenderer, VolumeRenderer, SliceRenderer). The renderer validates that
/// `scene` holds its own scene type and renders it into `target` from
/// `camera`; a scene of a different technique is rejected with a typed error
/// (SPEC §5, no exceptions), never a crash or undefined behavior.
class IRenderer {
   public:
    virtual ~IRenderer() = default;

    /// Render the scene held by `scene` into `target` from `camera`. On
    /// success the target framebuffer is left bound (so tests can read it
    /// back). Returns a typed error if the scene holds a different technique
    /// or the render cannot be issued (SPEC §5).
    virtual data::Result<void> render(const Scene& scene, const Camera& camera,
                                      const RenderTarget& target) = 0;
};

} // namespace re::render