#pragma once

// render/types.hpp — shared render types (SPEC §9 V2.3, V3.4 T5, T14 collapse).
//
// The types every renderer shares: Camera and RenderTarget (moved here from
// mesh_renderer.hpp so renderers that need only the shared types no longer pull
// in the whole mesh renderer), ViewRect as the shared window-section handle,
// and ClipPlane for view culling. The former Scene dispatch variant
// `variant<const MeshScene*, ...>` and the pure abstract IRenderer dispatch
// contract were kept ONLY for direct single-item render() tests; they were
// deleted in T14 (collapse to the broker path) because the dispatch introduced
// a second transparent-mesh behavior that silently dropped transparent instances
// when no OIT pipeline was wired, while the broker drawLayer path draws every
// instance with blending off — a bug class made unrepresentable by keeping only
// IRenderable::drawLayer via View/REContext (T14, A9). The multi-view
// workstream (SPEC §9 V2.4) previously dispatched through IRenderer; the V3
// view redesign (T5) replaced it with render::View (ReView) per screen section
// + IRenderable list (render/view.hpp) and deleted ViewRenderer; T14 then
// removed the vestigial Scene/ IRenderer dispatch entirely so all rendering
// goes through the View compositor's out-of-band OIT capture when a pipeline
// is wired, otherwise drawLayer draws with blending off — never a silent drop.
// Scenes are now consumed as concrete `MeshScene`/`PlaneScene`/etc. via
// `drawLayer` after View's `REContext::current().beginPass` prologue, and the
// App never retains the scene beyond one call (stateless-renderer model,
// SPEC §3).
//
// render/ is GL-call-free: it draws through the core wrappers and REContext
// (guardrail gpu_api_ownership).

#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

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

/// Offscreen render target: a framebuffer plus its pixel size and clear color
/// (SPEC §3). The default configuration is color-only (v1 semantics, and
/// still the deterministic-gate default — painter's-order output that is
/// reproducible on software GL), but the framebuffer MAY carry an optional
/// depth attachment when it belongs to a ViewTarget created with
/// DepthMode::Enabled; direct renders keep the depth test off either way (the
/// per-view opt-in lives on render::View::setDepthTest). A null framebuffer
/// means the window's on-screen default framebuffer (samples, T12).
struct RenderTarget {
    /// Borrow for the DURATION OF ONE render/blit call only (structurally
    /// guaranteed: renderers use it synchronously inside
    /// concrete `render(scene,camera,target)` / `View::blitTo` and never retain
    /// it — stateless-renderer contract, SPEC §3). Null = the window's default
    /// framebuffer.
    /// @note lifetime: owned by the caller — a ViewTarget's inner framebuffer
    /// or the window's default FB; must outlive the single call that consumes
    /// this target.
    core::Framebuffer* /*borrow*/ framebuffer = nullptr;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 0.0f};
};

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

} // namespace re::render