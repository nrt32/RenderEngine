#pragma once

// render/view_renderer.hpp — ViewRenderer: multi-view window compositor
// (SPEC §9 V2.4, Model B: per-view FBO + engine blit).
//
// The engine-side half of the multi-view workstream. The front-end (app/)
// shares per-view window-section handles (render::ViewRect) and abstract
// scene objects (render::View, holding a Scene dispatch variant); the engine
// dispatches each view's scene to the IRenderer registered for its technique
// (SPEC §9 V2.3), renders it into the view's OWN core::Framebuffer (owned by
// this compositor), then blits each FBO into its pinned window rect via
// core::blit — the new core/ wrapper around glBlitFramebuffer (guardrail
// gpu_api_ownership: the raw GL call lives under core/). No app-side viewport
// blending: the app performs no textured-quad present pass and no per-view
// compositing; it only provides the views and their rects.
//
// The compositor drives SceneView/MPRView composition (SPEC §9): the MPR
// sample composes its four 640x480 views (three PlaneScene slice views + one
// MeshScene 3D view) through this class instead of an app-side present pass
// (V2 T2).

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <vector>

#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/result.hpp"
#include "render/types.hpp"

namespace re::render {

/// The four scene techniques of the `Scene` dispatch variant
/// (render/types.hpp, SPEC §9 V2.3). The variant's alternative index equals
/// the enumerator value (verified by the static_assert below), so
/// `static_cast<SceneKind>(scene.index())` is the technique a view's scene
/// holds. ViewRenderer registers one IRenderer per kind and dispatches each
/// view's scene to the renderer of its kind.
enum class SceneKind {
    Mesh = 0,   ///< the `const MeshScene*` alternative
    Plane = 1,  ///< the `const PlaneScene*` alternative
    Volume = 2, ///< the `const VolumeScene*` alternative
    Slice = 3,  ///< the `const SliceScene*` alternative
};

static_assert(static_cast<std::size_t>(SceneKind::Slice) + 1u ==
                  std::variant_size_v<Scene>,
              "SceneKind must mirror the Scene variant's alternatives");
// The variant alternative at index k must be exactly the scene type SceneKind
// k dispatches — a reordered Scene variant would silently mis-dispatch views to
// the wrong renderer, so every alternative is pinned by its own static_assert
// (not just the count).
static_assert(
    std::is_same_v<std::variant_alternative_t<0, Scene>, const MeshScene*>,
    "Scene alternative 0 must be const MeshScene* (SceneKind::Mesh)");
static_assert(
    std::is_same_v<std::variant_alternative_t<1, Scene>, const PlaneScene*>,
    "Scene alternative 1 must be const PlaneScene* (SceneKind::Plane)");
static_assert(
    std::is_same_v<std::variant_alternative_t<2, Scene>, const VolumeScene*>,
    "Scene alternative 2 must be const VolumeScene* "
    "(SceneKind::Volume)");
static_assert(
    std::is_same_v<std::variant_alternative_t<3, Scene>, const SliceScene*>,
    "Scene alternative 3 must be const SliceScene* (SceneKind::Slice)");

/// Multi-view window compositor (SPEC §9 V2.4, "Model B: per-view FBO +
/// engine blit").
///
/// Owns one color-only core::Framebuffer (with its color-attachment texture)
/// per view, all `viewWidth` x `viewHeight` (the per-view render resolution).
/// `renderViews()` dispatches each view's Scene through the IRenderer
/// registered for its SceneKind (SPEC §9 V2.3), renders it into the view's own
/// FBO, and leaves that FBO bound; `present()` then blits each FBO into the
/// view's pinned window ViewRect on the destination framebuffer (nullptr = the
/// window's default framebuffer) through core::blit — the engine performs the
/// whole present, so the app never blends viewports itself.
///
/// Typed errors (SPEC §5, no exceptions): a view-count mismatch, an FBO
/// creation failure, a view whose technique has no registered renderer, a
/// present() call before the first renderViews (code 3), a render failure, or
/// a blit failure all surface as data::Result errors.
class ViewRenderer {
   public:
    /// Construct a compositor for `viewCount` views, each rendered into its
    /// own `viewWidth` x `viewHeight` color FBO (created lazily on the first
    /// render, which therefore requires a current GL context).
    ViewRenderer(std::size_t viewCount, std::uint32_t viewWidth,
                 std::uint32_t viewHeight);

    /// The number of views this compositor manages.
    std::size_t viewCount() const noexcept {
        return viewCount_;
    }

    /// The per-view render resolution.
    std::uint32_t viewWidth() const noexcept {
        return viewWidth_;
    }
    std::uint32_t viewHeight() const noexcept {
        return viewHeight_;
    }

    /// Register the IRenderer that renders scenes of technique `kind`
    /// (dispatched through the Scene variant, SPEC §9 V2.3). A view whose
    /// scene holds a technique with no registered renderer is rejected with a
    /// typed error. May be called any time before the first render of such a
    /// view.
    void setRenderer(SceneKind kind, IRenderer* renderer) noexcept;

    /// Render every view's scene into its own per-view FBO (dispatch through
    /// the registered IRenderer). The last-rendered FBO is left bound (so
    /// tests can read each view's FBO back after binding it). Returns a typed
    /// error if the per-view FBOs cannot be created, `views.size()` differs
    /// from the constructed view count, a scene's technique has no registered
    /// renderer, or a render fails.
    data::Result<void> renderViews(const std::vector<View>& views);

    /// Blit every view's FBO into its pinned ViewRect on `destination`
    /// (nullptr = the window's default framebuffer). Engine-side blit only
    /// (core::blit; guardrail gpu_api_ownership) — the app performs no
    /// viewport blending. Returns a typed error (code 3) if the per-view FBOs
    /// are not yet created (call renderViews first), code 1 if `views.size()`
    /// differs from the constructed view count, or a blit error if one cannot
    /// be issued.
    data::Result<void> present(const std::vector<View>& views,
                               core::Framebuffer* destination);

    /// Render every view into its own FBO, then blit each FBO into its pinned
    /// window rect on `destination` (nullptr = the window's default
    /// framebuffer): renderViews followed by present.
    data::Result<void> render(const std::vector<View>& views,
                              core::Framebuffer* destination);

    /// The per-view color framebuffer (owned by this compositor; non-null
    /// after the first successful renderViews call).
    core::Framebuffer* viewFramebuffer(std::size_t viewIndex) noexcept;

   private:
    /// A per-view color FBO: the color-attachment texture plus the
    /// framebuffer that renders into it (the texture must stay alive for the
    /// framebuffer's lifetime).
    struct ViewTarget {
        core::Texture2D color;
        core::Framebuffer framebuffer;
        ViewTarget(core::Texture2D c, core::Framebuffer f)
            : color(std::move(c)), framebuffer(std::move(f)) {}
    };

    /// Create the per-view FBOs (once). Returns a typed error on any GL
    /// creation/attachment failure.
    data::Result<void> ensureTargets();

    std::size_t viewCount_;
    std::uint32_t viewWidth_;
    std::uint32_t viewHeight_;
    bool targetsReady_{false};

    // Renderer per scene technique, indexed by SceneKind (nullptr = not
    // registered; a view of that technique is rejected with a typed error).
    std::array<IRenderer*, 4> renderers_{nullptr, nullptr, nullptr, nullptr};
    std::vector<std::optional<ViewTarget>> viewTargets_;
};

} // namespace re::render