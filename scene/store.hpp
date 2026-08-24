#pragma once

// scene/store.hpp — SceneStore / ViewStore stable handles + per-field generation (SPEC §3.1, §10).
//
// Pure value library, no GL, no core. Provides stable uint64_t handles with
// generation bump on add/remove/mutate. Single bump(FieldId) entry point per SPEC §10.4.

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "data/result.hpp"
#include "scene/asset_id.hpp"
#include "scene/asset_registry.hpp"
#include "scene/object.hpp"
#include "scene/view.hpp"

namespace re::scene {

/// Field identifier for per-field generation tracking: each enum value names
/// one independently mutable part of the scene/view state, so a camera orbit
/// can bump only `CameraView` while layout and items stay clean. Mappers use
/// these generations as cache keys to skip re-translating unchanged input
/// (SPEC §10.4).
enum class FieldId : uint8_t {
    Rect = 0,
    Plane = 1,
    CameraView = 2,
    CameraProj = 3,
    Items = 4,
    Transform = 5,
    Material = 6,
    TransferFunction = 7,
};

/// SceneStore: owns SceneObject family with stable handles + generation.
///
/// Each object gets a stable uint64_t id (never reused with same generation —
/// generation increments on removal so stale handles are detectable). storeGen
/// is the global monotonic counter (SPEC §10.4 hybrid poll early-out).
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

    /// Global store generation — monotonic, bumped on every add/remove/mutate.
    uint64_t storeGeneration() const noexcept { return storeGen_; }

    /// Single mutation entry point: every field change goes through `bump`
    /// (never a direct storeGen increment elsewhere), which keeps the global
    /// generation and the dirty log consistent and makes "who changed since
    /// when" answerable from one place — a guard against store methods each
    /// inventing their own dirty-tracking side channels (SPEC §10.4).
    void bump(FieldId field) noexcept;

    /// Push opt-in: mark a specific id/field dirty without the caller needing
    /// to mutate through a getter first. Complements the poll path (storeGen
    /// compare) so consumers can choose cheap polling, precise push, or both
    /// — the hybrid poll+push contract (SPEC §10.4).
    void markDirty(uint64_t id, FieldId field) noexcept;

    /// Counts.
    size_t meshObjectCount() const noexcept { return meshObjects_.size(); }
    size_t volumeObjectCount() const noexcept { return volumeObjects_.size(); }
    size_t planeObjectCount() const noexcept { return planeObjects_.size(); }

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
    /// Live mesh asset count (distinct content hashes).
    std::size_t meshAssetCount() const noexcept {
        return meshAssets_.liveCount();
    }
    /// Total mesh asset slots (including free).
    std::size_t meshAssetSlotCount() const noexcept {
        return meshAssets_.slotCount();
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
    /// Single write path of the bounded dirty log: raise the field's slot
    /// generation in place (or append a new slot) so the log can never grow
    /// past one entry per FieldId (see SceneStore::dirtyFieldsSince).
    void recordDirty_(FieldId field) noexcept;
    uint64_t storeGen_{0};
    uint64_t nextId_{1};
    // Per-field dirty log with ONE slot per FieldId whose recorded
    // generation is raised on every mutation of that field — the bounded
    // structure `dirtyFieldsSince` computes from (never a hardcoded set).
    std::vector<std::pair<uint64_t, FieldId>> dirtyLog_{};

    // Asset registries: one typed store per CPU asset kind (mesh, volume,
    // image). The template makes adding a new kind a one-line member instead
    // of a parallel hand-written registry per type (open/closed extension).
    AssetRegistry<data::Mesh> meshAssets_;
    AssetRegistry<data::VolumeDataset> volumeAssets_;
    AssetRegistry<data::Image> imageAssets_;

    std::unordered_map<uint64_t, MeshObject> meshObjects_;
    std::unordered_map<uint64_t, MeshSliceObject> meshSliceObjects_;
    std::unordered_map<uint64_t, VolumeObject> volumeObjects_;
    std::unordered_map<uint64_t, VolumeSliceObject> volumeSliceObjects_;
    std::unordered_map<uint64_t, PlaneObject> planeObjects_;
    std::unordered_map<uint64_t, ContourObject> contourObjects_;

    // Tombstone generations for removed ids (to detect stale handles).
    std::unordered_map<uint64_t, uint64_t> tombstoneGen_;
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

    uint64_t storeGeneration() const noexcept { return storeGen_; }
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
     /// Single write path of the bounded dirty log: raise the field's slot
     /// generation in place (or append a new slot) so the log can never grow
     /// past one entry per FieldId (see SceneStore::dirtyFieldsSince).
     void recordDirty_(FieldId field) noexcept;
     uint64_t storeGen_{0};
     uint64_t nextId_{1};
     std::unordered_map<uint64_t, View> views_;
     std::unordered_map<uint64_t, uint64_t> tombstoneGen_;
     std::vector<std::pair<uint64_t, FieldId>> dirtyLog_{};
};

} // namespace re::scene
