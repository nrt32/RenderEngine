#pragma once

// render/view.hpp — View (ReView) per screen section (SPEC §3.2 V3.4 T5).
//
// One ViewTarget{Texture2D+Framebuffer} per ViewRect (rect.w×h) + Camera +
// optional<ClipPlane> (2D vs 3D) + list<IRenderable> (VolumeSlice+MeshSlice for
// 2D, Volume+Mesh for 3D). Each IRenderable is type-erased drawLayer(Camera,DrawContext&)
// — View never knows the renderer. View::render() begins its pass via the ONE
// shared prologue core::DrawContext::beginPass (bind+viewport+clear+depth-
// state+blend-off — T17 single-site rule), then iterates drawLayer without
// clearing between layers.
//
// Depth handling is per-view and opt-in (architecture review 2026-08-23): a
// view whose depthTest flag is true renders into a ViewTarget created with
// DepthMode::Enabled and its pass prologue enables the depth test (+ clears
// depth), so overlapping opaque geometry resolves by true occlusion — nearer
// fragment wins regardless of draw order. The DEFAULT stays false: color-only
// target + depth test disabled, the deterministic painter's-order
// configuration every existing analytic gate asserts against on software GL.

#include <memory>
#include <optional>
#include <vector>

#include "core/draw.hpp"
#include "core/framebuffer.hpp"
#include "data/result.hpp"
#include "render/i_renderable.hpp"
#include "render/types.hpp"
#include "render/view_target.hpp"

namespace re::render {

/// ReView — one per screen section (SPEC §3.2, V3.4 T5).
///
/// Owns one ViewTarget{Texture2D+Framebuffer} sized rect.w×h, a Camera,
/// an optional ClipPlane (2D when present, 3D when nullopt), and a
/// heterogeneous list of IRenderables. Each renderer contributes a drawLayer
/// that assumes the View already bound its FBO and cleared via DrawContext.
/// The View composes layers without clearing between them; single-item
/// render::Renderer::render() keeps its own clear for direct tests.
/// The per-view `depthTest` flag opts the view into a depth-enabled target +
/// depth-tested pass; the default stays color-only + depth-off.
class View {
   public:
    /// Construct with a window-section rect (origin bottom-left, GL convention).
    explicit View(ViewRect rect, glm::vec4 clearColor = glm::vec4(0, 0, 0, 0));

    View(const View&) = delete;
    View& operator=(const View&) = delete;
    View(View&&) noexcept = default;
    View& operator=(View&&) noexcept = default;
    ~View() = default;

    // --- view state ---------------------------------------------------------

    const ViewRect& rect() const noexcept { return rect_; }
    /// Set rect (physical pixels). Next ensureTarget() will recreate ViewTarget
    /// if size changed.
    void setRect(ViewRect r) noexcept { rect_ = r; }

    const Camera& camera() const noexcept { return camera_; }
    void setCamera(const Camera& c) noexcept { camera_ = c; }

    const std::optional<ClipPlane>& clipPlane() const noexcept { return clipPlane_; }
    void setClipPlane(std::optional<ClipPlane> plane) noexcept { clipPlane_ = std::move(plane); }
    bool is2D() const noexcept { return clipPlane_.has_value(); }

    const glm::vec4& clearColor() const noexcept { return clearColor_; }
    void setClearColor(glm::vec4 c) noexcept { clearColor_ = c; }

    /// The per-view depth-test flag (default false = color-only painter's-
    /// order pass). When true, the next ensureTarget() creates/recreates the
    /// ViewTarget with DepthMode::Enabled (the target must physically own a
    /// depth attachment for the test to be meaningful) and render()'s prologue
    /// enables + clears the depth test instead of disabling it, so overlapping
    /// opaque items resolve by true occlusion. Transparent compositing is
    /// unaffected by this flag: the OIT capture draws only inside
    /// MeshRenderer::render, right behind its default depth-off beginPass
    /// prologue (View layers never engage the pipeline), and the composite
    /// pass issues its own explicit core::disableDepthTest().
    bool depthTest() const noexcept { return depthTest_; }
    void setDepthTest(bool enabled) noexcept { depthTest_ = enabled; }

    // --- ViewTarget ---------------------------------------------------------

