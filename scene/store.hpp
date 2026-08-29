#pragma once

// scene/store.hpp — SceneStore / ViewStore stable handles + per-field generation (SPEC §3.1, §10, T1, T5, T6).
//
// Pure value library, no GL, no core. Provides stable uint64_t handles with
// generation bump on add/remove/mutate via the polymorphic ISceneObject
// hierarchy (T1). The store keeps a single primary map of unique_ptr<ISceneObject>
// keyed by stable Id plus a secondary kindIndex_ (SceneKind → set<Id>) for
// O(kind) typed iteration without scanning unrelated objects (T6 single-map).
// Prior to T6 the store held six partitioned maps (one per SceneKind) after T5's
// collapse of the 11 byte-identical mesh-backed headers into MeshObject + GeometryKind;
// T6 replaces those partitions with one map plus the existing index. Templated
// addObject<T>(T) / get<T>(Id) / remove(Id) own the mutation (single SRP template),
// per-kind forwarders (addMeshObject etc.) delegate to that template; typed
// iteration is objectsOfKind(SceneKind) via kindIndex_. Counts are count(SceneKind)
// + totalObjectCount() — no per-kind hand copies. T17 AssetRef<T> shared-ptr
// co-ownership stays — objects are heap-allocated via make_unique<Derived>,
// assets remain shared. T6.

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <string>

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
/// store keeps one primary map (Id → owned ISceneObject) plus a secondary
/// kindIndex_ (SceneKind → live Ids) so iterating objects of exactly one kind
/// is O(kind) not O(total). The 11 mesh-backed variations (Cube, Sphere, etc.)
/// are GeometryKind values inside MeshObject (T5), so they share the Mesh kind
/// and the single MeshObjectMapper. Adding a new technique (e.g., StreamlineObject)
/// needs one new Kind value plus registration; mesh variations need no new header.
class SceneStore {
   public:
    SceneStore() = default;

    /// Templated add — single SRP entry for typed value objects (T6).
    /// Allocates a stable Id, assigns Id + generation (storeGen+1), inserts into
    /// the single map and the secondary kindIndex_, bumps storeGen and dirties
    /// Transform+Items. One template replaces the former six hand-copied add* wrappers.
    template <typename T>
    uint64_t addObject(T obj) {
        static_assert(std::is_base_of_v<ISceneObject, T>, "T must derive from ISceneObject");
        uint64_t id = allocId();
        obj.setId(id);
        obj.setGeneration(tracker_.storeGeneration() + 1);
        SceneKind kind = T::Kind;
        auto ptr = std::make_unique<T>(std::move(obj));
        objects_.emplace(id, std::move(ptr));
        kindIndex_[kind].insert(id);
        tracker_.incStoreGen();
        tracker_.recordDirty(FieldId::Transform);
        tracker_.recordDirty(FieldId::Items);
        return id;
    }

    /// Per-kind add forwarders — thin delegates to addObject<T> (kept for
    /// existing call-site compatibility; core logic lives in the template above).
    uint64_t addMeshObject(MeshObject obj);
    uint64_t addMeshSliceObject(MeshSliceObject obj);
    uint64_t addVolumeObject(VolumeObject obj);
    uint64_t addVolumeSliceObject(VolumeSliceObject obj);
    uint64_t addPlaneObject(PlaneObject obj);
    uint64_t addContourObject(ContourObject obj);

    /// Generic add for any ISceneObject kind — used by factory and tests that
    /// exercise open extension without naming the concrete add* wrapper.
    uint64_t addObject(std::unique_ptr<ISceneObject> obj);

    /// Templated typed getter — single SRP lookup via the primary map plus kind check to enforce type safety without branching over partitions (T6).
    ///
    /// The store's single primary map holds every live ISceneObject keyed by stable Id plus the secondary kindIndex_ for O(kind) iteration; this template consults the primary map directly and verifies the stored object's kind equals T::Kind before down-casting, so callers obtain a correctly typed borrow without a per-kind hand-copied wrapper duplicating the map-lookup logic — one template owns the lookup, per-kind forwarders merely delegate (T6).
    template <typename T>
    const T* /*borrow*/ get(uint64_t id) const noexcept {
        auto it = objects_.find(id);
        if (it == objects_.end()) return nullptr;
        if (it->second->kind() != T::Kind) return nullptr;
        return static_cast<const T*>(it->second.get());
    }
    template <typename T>
    T* /*borrow*/ getMut(uint64_t id) noexcept {
        auto it = objects_.find(id);
        if (it == objects_.end()) return nullptr;
        if (it->second->kind() != T::Kind) return nullptr;
        return static_cast<T*>(it->second.get());
    }

    /// Getters — borrow into the store's OWNED storage. Nullptr if
    /// not found or generation mismatch (stale handle is nullptr).
    ///
    /// @note lifetime: the SceneStore owns every object value (its member
    /// map); a returned pointer is valid only until the next store mutation
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
    /// exactly kind k by consulting kindIndex_[k] (O(kind) scan, no erased
    /// linear scan over unrelated objects). Empty when no objects of that
    /// kind exist. The secondary index is updated on every add/remove, so the
    /// store never branches over unrelated entries to filter by kind. T6 single-map.
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
    /// Templated remove alias — single generic removal that centralizes map plus index mutation without per-kind hand copies (T6).
    ///
    /// Prior to T6 each SceneKind had its own removeMeshObject/removeVolumeObject wrapper duplicating tombstone and index logic; now every removal goes through removeObject (single map plus kindIndex_ update) and this typed convenience merely checks the kind via get<T> before delegating, preserving per-kind call-site compatibility while keeping the ownership-mutation logic in one place (T6).
    bool remove(uint64_t id) noexcept { return removeObject(id); }
    template <typename T>
    bool removeTyped(uint64_t id) noexcept {
        const T* /*borrow*/ p = get<T>(id);
        if (!p) return false;
        return removeObject(id);
    }

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

