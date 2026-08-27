#pragma once

// broker/view_synchronizer.hpp — ViewSynchronizer full persistence (SPEC §11 V3.2b T3, V3.5 T6, T9 A3).
//
// SRP: single responsibility is poll SceneStore generations / contentHash and
// drive ICachedMapper::mapCached (owns CompositeKey cache + per-field ViewCache).
// Hybrid poll+push via IDirtyTracker (DIP): poll storeGeneration() early-out,
// bounded dirtyFieldsSince() scan, markDirty() push opt-in. Owns no ReView
// lifetime (that's ViewCompositor); but updates ReViews in place via
// ViewCompositor pointer passed explicitly per sync (SRP split per §11.3.1,
// T9 A3 — the former weak pointer to ViewCompositor back-pointer cycle is removed;
// ViewBridge::sync passes the compositor as a call-scoped borrow, so the
// synchronizer never retains a view-compositor handle).
//
// Item translation produces REAL layers (the "no silent drops" contract): a
// matched scene object maps through its registered per-type mapper into an RE
// value which is wrapped as a live type-erased drawLayer bound to the matching
// RenderStack renderer — MeshObject→MeshRenderer, VolumeObject→
// VolumeRenderer (a working ray-cast layer, never a no-op), VolumeSliceObject→
// VolumeSliceRenderer (GPU extraction), MeshSliceObject→SliceRenderer,
// PlaneObject→PlaneRenderer, ContourObject→ContourRenderer. An item id that
// resolves to NOTHING in the store is a typed error — a silently skipped item
// is visually indistinguishable from "renders nothing", the exact defect this
// replaced. Transparent mesh instances ride out-of-band when the stack carries
// an OIT pipeline: they go to the compositor's capture stage instead of inline
// layers (FR-render.2/3), because View layers never engage the pipeline.
//
/// Ownership (T13, T9 A3): `broker` and `stack` are SHARED references (co-owned —
/// wiring can never dangle); the compositor is a call-scoped borrow passed to
/// `sync` (never retained), so no synchronizer↔compositor cycle remains.

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "broker/broker.hpp"
#include "broker/idirty_tracker.hpp"
#include "broker/render_stack.hpp"
#include "broker/stable_key.hpp"
#include "data/result.hpp"
#include "scene/composite_key.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"

namespace re::render {
class View; // forward: item layers are attached to a render::View
}

namespace re::broker {

class ViewCompositor; // forward

/// View synchronizer — cache/dirty side of IViewBridge (SRP via composition).
///
/// Implements IDirtyTracker for hybrid poll+push DIP (ViewSynchronizer is the
/// collaborator from T2; SceneStore/ViewStore adapters also implement the same
/// abstraction so sync can be tested against either).
///
/// Polls SceneStore::storeGeneration() as early-out, then iterates
/// dirtyFieldsSince(lastStoreGen) bounded set (hybrid poll+push per §10.4).
/// For T6 it also diffs per-field generations (rectGen/planeGen/cameraGen/
/// itemsGen and Camera viewGen/projGen) and drives ICachedMapper::mapCached
/// for the dirty field only (Camera::rotate dirties only CameraMapper).
/// ReView identity by CompositeKey{Version,LayoutId,ViewId} stable — no map churn
/// on 2D→3D toggle or camera orbit; size resize recreates only ViewTarget inner FBO.
class ViewSynchronizer : public IDirtyTracker {
    public:
     explicit ViewSynchronizer(std::shared_ptr<Broker> broker,
                               std::shared_ptr<RenderStack> stack = nullptr)
          : broker_(std::move(broker)), stack_(std::move(stack)) {}

     // Legacy overload for tests that constructed with (broker, compositor, stack).
     // Stores the compositor as a shared fallback for old call sites that still
     // call sync(views, scene, layoutId) without an explicit compositor arg;
     // the new ViewBridge path passes the compositor explicitly and does not use
     // this fallback. The stored handle is a SHARED fallback only for legacy
     // test compatibility — it is NOT a weak cycle (T9 A3 forbids weak_ptr, the
     // primary path is explicit per-call).
     explicit ViewSynchronizer(std::shared_ptr<Broker> broker,
                               std::shared_ptr<ViewCompositor> compositor,
                               std::shared_ptr<RenderStack> stack = nullptr)
          : broker_(std::move(broker)),
            legacyCompositor_(std::move(compositor)),
            stack_(std::move(stack)) {}