    /// The owned ViewTarget (nullptr until ensureTarget() or render() creates it).
    /// @note lifetime: non-owning view of View-owned storage (the target_
    /// `optional<>` member) — valid while this View is and until the target
    /// is recreated; never delete through it.
    ViewTarget* /*borrow*/ target() noexcept { return target_ ? &*target_ : nullptr; }
    const ViewTarget* /*borrow*/ target() const noexcept { return target_ ? &*target_ : nullptr; }

    /// Ensure the ViewTarget exists and is sized rect_.width×rect_.height,
    /// and that its depth mode matches the per-view depthTest flag (a flag
    /// flip recreates the target with/without its depth attachment).
    /// Recreate only when size or depth mode changed (resize path for
    /// DPR/window resize; mode path for setDepthTest).
    data::Result<void> ensureTarget();

    // --- IRenderable list ---------------------------------------------------

    /// Add a type-erased renderable (View never knows the renderer).
    void addRenderable(std::unique_ptr<IRenderable> item) { items_.push_back(std::move(item)); }

    /// Convenience: type-erased add for a typed scene + renderer that exposes
    /// drawLayer(SceneT,Camera,DrawContext&). Enforces Renderable at compile
    /// time. The renderer reference is SHARED (T13): the item co-owns the
    /// renderer, so a stored View can never outlive the renderer it draws
    /// with (no declaration-order or teardown-order hazard).
    template <typename SceneT, typename RendererT>
    void addItem(SceneT scene, std::shared_ptr<RendererT> renderer) {
        struct Impl final : public IRenderable {
            SceneT scene_;
            std::shared_ptr<RendererT> renderer_;
            Impl(SceneT s, std::shared_ptr<RendererT> r)
                : scene_(std::move(s)), renderer_(std::move(r)) {}
            data::Result<void> drawLayer(const Camera& cam, core::DrawContext& ctx) override {
                return renderer_->drawLayer(scene_, cam, ctx);
            }
        };
        addRenderable(std::make_unique<Impl>(std::move(scene), std::move(renderer)));
    }

    std::size_t itemCount() const noexcept { return items_.size(); }
    void clearItems() noexcept { items_.clear(); }

    // --- render / blit ------------------------------------------------------

    /// Render all items into the View's own FBO. Assumes ensureTarget() already
    /// succeeded; begins the pass through the shared core::DrawContext::beginPass
    /// prologue (binds the FBO, sets the viewport, clears to clearColor, then
    /// sets the depth state — enabled + depth-cleared iff this view's
    /// depthTest flag is true, disabled otherwise — and disables blending),
    /// then calls each item's drawLayer(camera, ctx) without clearing between
    /// layers. Returns typed error on target failure or any layer failure.
    data::Result<void> render(core::DrawContext& ctx);

    /// Convenience: ensureTarget() then render().
    data::Result<void> renderWithEnsure(core::DrawContext& ctx) {
        auto e = ensureTarget();
        if (e.failed()) return e;
        return render(ctx);
    }

    /// Blit the View's FBO into its pinned window rect on destination
    /// (nullptr = the window's default framebuffer 0) via core::blit. The copy
    /// is GL_NEAREST, 1:1 when target size == rect size (the pinned gate).
    /// @note lifetime: `destination` is borrowed for the DURATION OF THIS CALL
    /// only (structurally guaranteed — the blit consumes it synchronously and
    /// retains nothing); owned by the caller (window default FB or a
    /// caller-held ViewTarget).
    data::Result<void> blitTo(core::Framebuffer* /*borrow*/ destination) const;

    /// Helpers for multi-view composition: render then blit.
    data::Result<void> renderAndBlit(core::DrawContext& ctx, core::Framebuffer* destination) {
        auto r = renderWithEnsure(ctx);
        if (r.failed()) return r;
        return blitTo(destination);
    }

   private:
    ViewRect rect_{0, 0, 640, 480};
    Camera camera_{};
    std::optional<ClipPlane> clipPlane_{std::nullopt};
    glm::vec4 clearColor_{0, 0, 0, 0};
    bool depthTest_{false};
    std::optional<ViewTarget> target_{std::nullopt};
    std::vector<std::unique_ptr<IRenderable>> items_{};
};

/// Alias for grep distinctness where both scene::View and render::View are in scope (broker ACL).
using ReView = View;

} // namespace re::render
