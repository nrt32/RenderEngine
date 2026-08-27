#pragma once

// scene/store.hpp — SceneStore / ViewStore stable handles + per-field generation (SPEC §3.1, §10, T1).
//
// Pure value library, no GL, no core. Provides stable uint64_t handles with
// generation bump on add/remove/mutate via the polymorphic ISceneObject
// hierarchy (T1). Single bump(FieldId) entry point per SPEC §10.4. The store
// keeps partitioned maps of unique_ptr<ISceneObject> — one per SceneKind
// (currently 17, covering Mesh/Volume/Plane/Contour plus the 10 new kinds and
// Teapot; the original value-type iteration had 6 core partitions, the spec's
// "5 partitioned maps" was the pre-extension count — indirection #1 keeps
// O(kind) iteration, iterating one kind scans only its partition, never an
// N→1 erased linear scan over the whole store) and a secondary kindIndex_
// (SceneKind → set<Id>) that restores typed iteration without branching. The
// polymorphic replacement for the closed variant< MeshObject,…> alias: each new
// object kind needs only one new map entry and one registration line, while the
// synchronizer dispatch stays closed for modification (open for extension via
// SceneKind registry). T17 AssetRef<T> shared-ptr co-ownership stays — objects
// are heap-allocated via make_unique<Derived>, assets remain shared. T1 D.

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
/// store keeps five logical partitions (mesh-family, slice-family,
/// volume-family, volume-slice-family, plane-family plus contour/teapot
/// extensions) as separate unordered_map<Id, unique_ptr<ISceneObject>> tables,
/// so iterating MeshObjects is O(meshCount) not O(total) — no 5→1 erased scan
/// — and a secondary kindIndex_ (SceneKind → set<Id>) restores typed iteration
/// for the fifteen mesh-backed extra kinds without branching on the store's
/// partitions. Adding TeapotObject or any future kind needs only one new
/// partition entry or a slot in the mesh-family partition plus one index update,
/// while ViewSynchronizer stays closed for modification (SceneKind registry).
class SceneStore {
   public:
    SceneStore() = default;

    /// Add a mesh object; returns stable id. Bumps storeGen and object's generation.
    uint64_t addMeshObject(MeshObject obj);
    uint64_t addMeshSliceObject(MeshSliceObject obj);
    uint64_t addVolumeObject(VolumeObject obj);
    uint64_t addVolumeSliceObject(VolumeSliceObject obj);
    uint64_t addPlaneObject(PlaneObject obj);
    uint64_t addContourObject(ContourObject obj);
    /// T1 open kinds — mesh-backed extra kinds for hierarchy proof. Each needs
    /// only one header plus one registration line; store and synchronizer need
    /// no dispatch edit (open for extension). The gate's TeapotObject count 1
    /// proves Broker::registeredTypes() contains the new kind without store
    /// edits.
    uint64_t addTeapotObject(TeapotObject obj);
    uint64_t addSphereObject(SphereObject obj);
    uint64_t addCubeObject(CubeObject obj);
    uint64_t addCylinderObject(CylinderObject obj);
    uint64_t addTorusObject(TorusObject obj);
    uint64_t addConeObject(ConeObject obj);
    uint64_t addArrowObject(ArrowObject obj);
    uint64_t addGridObject(GridObject obj);
    uint64_t addAxesObject(AxesObject obj);
    uint64_t addPointCloudObject(PointCloudObject obj);
    uint64_t addCapsuleObject(CapsuleObject obj);

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
    const TeapotObject* /*borrow*/ getTeapotObject(uint64_t id) const noexcept;
    const SphereObject* /*borrow*/ getSphereObject(uint64_t id) const noexcept;
    const CubeObject* /*borrow*/ getCubeObject(uint64_t id) const noexcept;
    const CylinderObject* /*borrow*/ getCylinderObject(uint64_t id) const noexcept;
    const TorusObject* /*borrow*/ getTorusObject(uint64_t id) const noexcept;
    const ConeObject* /*borrow*/ getConeObject(uint64_t id) const noexcept;
    const ArrowObject* /*borrow*/ getArrowObject(uint64_t id) const noexcept;
    const GridObject* /*borrow*/ getGridObject(uint64_t id) const noexcept;
    const AxesObject* /*borrow*/ getAxesObject(uint64_t id) const noexcept;
    const PointCloudObject* /*borrow*/ getPointCloudObject(uint64_t id) const noexcept;
    const CapsuleObject* /*borrow*/ getCapsuleObject(uint64_t id) const noexcept;