    /// Counts — single generic counter plus total. After T6 the store no longer
    /// keeps per-kind hand-copied size accessors; callers use count(SceneKind)
    /// or countOfKind(k) (O(1) via kindIndex_) and totalObjectCount() for the
    /// Kind-agnostic total. The secondary index size is the source of truth.
    size_t count(SceneKind k) const noexcept { return countOfKind(k); }
    /// Total live objects across all kinds (kindIndex_ size invariant).
    size_t totalObjectCount() const noexcept;
    /// Per-kind live count via kindIndex_ — O(1) for any of the six kinds,
    /// no map scan.
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

     // --- Loader facades — retired at T1 (scene/store.hpp:215 note retains 4 steps →1 call but points to utils/).
      //
      // Prior to T1 the store offered atomic IO sugar (filesystem → shared_ptr → register → add, 4 steps →1 call, 5/6 samples deduped) that kept the single-map invariant by loading via the mesh/volume loaders (GL-free, typed Result), wrapping in a shared_ptr, registering through the content-hashed Asset Registry (alias on identical bytes, T7 asset identity), and inserting as a Mesh/Volume object via the templated `addObject<T>` path (single primary map + kindIndex_, T6). Failure at any stage short-circuits and propagates the typed Error Domain (MeshIo/VolumeIo) unchanged, so callers branch on domain+code without string parsing (SPEC §5). The transform stays identity and presentation defaults to opaque Phong (mesh) / default TF (volume); samples that need a custom transform or TF mutate the returned object via getMut or use the Objects:: helpers in scene/builders.hpp. At T1 the filesystem IO was extracted to `utils/asset_utils.hpp` (IO-only, header-only, `utils/` owns filesystem) — the 4-step ceremony `load→shared_ptr→registerMeshAsset→addMeshObject` now lives in `utils/` (header stays lean, `SceneStore` stays pure value lib `data+volume+glm` per `docs/spec/modules.md:21`; header no longer includes io headers, linkage is via `utils/` not `scene/`). See `utils/asset_utils.hpp` for the IO entry points (header-only, filesystem-owned).

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
     /// Versioned JSON wire format for persistence (T13).
     ///
     /// Returns a JSON string with `Version` migrations and the `View` wire
     /// format `CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash}` per
     /// `docs/spec/persistence.md` §10.8. `Version` is the persistence schema
     /// version that invalidates the broker cache on migration (hierarchical
     /// `Version:LayoutId:Type:Hash`, `BACKWARD` compat via `SceneMigrator`
     /// chain per DCS Data Contracts). The JSON is built with `nlohmann/json`
     /// 3.11.3 (pinned `GIT_TAG v3.11.3`, `CMakeLists.txt:117`) — `MaterialDesc`
     /// / `LightDesc` stable variant JSON plus the `View` fields (`Rect`,
     /// `Camera`, `CompositeKey`, `Plane`, `ItemIds`, `ClearColor`, `DepthTest`,
     /// `Lights`, `LayerMask`, `LayerOverrides`) that were in-memory only before
     /// T13. `Re*` caches are never serialized (reconstructible via
     /// `ViewSynchronizer` replay); only stable wire is `SceneStore`
     /// (`Id+gen+hash` per object), `MaterialDesc`/`LightDesc`, `LayoutSpec`,
     /// `Camera`. Binary `VolumeDataset` bytes are the NRRD raw `uint16` blob
     /// beside the JSON plus `SHA-256` `contentHash`.
     std::string serialize() const;
     /// Deserialize a JSON string produced by `serialize()`, applying the
     /// `SceneMigrator` chain from the file's `Version` to the current
     /// `kSerializeVersion` (`BACKWARD` compat, new reader reads old writer).
     /// Returns a typed error if the JSON is malformed or the version is
     /// newer than the current.
     static data::Result<SceneStore> deserialize(const std::string& jsonStr);
     /// Current persistence schema version — bump when `Re*` field inventory or
     /// hash algorithm changes, invalidating every cached `CompositeKey`.
     static constexpr uint32_t kSerializeVersion = 1;
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

    detail::GenerationTracker tracker_;
    uint64_t nextId_{1};

    AssetRegistry<data::Mesh> meshAssets_;
    AssetRegistry<data::VolumeDataset> volumeAssets_;
    AssetRegistry<data::Image> imageAssets_;

    // Single primary map — Id maps to the heap-owned polymorphic ISceneObject that this store alone owns; typed iteration and O(kind) counting reuse the secondary kindIndex_ below, which is updated on every add/remove so no per-kind map scan is ever needed (T6).
    //
    // Prior to T6 the store held six hand-written unordered_map<uint64_t, unique_ptr<ISceneObject>> partitions (one per SceneKind) duplicating allocation, tombstone, and index logic; T6 replaces those six partitions with this one map plus the existing kindIndex_ (SceneKind → live Ids), and the single templated addObject<T>/get<T>/remove path owns the mutation — per-kind forwarders merely delegate (T6).
    std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>> objects_;

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
