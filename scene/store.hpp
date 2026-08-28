#pragma once

// scene/store.hpp — SceneStore / ViewStore stable handles + per-field generation (SPEC §3.1, §10, T1, T5).
//
// Pure value library, no GL, no core. Provides stable uint64_t handles with
// generation bump on add/remove/mutate via the polymorphic ISceneObject
// hierarchy (T1). Single bump(FieldId) entry point per SPEC §10.4. The store
// keeps partitioned maps of unique_ptr<ISceneObject> — one per SceneKind
// (6 technique kinds after T5 collapse: Mesh, MeshSlice, Volume, VolumeSlice,
// Plane, Contour; the 11 byte-identical mesh-backed headers Cube/Sphere/etc.
// at scene/objects/*.hpp:36-40 were collapsed into MeshObject + GeometryKind
// so they no longer need separate partitions). The secondary kindIndex_
// (SceneKind → set<Id>) restores typed iteration without branching. The
// polymorphic replacement for the closed variant< MeshObject,…> alias: each new
// technique kind needs only one new map entry and one registration line, while
// mesh variations (Sphere vs Cube) reuse MeshObject.geometryKind without a new
// header. T17 AssetRef<T> shared-ptr co-ownership stays — objects are
// heap-allocated via make_unique<Derived>, assets remain shared. T5 reduces
// 17→6 partitions (the intermediate state before T6 single-map).

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "data/result.hpp"
#include "scene/asset_id.hpp"
#include "scene/asset_registry.hpp"
#include "scene/detail/generation_tracker.hpp"
#include "scene/field_id.hpp"
#include "scene/iscene_object.hpp"
#include "scene/object.hpp"
#include "scene/view.hpp"

namespace re::scene {

/// SceneStore: owns SceneObject family (polymorphic) with stable handles + generation.
///
/// Each object gets a stable uint64_t id (never reused with same generation —
/// generation increments on removal so stale handles are detectable). storeGen
/// is the global monotonic counter (SPEC §10.4 hybrid poll early-out). The
/// store keeps six partitioned maps (Mesh, MeshSlice, Volume, VolumeSlice,
/// Plane, Contour) as separate unordered_map<Id, unique_ptr<ISceneObject>>
/// tables, so iterating MeshObjects is O(meshCount) not O(total) — no 6→1
/// erased scan — and a secondary kindIndex_ (SceneKind → set<Id>) restores
/// typed iteration. The 11 mesh-backed variations (Cube, Sphere, Cylinder,
/// Torus, Cone, Arrow, Grid, Axes, Capsule, PointCloud, Teapot) are not new
/// technique kinds — they are GeometryKind values inside MeshObject (T5), so
/// they share the Mesh partition and the single MeshObjectMapper. Adding a new
/// technique (e.g., StreamlineObject) still needs one new partition plus one
/// index update, while adding a Sphere variation needs no new header.
class SceneStore {
   public:
    SceneStore() = default;

    /// Add objects; returns stable id. Bumps storeGen and object's generation.
    uint64_t addMeshObject(MeshObject obj);
    uint64_t addMeshSliceObject(MeshSliceObject obj);
    uint64_t addVolumeObject(VolumeObject obj);
    uint64_t addVolumeSliceObject(VolumeSliceObject obj);
    uint64_t addPlaneObject(PlaneObject obj);
    uint64_t addContourObject(ContourObject obj);

    /// Generic add for any ISceneObject kind — used by factory and tests that
    /// exercise open extension without naming the concrete add* wrapper.
    uint64_t addObject(std::unique_ptr<ISceneObject> obj);

    /// Getters — borrow into the store's OWNED storage. Nullptr if
    /// not found or generation mismatch (stale handle is nullptr).
    ///
    /// @note lifetime: the SceneStore owns every object value (its member
    /// maps); a returned pointer is valid only until the next store mutation
    /// (add/remove/erase rehashes or erases the entry). Never delete through
    /// it; never retain it across mutations.
    const MeshObject* /*borrow*/ getMeshObject(uint64_t id) const noexcept;
    const MeshSliceObject* /*borrow*/ getMeshSliceObject(uint64_t id) const noexcept;
    const VolumeObject* /*borrow*/ getVolumeObject(uint64_t id) const noexcept;
    const VolumeSliceObject* /*borrow*/ getVolumeSliceObject(uint64_t id) const noexcept;
    const PlaneObject* /*borrow*/ getPlaneObject(uint64_t id) const noexcept;
    const ContourObject* /*borrow*/ getContourObject(uint64_t id) const noexcept;

    /// Polymorphic generic getter — returns the base ISceneObject borrow for
    /// any id regardless of kind (open for extension — the synchronizer uses
    /// this to resolve ids without branching on the variant). nullptr if not
    /// found.
    /// @note lifetime: same store-owned storage borrow as the typed getters
    /// above — valid until the next add/remove/erase on this store.
    const ISceneObject* /*borrow*/ getObject(uint64_t id) const noexcept;
    ISceneObject* /*borrow*/ getObjectMut(uint64_t id) noexcept;