    /// Polymorphic generic getter — returns the base ISceneObject borrow for
    /// any id regardless of kind (open for extension — the synchronizer uses
    /// this to resolve ids without branching on the variant). nullptr if not
    /// found.
    /// @note lifetime: same store-owned storage borrow as the typed getters
    /// above — valid until the next add/remove/erase on this store.
    const ISceneObject* /*borrow*/ getObject(uint64_t id) const noexcept;
    ISceneObject* /*borrow*/ getObjectMut(uint64_t id) noexcept;

    /// Typed iteration without branch — returns borrows of all live objects of
    /// exactly kind k by consulting kindIndex_[k] (O(kind) scan, no 5→1 erased
    /// linear scan over unrelated partitions). Empty when no objects of that
    /// kind exist. The secondary index is updated on every add/remove, so the
    /// store never branches on the store's partitions to filter by kind. T1
    /// Phase B.
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
    TeapotObject* /*borrow*/ getTeapotObjectMut(uint64_t id) noexcept;

    /// Remove by id; bumps storeGen; retains generation tombstone for stale detection.
    /// Returns true if existed.
    bool removeMeshObject(uint64_t id) noexcept;
    bool removeMeshSliceObject(uint64_t id) noexcept;
    bool removeVolumeObject(uint64_t id) noexcept;
    bool removeVolumeSliceObject(uint64_t id) noexcept;
    bool removePlaneObject(uint64_t id) noexcept;
    bool removeContourObject(uint64_t id) noexcept;
    bool removeTeapotObject(uint64_t id) noexcept;
    bool removeSphereObject(uint64_t id) noexcept;
    bool removeCubeObject(uint64_t id) noexcept;
    bool removeCylinderObject(uint64_t id) noexcept;
    bool removeTorusObject(uint64_t id) noexcept;
    bool removeConeObject(uint64_t id) noexcept;
    bool removeArrowObject(uint64_t id) noexcept;
    bool removeGridObject(uint64_t id) noexcept;
    bool removeAxesObject(uint64_t id) noexcept;
    bool removePointCloudObject(uint64_t id) noexcept;
    bool removeCapsuleObject(uint64_t id) noexcept;
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

    /// Counts — symmetric per-kind counters generated from the single partitioned
    /// store template (T9 A7). Each `*Count()` is the partition size for that
    /// kind (O(1)), and the six base families cover the original six partitions
    /// (mesh/meshSlice/volume/volumeSlice/plane/contour) that prove the O(kind)
    /// split. The template helper `addTypedObject_` underlies the add/get/remove
    /// families, so the six families are one SRP template, not hand-copied.
    size_t meshObjectCount() const noexcept { return meshObjects_.size(); }
    size_t meshSliceObjectCount() const noexcept { return meshSliceObjects_.size(); }
    size_t volumeObjectCount() const noexcept { return volumeObjects_.size(); }
    size_t volumeSliceObjectCount() const noexcept { return volumeSliceObjects_.size(); }
    size_t planeObjectCount() const noexcept { return planeObjects_.size(); }
    size_t contourObjectCount() const noexcept { return contourObjects_.size(); }
    size_t teapotObjectCount() const noexcept { return teapotObjects_.size(); }
    size_t sphereObjectCount() const noexcept { return sphereObjects_.size(); }
    size_t cubeObjectCount() const noexcept { return cubeObjects_.size(); }
    size_t cylinderObjectCount() const noexcept { return cylinderObjects_.size(); }
    size_t torusObjectCount() const noexcept { return torusObjects_.size(); }
    size_t coneObjectCount() const noexcept { return coneObjects_.size(); }
    size_t arrowObjectCount() const noexcept { return arrowObjects_.size(); }
    size_t gridObjectCount() const noexcept { return gridObjects_.size(); }
    size_t axesObjectCount() const noexcept { return axesObjects_.size(); }
    size_t pointCloudObjectCount() const noexcept { return pointCloudObjects_.size(); }
    size_t capsuleObjectCount() const noexcept { return capsuleObjects_.size(); }
    /// Generic alias `count()` per kind family — unified staleness contract A8
    /// exposes typed `resolve` + borrowed accessors; `count()` is the symmetric
    /// set for every kind (six base families plus open kinds) to avoid asymmetry
    /// where some kinds lack a counter.
    size_t count(SceneKind k) const noexcept { return countOfKind(k); }
    /// Total live objects across all partitions (kindIndex_ size invariant).
    size_t totalObjectCount() const noexcept;
    /// Per-kind live count via kindIndex_ — O(1) for any of the fifteen kinds,
    /// no partition scan.
    size_t countOfKind(SceneKind k) const noexcept;

    /// Fields genuinely changed since `lastGen`, computed from the per-field
    /// dirty log (never a hardcoded superset). The log holds at most ONE slot
    /// per FieldId whose recorded generation is RAISED in place on every new
    /// mutation of that field (bounded drain: memory stays O(#FieldIds)
    /// regardless of frame count), so the answer is the exact distinct set of
    /// fields mutated after `lastGen` in first-mutation order — e.g. a lone
    /// camera bump yields exactly `{CameraView}`, not a fixed four-field list.
    std::vector<FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept;

