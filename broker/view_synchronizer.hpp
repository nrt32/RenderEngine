#pragma once

// broker/view_synchronizer.hpp — ViewSynchronizer full persistence (SPEC §11 V3.2b T3, V3.5 T6).
//
// SRP: single responsibility is poll SceneStore generations / contentHash and
// drive ICachedMapper::mapCached (owns CompositeKey cache + per-field ViewCache).
// Hybrid poll+push via IDirtyTracker (DIP): poll storeGeneration() early-out,
// bounded dirtyFieldsSince() scan, markDirty() push opt-in. Owns no ReView
// lifetime (that's ViewCompositor); but updates ReViews in place via
// ViewCompositor pointer (SRP split per §11.3.1).
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
/// Ownership (T13): `broker`, `stack` and `executor` are SHARED references
/// (co-owned — wiring can never dangle); the compositor back-pointer is a
/// WEAK observer (the compositor is owned by the ViewBridge that wires both
/// sides), locked per sync and treated as absent when expired.

#include <memory>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "broker/broker.hpp"
#include "broker/idirty_tracker.hpp"
#include "broker/render_stack.hpp"
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
                               std::shared_ptr<ViewCompositor> compositor = nullptr,
                               std::shared_ptr<IJobExecutor> executor = nullptr,
                               std::shared_ptr<RenderStack> stack = nullptr)
          : broker_(std::move(broker)), compositor_(std::move(compositor)),
            executor_(executor ? std::move(executor)
                               : std::make_shared<InlineJobExecutor>()),
            stack_(std::move(stack)) {}

     /// Primary sync: views + sceneStore, optional layoutId (default 0 for single-layout).
     /// @note lifetime: `views`/`scene` are call-scoped borrows consumed
     /// synchronously inside this call (never retained).
     data::Result<void> sync(std::span<const scene::View> views,
                             const scene::SceneStore& scene,
                             uint64_t layoutId = 0);

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

    void setCompositor(std::weak_ptr<ViewCompositor> c) noexcept { compositor_ = std::move(c); }

    private:
     struct ViewCache {
         uint64_t rectGen{static_cast<uint64_t>(-1)};
         uint64_t planeGen{static_cast<uint64_t>(-1)};
         uint64_t cameraGen{static_cast<uint64_t>(-1)};
         uint64_t itemsGen{static_cast<uint64_t>(-1)};
         uint64_t viewGen{static_cast<uint64_t>(-1)};
         uint64_t projGen{static_cast<uint64_t>(-1)};
     };
     struct StableKey {
         uint64_t layoutId{0};
         uint64_t viewId{0};
         bool operator==(const StableKey& o) const noexcept {
             return layoutId == o.layoutId && viewId == o.viewId;
         }
     };
     struct StableKeyHash {
         std::size_t operator()(const StableKey& k) const noexcept {
             std::size_t h = std::hash<uint64_t>{}(k.layoutId);
             h ^= std::hash<uint64_t>{}(k.viewId) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
             return h;
         }
     };

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
     /// Weak OBSERVER of the dispatch/present side (owned by the wiring
     /// ViewBridge). Locked per sync; expired == no compositor wired.
     std::weak_ptr<ViewCompositor> compositor_;
     std::shared_ptr<IJobExecutor> executor_;
     /// The technique-renderer set layers bind to (see header comment).
     std::shared_ptr<RenderStack> stack_;
     uint64_t lastStoreGen_{0};
     /// Raw scene-store generation observed at the last completed sync — the
     /// baseline the next sync's conservative item-affecting dirty scan runs
     /// against (object mutations bump the STORE even though the VIEW gens
     /// stand still, so item content changes must re-translate).
     uint64_t lastSceneStoreGen_{0};
     std::unordered_map<StableKey, ViewCache, StableKeyHash> caches_{};
     std::unordered_map<uint64_t, std::vector<scene::FieldId>> pushDirties_{};
     // Last computed scene dirty set for storeGeneration poll
     mutable std::vector<scene::FieldId> lastDirtySet_{};
};

} // namespace re::broker