    /// Typed iteration without branch — returns borrows of all live objects of
    /// exactly kind k by consulting kindIndex_[k] (O(kind) scan, no 6→1 erased
    /// linear scan over unrelated partitions). Empty when no objects of that
    /// kind exist. The secondary index is updated on every add/remove, so the
    /// store never branches on the store's partitions to filter by kind. T1
    /// Phase B, T5 6-partition.
    /// @note lifetime: borrows as above — valid until next store mutation.
    std::vector<const ISceneObject*> /*borrow*/ objectsOfKind(SceneKind k) const noexcept;
    std::vector<ISceneObject*> /*borrow*/ objectsOfKindMut(SceneKind k) noexcept;

    /// Mutable getters for mutation (bump via bump()).
    ///
    /// @note lifetime: same store-owned storage borrow as the const getters
    /// above — valid until the next add/remove/erase on this store.
    MeshObject* /*borrow*/ getMeshObjectMut(uint64_t id) noexcept;
    VolumeObject* /*borrow*/ getVolumeObjectMut(uint64_t id) noexcept;
    ContourObject* /*borrow*/ getContourObjectMut(uint64_t id) noexcept;

    /// Remove by id; bumps storeGen; retains generation tombstone for stale detection.
    /// Returns true if existed.
    bool removeMeshObject(uint64_t id) noexcept;
    bool removeMeshSliceObject(uint64_t id) noexcept;
    bool removeVolumeObject(uint64_t id) noexcept;
    bool removeVolumeSliceObject(uint64_t id) noexcept;
    bool removePlaneObject(uint64_t id) noexcept;
    bool removeContourObject(uint64_t id) noexcept;
    /// Generic remove by id (open for extension — removes whatever kind the id
    /// belongs to by consulting kindIndex_). Returns true if existed.
    bool removeObject(uint64_t id) noexcept;

    /// Global store generation — monotonic, bumped on every add/remove/mutate.
    /// Delegates to the shared GenerationTracker (T9 A6) — single impl for both
    /// SceneStore/ViewStore, no hand-copied duplicate.
    uint64_t storeGeneration() const noexcept { return tracker_.storeGeneration(); }

    /// Single mutation entry point: every field change goes through `bump`
    /// (never a direct storeGen increment elsewhere), which keeps the global
    /// generation and the dirty log consistent and makes "who changed since
    /// when" answerable from one place — a guard against store methods each
    /// inventing their own dirty-tracking side channels (SPEC §10.4). Delegates
    /// to GenerationTracker::bump (shared impl, T9 A6).
    void bump(FieldId field) noexcept;

    /// Push opt-in: mark a specific id/field dirty without the caller needing
    /// to mutate through a getter first. Complements the poll path (storeGen
    /// compare) so consumers can choose cheap polling, precise push, or both
    /// — the hybrid poll+push contract (SPEC §10.4).
    void markDirty(uint64_t id, FieldId field) noexcept;

    /// Counts — symmetric per-kind counters for the 6 technique kinds after T5's collapse of the 11 byte-identical mesh-backed headers (Cube, Sphere, Cylinder, Torus, Cone, Arrow, Grid, Axes, Capsule, PointCloud, Teapot that shared `AssetRef<Mesh> mesh; mat4 transform; MeshMaterialDesc presentation;` at scene/objects/*.hpp:36-40) into one MeshObject carrying GeometryKind {Mesh, Cube, Sphere, Cylinder, Torus, Cone, Arrow, Grid, Axes, Capsule, PointCloud, Teapot} — SceneKind stays for technique dispatch only, adding a Sphere variation needs no new header while a new technique like StreamlineObject still needs one new partition and one registration line, preserving the open-closed principle via registry and the 6-partition O(kind) guarantee — T5.
    size_t meshObjectCount() const noexcept { return meshObjects_.size(); }
    size_t meshSliceObjectCount() const noexcept { return meshSliceObjects_.size(); }
    size_t volumeObjectCount() const noexcept { return volumeObjects_.size(); }
    size_t volumeSliceObjectCount() const noexcept { return volumeSliceObjects_.size(); }
    size_t planeObjectCount() const noexcept { return planeObjects_.size(); }
    size_t contourObjectCount() const noexcept { return contourObjects_.size(); }
    size_t count(SceneKind k) const noexcept { return countOfKind(k); }
    /// Total live objects across all partitions (kindIndex_ size invariant).
    size_t totalObjectCount() const noexcept;
    /// Per-kind live count via kindIndex_ — O(1) for any of the six kinds,
    /// no partition scan.
    size_t countOfKind(SceneKind k) const noexcept;

    /// Fields genuinely changed since `lastGen`, computed from the per-field
    /// dirty log (never a hardcoded superset). The log holds at most ONE slot
    /// per FieldId whose recorded generation is RAISED in place on every new
    /// mutation of that field (bounded drain: memory stays O(#FieldIds)
    /// regardless of frame count), so the answer is the exact distinct set of
    /// fields mutated after `lastGen` in first-mutation order.
    std::vector<FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept;