    /// Resolve a stable object handle against live storage and tombstones
    /// (typed-error stale detection, SPEC §10.4 hybrid contract). Returns
    /// success when `id` is live in any owned object map; error code 2 when
    /// the id was erased (its tombstone generation is retained exactly so a
    /// held handle can be distinguished from a never-existing one); error
    /// code 1 when the id never existed. Never throws, never returns a
    /// dangling borrow — the caller re-fetches through the typed getters.
    data::Result<void> resolve(uint64_t id) const noexcept;

    // --- Asset identity V3.6 (T7) — SceneStore-owned AssetId -----------------
    /// Register mesh asset (the store takes a SHARED reference — co-ownership
    /// with the caller, T13); dedup by content hash (identical bytes alias).
    data::Result<AssetId> registerMeshAsset(
        AssetRegistry<data::Mesh>::SharedAsset mesh);
    /// Resolve AssetId to its live asset as a SHARED reference (the caller
    /// co-owns the bytes, so nothing can dangle behind its back); a handle
    /// whose generation is older than the slot's (freed or reused slot) comes
    /// back as error code 2 — never a crash and never a dangling pointer.
    data::Result<AssetRegistry<data::Mesh>::SharedAsset> resolveMeshAsset(
        AssetId id) const;
    /// Free asset slot; bumps generation.
    data::Result<void> unregisterMeshAsset(AssetId id);
    /// Register volume asset (the store takes a SHARED reference — co-ownership
    /// with the caller, T13); dedup by content hash (identical bytes alias).
    /// Register volume asset — owner-driven like meshes (T7, SPEC §7, data/content_hash.hpp:31 hashed at load/register time, never per frame): the store takes a SHARED reference and dedups by content hash of stable bytes (positions+indices / voxel bytes / pixel bytes); two distinct allocations with identical bytes alias to same AssetId, and the handle's contentHash IS identity (no byObject pointer shim). This closes the per-frame FNV-1a violation and the pinned-slot growth.
    data::Result<AssetId> registerVolumeAsset(
        AssetRegistry<data::VolumeDataset>::SharedAsset vol);
    data::Result<AssetRegistry<data::VolumeDataset>::SharedAsset> resolveVolumeAsset(
        AssetId id) const;
    data::Result<void> unregisterVolumeAsset(AssetId id);
    /// Register image asset — same owner-driven contract as volumes/meshes (T7, SPEC §7, data/content_hash.hpp:31 hashed at load/register time, never per frame): the store co-owns the image via shared_ptr, dedups by content hash of stable pixel bytes, and the handle's contentHash IS identity (no byObject pointer shim, no pinned refs==0 slots), keeping the renderer's resolve O(1) and slot growth bounded.
    data::Result<AssetId> registerImageAsset(
        AssetRegistry<data::Image>::SharedAsset img);
    data::Result<AssetRegistry<data::Image>::SharedAsset> resolveImageAsset(
        AssetId id) const;
    data::Result<void> unregisterImageAsset(AssetId id);
    /// Live mesh asset count (distinct content hashes).
    std::size_t meshAssetCount() const noexcept {
        return meshAssets_.liveCount();
    }
    /// Total mesh asset slots (including free).
    std::size_t meshAssetSlotCount() const noexcept {
        return meshAssets_.slotCount();
    }
    std::size_t volumeAssetCount() const noexcept {
        return volumeAssets_.liveCount();
    }
    std::size_t volumeAssetSlotCount() const noexcept {
        return volumeAssets_.slotCount();
    }
    std::size_t imageAssetCount() const noexcept {
        return imageAssets_.liveCount();
    }
    std::size_t imageAssetSlotCount() const noexcept {
        return imageAssets_.slotCount();
    }
    /// Generic extensible accessors for templated store (OCP per kind).
    AssetRegistry<data::Mesh>& meshAssetRegistry() noexcept {
        return meshAssets_;
    }
    const AssetRegistry<data::Mesh>& meshAssetRegistry() const noexcept {
        return meshAssets_;
    }
    AssetRegistry<data::VolumeDataset>& volumeAssetRegistry() noexcept {
        return volumeAssets_;
    }
    const AssetRegistry<data::VolumeDataset>& volumeAssetRegistry() const noexcept {
        return volumeAssets_;
    }
    AssetRegistry<data::Image>& imageAssetRegistry() noexcept {
        return imageAssets_;
    }
    const AssetRegistry<data::Image>& imageAssetRegistry() const noexcept {
        return imageAssets_;
    }