     /// Primary sync: views + sceneStore, optional layoutId + explicit compositor.
     /// @note lifetime: `views`/`scene`/`compositor` are call-scoped borrows
     /// consumed synchronously inside this call (never retained). The compositor
     /// is the dispatch/present side owned by ViewBridge and passed here
     /// explicitly so the synchronizer never retains a handle (T9 A3 — removes
     /// the weak pointer to ViewCompositor cycle).
     data::Result<void> sync(std::span<const scene::View> views,
                             const scene::SceneStore& scene,
                             uint64_t layoutId = 0,
                             ViewCompositor* /*borrow*/ compositor = nullptr);
     // Convenience overload without layoutId (views+scene only) — forwards to
     // the primary with layoutId 0 and no compositor (early skeleton wiring).
     data::Result<void> sync(std::span<const scene::View> views,
                             const scene::SceneStore& scene,
                             ViewCompositor* /*borrow*/ compositor) {
         return sync(views, scene, 0, compositor);
     }

    /// Push opt-in: mark one view's field dirty between frames so the next
    /// sync() re-translates exactly that field even if the poll path (store
    /// generation compare) saw no change — the push half of the hybrid
    /// poll+push dirty contract.
    void markDirty(uint64_t viewId, scene::FieldId field) noexcept override;

     /// For test: last synced store generation (poll early-out).
     uint64_t lastStoreGen() const noexcept { return lastStoreGen_; }

     /// IDirtyTracker facet (for tests exercising tracker directly).
     uint64_t storeGeneration() const noexcept override;
     std::vector<scene::FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept override;

    private:
     struct ViewCache {
         uint64_t rectGen{static_cast<uint64_t>(-1)};
         uint64_t planeGen{static_cast<uint64_t>(-1)};
         uint64_t cameraGen{static_cast<uint64_t>(-1)};
         uint64_t itemsGen{static_cast<uint64_t>(-1)};
         uint64_t viewGen{static_cast<uint64_t>(-1)};
         uint64_t projGen{static_cast<uint64_t>(-1)};
         uint64_t clearColorGen{static_cast<uint64_t>(-1)};
         uint64_t depthTestGen{static_cast<uint64_t>(-1)};
     };
     // ReView cache identity is the SHARED broker::StableKey (the same
     // definition the compositor's map keys on) — no local twin key exists
     // anymore, so a synchronizer cache entry and its ReView can never
     // disagree on identity.

     bool hasPushDirty(uint64_t viewId, scene::FieldId field) const noexcept;

     /// Translate ONE item id into a live layer on `rv`. Dispatches through
     /// the registered per-type mapper + the matching RenderStack renderer;
     /// returns a typed error for unknown ids (never a silent skip).
     /// Transparent mesh instances are appended to `transparentOut` instead of
     /// becoming inline layers when the stack carries an OIT pipeline.
     /// @note lifetime: `rv` is a non-owning view over the compositor's
     /// views_ storage (the ReView this layer attaches to); it is consumed
     /// synchronously within this call and never retained.
     data::Result<void> mapItemToLayer(
         const scene::View& av, const scene::SceneStore& scene,
         uint64_t oid, render::View* /*borrow*/ rv,
         std::vector<render::MeshInstance>& transparentOut);

     /// Shared_ptr alias used inside mapItemToLayer (reads better than the
     /// raw member type there).
     std::shared_ptr<Broker> broker_;
     /// Legacy fallback compositor for old test call sites that constructed
     /// the synchronizer with (broker, compositor, stack) and call the 3-arg
     /// sync without an explicit compositor. New ViewBridge code always passes
     /// the compositor explicitly, so this fallback is not used on the primary
     /// path (T9 A3 — the synchronizer never retains a weak cycle on the new
     /// path).
     std::shared_ptr<ViewCompositor> legacyCompositor_;
     /// The technique-renderer set layers bind to (see header comment).
     std::shared_ptr<RenderStack> stack_;
     uint64_t lastStoreGen_{0};
     /// Raw scene-store generation observed at the last completed sync — the
     /// baseline the next sync's conservative item-affecting dirty scan runs
     /// against (object mutations bump the STORE even though the VIEW gens
     /// stand still, so item content changes must re-translate).
     uint64_t lastSceneStoreGen_{0};
     std::unordered_map<StableKey, ViewCache> caches_{};
     std::unordered_map<uint64_t, std::vector<scene::FieldId>> pushDirties_{};
};

} // namespace re::broker