    /// Resolve a stable object handle against live storage and tombstones
    /// (typed-error stale detection, SPEC §10.4 hybrid contract). Returns
    /// success when `id` is live in any owned object map; error code 2 when
    /// the id was erased (its tombstone generation is retained exactly so a
    /// held handle can be distinguished from a never-existing one); error
    /// code 1 when the id never existed.
    data::Result<void> resolve(uint64_t id) const noexcept;

    // --- Asset identity V3.6 (T7) — SceneStore-owned AssetId per content hash (hashed at load/register time via data/content_hash.hpp:31, never per frame; identical bytes alias to same AssetId, the handle's contentHash is identity with no byObject pointer shim and no pinned refs==0 slots, keeping the renderer's resolve O(1) and slot growth bounded, and the store co-owns every asset via shared_ptr so nothing can dangle behind the caller's back) — T7, SPEC §7, V3.6.
    data::Result<AssetId> registerMeshAsset(
        AssetRegistry<data::Mesh>::SharedAsset mesh);
    data::Result<AssetRegistry<data::Mesh>::SharedAsset> resolveMeshAsset(
        AssetId id) const;
    data::Result<void> unregisterMeshAsset(AssetId id);
    data::Result<AssetId> registerVolumeAsset(
        AssetRegistry<data::VolumeDataset>::SharedAsset vol);
    data::Result<AssetRegistry<data::VolumeDataset>::SharedAsset> resolveVolumeAsset(
        AssetId id) const;
    data::Result<void> unregisterVolumeAsset(AssetId id);
    data::Result<AssetId> registerImageAsset(
        AssetRegistry<data::Image>::SharedAsset img);
    data::Result<AssetRegistry<data::Image>::SharedAsset> resolveImageAsset(
        AssetId id) const;
    data::Result<void> unregisterImageAsset(AssetId id);
    std::size_t meshAssetCount() const noexcept { return meshAssets_.liveCount(); }
    std::size_t meshAssetSlotCount() const noexcept { return meshAssets_.slotCount(); }
    std::size_t volumeAssetCount() const noexcept { return volumeAssets_.liveCount(); }
    std::size_t volumeAssetSlotCount() const noexcept { return volumeAssets_.slotCount(); }
    std::size_t imageAssetCount() const noexcept { return imageAssets_.liveCount(); }
    std::size_t imageAssetSlotCount() const noexcept { return imageAssets_.slotCount(); }
    AssetRegistry<data::Mesh>& meshAssetRegistry() noexcept { return meshAssets_; }
    const AssetRegistry<data::Mesh>& meshAssetRegistry() const noexcept { return meshAssets_; }
    AssetRegistry<data::VolumeDataset>& volumeAssetRegistry() noexcept { return volumeAssets_; }
    const AssetRegistry<data::VolumeDataset>& volumeAssetRegistry() const noexcept { return volumeAssets_; }
    AssetRegistry<data::Image>& imageAssetRegistry() noexcept { return imageAssets_; }
    const AssetRegistry<data::Image>& imageAssetRegistry() const noexcept { return imageAssets_; }

   private:
    uint64_t allocId() noexcept { return nextId_++; }
    void recordDirty_(FieldId field) noexcept;
    template <typename Derived>
    uint64_t addTypedObject_(Derived obj, std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition);
    bool removeFromPartition_(uint64_t id, std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition) noexcept;
    bool containsInPartition_(uint64_t id, const std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition) const noexcept;

    detail::GenerationTracker tracker_;
    uint64_t nextId_{1};

    AssetRegistry<data::Mesh> meshAssets_;
    AssetRegistry<data::VolumeDataset> volumeAssets_;
    AssetRegistry<data::Image> imageAssets_;

    // Partitioned maps — one per SceneKind (6 technique kinds after T5). T5
    // collapses the 11 mesh-backed variations into MeshObject+GeometryKind, so
    // only the 6 technique partitions remain. T6 will tighten further to
    // single-map (see T6). Iterating MeshObjects is O(meshCount) not O(total).
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> meshObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> meshSliceObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> volumeObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> volumeSliceObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> planeObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> contourObjects_;

    // Secondary index: SceneKind → live Ids of exactly that kind (6 kinds).
    std::unordered_map<SceneKind, std::unordered_set<uint64_t>> kindIndex_;
};

/// ViewStore: owns View objects with stable handles + per-field generation.
class ViewStore {
   public:
    ViewStore() = default;
    uint64_t addView(View view);
    const View* /*borrow*/ getView(uint64_t id) const noexcept;
    View* /*borrow*/ getViewMut(uint64_t id) noexcept;
    bool removeView(uint64_t id) noexcept;
    uint64_t storeGeneration() const noexcept { return tracker_.storeGeneration(); }
    void bump(FieldId field) noexcept;
    void markDirty(uint64_t id, FieldId field) noexcept;
    size_t count() const noexcept { return views_.size(); }
    std::vector<FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept;
    data::Result<void> resolve(uint64_t id) const noexcept;
   private:
     uint64_t allocId() noexcept { return nextId_++; }
     void recordDirty_(FieldId field) noexcept;
     detail::GenerationTracker tracker_;
     uint64_t nextId_{1};
     std::unordered_map<uint64_t, View> views_;
};

} // namespace re::scene