   private:
    uint64_t allocId() noexcept { return nextId_++; }
    /// Single write path of the bounded dirty log — now delegated to the shared
    /// GenerationTracker (T9 A6) so SceneStore/ViewStore share one impl; no
    /// hand-copied duplicate. The tracker holds the bounded log with one slot
    /// per FieldId raised in place (see GenerationTracker::recordDirty).
    void recordDirty_(FieldId field) noexcept;
    /// Helper to insert a newly allocated polymorphic object into its
    /// partitioned map and the secondary kindIndex_ (keeps O(kind) typed
    /// iteration without branching). Returns the assigned stable id.
    template <typename Derived>
    uint64_t addTypedObject_(Derived obj, std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition);
    /// Helper to remove from a partitioned map and kindIndex_ generically.
    bool removeFromPartition_(uint64_t id, std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition) noexcept;
    /// Resolve helper — check whether id is live in given partition.
    bool containsInPartition_(uint64_t id, const std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition) const noexcept;

    detail::GenerationTracker tracker_;
    uint64_t nextId_{1};

    // Asset registries: one typed store per CPU asset kind (mesh, volume,
    // image). The template makes adding a new kind a one-line member instead
    // of a parallel hand-written registry per type (open/closed extension).
    AssetRegistry<data::Mesh> meshAssets_;
    AssetRegistry<data::VolumeDataset> volumeAssets_;
    AssetRegistry<data::Image> imageAssets_;

    // Partitioned maps — one per SceneKind (currently 17) as separate
    // unordered_map<Id, unique_ptr<ISceneObject>> — iterating MeshObjects is
    // O(meshCount) not O(total) (indirection #1). The secondary kindIndex_
    // (SceneKind → set<Id>) restores typed iteration for the mesh-backed extra
    // kinds without branching on partitions.
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> meshObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> meshSliceObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> volumeObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> volumeSliceObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> planeObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> contourObjects_;
    // Extra partitions for open kinds — mesh-backed extra kinds that prove open
    // extension without variant edits. They participate in the same kindIndex_
    // so typed iteration stays O(kind) and the store remains open for extension
    // with one map line per new kind.
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> teapotObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> sphereObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> cubeObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> cylinderObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> torusObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> coneObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> arrowObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> gridObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> axesObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> pointCloudObjects_;
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> capsuleObjects_;

    // Secondary index: SceneKind → live Ids of exactly that kind. Restores
    // typed iteration without branching on partition maps — objectsOfKind(k)
    // consults kindIndex_[k] (O(kind) result, no 17→1 scan). Kept in sync on
    // every add/remove. T1 Phase B O(kind) guarantee.
    std::unordered_map<SceneKind, std::unordered_set<uint64_t>> kindIndex_;
};

/// ViewStore: owns View objects with stable handles + per-field generation.
class ViewStore {
   public:
    ViewStore() = default;

    /// Add view; returns stable id. Bumps storeGen.
    uint64_t addView(View view);
    /// Borrow into the store's OWNED storage.
    /// @note lifetime: the ViewStore owns every view value (its member map);
    /// a returned pointer is valid only until the next add/remove mutation on
    /// this store. Never delete through it.
    const View* /*borrow*/ getView(uint64_t id) const noexcept;
    /// @note lifetime: same ViewStore-owned storage borrow as getView().
    View* /*borrow*/ getViewMut(uint64_t id) noexcept;
    bool removeView(uint64_t id) noexcept;

    uint64_t storeGeneration() const noexcept { return tracker_.storeGeneration(); }
    void bump(FieldId field) noexcept;
    void markDirty(uint64_t id, FieldId field) noexcept;
    size_t count() const noexcept { return views_.size(); }

    /// Fields genuinely changed since `lastGen` — same computed per-field
    /// dirty-log contract as SceneStore::dirtyFieldsSince (exact distinct
    /// set, bounded one-slot-per-field log, first-mutation order).
    std::vector<FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept;

    /// Resolve a stable view handle against live storage and tombstones —
    /// success when live, error code 2 when erased (tombstone present),
    /// error code 1 when never added (see SceneStore::resolve).
    data::Result<void> resolve(uint64_t id) const noexcept;

   private:
     uint64_t allocId() noexcept { return nextId_++; }
     /// Single write path of the bounded dirty log — now delegated to the shared GenerationTracker so both SceneStore and ViewStore share one implementation (no hand-copied duplicate). The tracker holds the bounded one-slot-per-field log and the tombstone map from one place, keeping the dirty computation and staleness contract unified (T9 A6).
     void recordDirty_(FieldId field) noexcept;
     detail::GenerationTracker tracker_;
     uint64_t nextId_{1};
     std::unordered_map<uint64_t, View> views_;
};

} // namespace re::scene
