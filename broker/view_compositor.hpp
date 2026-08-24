#pragma once

// broker/view_compositor.hpp — ViewCompositor (SPEC §11 V3.2b T3, V3.5 T6 full).
//
// SRP: single responsibility is own ReView -> renderAll()/presentAll(dst) via
// render dispatch (owns ReView lifetime map LayoutId->ReView). Does not poll
// SceneStore or cache CompositeKey (that's ViewSynchronizer). Handles
// persistence: ReView identity by CompositeKey{Version,LayoutId,ViewId} stable,
// ViewTarget inner FBO recreated only on rect size change (size hash includes
// physical pixels framebufferSize + contentScale). No GL calls here except via
// render::View helpers (gpu_api_ownership).

#include <memory>
#include <unordered_map>
#include <vector>

#include "broker/broker.hpp"
#include "broker/render_stack.hpp"
#include "broker/stable_key.hpp"
#include "core/framebuffer.hpp"
#include "data/result.hpp"
#include "render/mesh_renderer.hpp" // render::MeshInstance (capture payloads)
#include "render/view.hpp"
#include "scene/composite_key.hpp"
#include "scene/view.hpp"

namespace re::broker {

/// View compositor — dispatch/present side of IViewBridge (SRP via composition).
///
/// Ownership (T13): the Broker and the RenderStack are SHARED references
/// (co-owned wiring); the ReViews in `views_` are SOLELY OWNED
/// (`unique_ptr`) — accessors returning raw View pointers are documented
/// non-owning views over that storage.
///
/// Transparency contract (FR-render.2/3): when the wired stack carries an OIT
/// pipeline, the SYNCHRONIZER routes transparent mesh instances here instead
/// of into inline layers ("View layers never engage the pipeline"), and this
/// compositor runs the capture+composite stage right after each view's own
/// pass inside renderAll() — begin(), one drawTransparent per captured
/// instance, end() compositing depth-sorted premultiplied fragments over the
/// opaque result inside the same view target.
class ViewCompositor {
   public:
    explicit ViewCompositor(std::shared_ptr<Broker> broker,
                            std::shared_ptr<RenderStack> stack = nullptr)
        : broker_(std::move(broker)), stack_(std::move(stack)) {}

    // --- ReView lifetime (persistence by stable key) --------------------------
    //
    // ReView identity is broker::StableKey{version, layoutId, viewId} — the
    // single shared definition from broker/stable_key.hpp that the
    // synchronizer's per-view caches key on too, so both collaborators can no
    // longer diverge on what identifies one persistent ReView.

    /// Get existing ReView by (layoutId, viewId) under the current schema version (or nullptr).
    /// @note lifetime: non-owning view over compositor-owned `unique_ptr`
    /// storage (views_) — valid until the ReView is pruned/cleared or the
    /// compositor dies; never delete through it.
    render::View* /*borrow*/ getView(uint64_t layoutId, uint64_t viewId) noexcept;
    /// @note lifetime: same views_-owned storage borrow as the non-const
    /// getView().
    const render::View* /*borrow*/ getView(uint64_t layoutId, uint64_t viewId) const noexcept;

    /// Ensure ReView exists for appView (create if missing, otherwise return existing).
    /// Returned pointer is stable identity (same &ReView across syncs when layoutId+viewId same).
    /// @note lifetime: same views_-owned storage borrow as getView() —
    /// identity-stable across syncs while layoutId+viewId are unchanged.
    render::View* /*borrow*/ ensureView(uint64_t layoutId, const scene::View& appView);

    /// Prune ReViews for a layout to activeViewIds set (layout count/set change -> insert/erase).
    void pruneLayout(uint64_t layoutId, const std::vector<uint64_t>& activeViewIds);

    /// Total number of cached ReViews (for tests).
    size_t viewCount() const noexcept { return views_.size(); }

    /// Access all cached ReViews (for renderAll/presentAll).
    const std::unordered_map<StableKey, std::unique_ptr<render::View>>& views() const noexcept {
        return views_;
    }

    /// Dispatch already-synced ReViews to their targets (no poll). When a
    /// view carries pending transparent instances (see setTransparentItems)
    /// and the stack has an OIT pipeline, runs capture+composite after that
    /// view's pass.
    data::Result<void> renderAll();

    /// Present already-rendered ReViews via core::blit to destination.
    /// @note lifetime: `destination` is borrowed for the DURATION OF THIS
    /// CALL only (null = window default framebuffer); owned by the caller.
    data::Result<void> presentAll(core::Framebuffer* /*borrow*/ destination);

    /// Replace the pending transparent-capture payloads for one (layout,
    /// view) pair. Called by the synchronizer on every item rebuild; an empty
    /// vector clears the stage for that view. Payloads are RE-side values
    /// (handle + material + model) copied out of the mapped layers — no
    /// borrow of scene state is retained.
    void setTransparentItems(uint64_t layoutId, uint64_t viewId,
                             std::vector<render::MeshInstance> items);

    /// Pending transparent count for one (layout, view) pair (test evidence).
    std::size_t transparentCount(uint64_t layoutId, uint64_t viewId) const;

    /// For determinism: clear all cached ReViews and pending stages.
    void clear() noexcept;

   private:
    /// Run the OIT capture+composite stage for `rv`'s pending transparent
    /// instances into its own target. No-op when nothing is pending or the
    /// stack has no pipeline.
    /// @note lifetime: `rv` is a non-owning view over this compositor's
    /// views_ storage (the ReView being dispatched); consumed synchronously
    /// within the renderAll call, never retained.
    data::Result<void> captureTransparents(StableKey key,
                                           render::View* /*borrow*/ rv,
                                           core::DrawContext& ctx);

    std::shared_ptr<Broker> broker_;
    std::shared_ptr<RenderStack> stack_;
    std::unordered_map<StableKey, std::unique_ptr<render::View>> views_{};
    std::unordered_map<StableKey, std::vector<render::MeshInstance>>
        transparentPending_{};
};

} // namespace re::broker
