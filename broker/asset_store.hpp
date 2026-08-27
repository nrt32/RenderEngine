#pragma once

// broker/asset_store.hpp — Broker-side generational handle store (SPEC §7/§10, T3 T, T7 owner-driven).
//
// Provides the generational slot table and typed StaleHandle error (code 2) so that
// T3 can already assert the stale generation+1 lookup invariant without waiting
// for T7's content-hash AssetId. render::AssetRegistry already implements the
// same code path; this is the broker-owned counterpart keyed by AssetId{generation,contentHash} and deduping by content hash rather than pointer identity (T7, SPEC §7, data/content_hash.hpp:31 hashed at load/register time, never per frame). T7 landed: SceneStore now owns AssetId for Mesh/VolumeDataset/Image via templated AssetRegistry (scene/asset_registry.hpp), and broker mappers (VolumeObjectMapper, VolumeSliceObjectMapper, PlaneObjectMapper) register volumes/images through the shared render::AssetRegistry at sync, handing renderers VolumeTextureHandle/ImageTextureHandle for O(1) resolve (no per-frame FNV-1a, no lookupVolume/lookupImage lazy insertion paths, no pinned refs==0 slots — content-hash IS identity). This file remains the mesh-kind broker store (volume/image kinds live in SceneStore's templated registries and the render::AssetRegistry); its content-hash dedup and generational stale-handle contract mirror the render store. No GL, no core/ includes.
//
// Always returns typed errors, never crashes (SPEC §5, R4 evidence rule).

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/mesh.hpp"
#include "data/result.hpp"
#include "scene/asset_id.hpp"

namespace re::broker {

/// Copyable generational handle mirroring render::AssetHandle but owned by broker/
/// SceneStore (future AssetId{generation,hash} will wrap this).
struct BrokerAssetHandle {
    uint32_t index = 0u;
    uint32_t generation = 0u;
    bool isNull() const noexcept { return index == 0u && generation == 0u; }
};

inline bool operator==(const BrokerAssetHandle& a,
                       const BrokerAssetHandle& b) noexcept {
    return a.index == b.index && a.generation == b.generation;
}
inline bool operator!=(const BrokerAssetHandle& a,
                       const BrokerAssetHandle& b) noexcept {
    return !(a == b);
}

/// Generational asset store with stable handles + typed stale errors (code 2).
///
/// T7 content-hash dedup: dedup is by `contentHash` (hash of stable bytes,
/// not pointer). Two distinct `data::Mesh` allocations with identical bytes
/// share the same handle — content-hash path (SPEC §7 T7). Ownership (T13):
/// each slot holds a SHARED reference (`std::shared_ptr<const data::Mesh>`) —
/// the store co-owns every asset it indexes, so a live handle always resolves
/// to live bytes. Pointer identity kept only as a diagnostic shim key (erased
/// on unregister; not the dedup key). Generational slot table gives the same
/// typed error codes as render::AssetRegistry:
///   code 1 — index out of range
///   code 2 — generation mismatch / stale handle (generation+1 probe)
///   code 3 — freed slot
/// No GL, no core/. Broker is the only lib that may include both scene/ and
/// render/ (ACL), so it may include scene/asset_id.hpp for hash.
class AssetStore {
   public:
    /// The owned immutable view of a stored asset (resolve() returns a copy:
    /// callers share ownership, never borrow).
    using SharedMesh = std::shared_ptr<const data::Mesh>;

    AssetStore() = default;
    AssetStore(const AssetStore&) = delete;
    AssetStore& operator=(const AssetStore&) = delete;
    AssetStore(AssetStore&&) noexcept = default;
    AssetStore& operator=(AssetStore&&) noexcept = default;

    /// Register `mesh` and return a generational handle. The store takes a
    /// SHARED reference (co-ownership: the CPU bytes stay alive while either
    /// the store or the caller holds them). Dedup is by CONTENT hash —
    /// registering the same bytes through another pointer aliases the same
    /// slot instead of duplicating GPU-side state downstream.
    data::Result<BrokerAssetHandle> registerAsset(SharedMesh mesh);

    /// Resolve a handle to its live asset as a SHARED reference (callers
    /// co-own the bytes, so nothing can dangle behind their back). A stale
    /// handle — one minted before the slot's last free/reuse — resolves to
    /// typed error code 2, never to wrong bytes and never a crash.
    data::Result<SharedMesh> resolve(const BrokerAssetHandle& handle) const;

    /// Free the slot `handle` refers to; the slot's generation is bumped so
    /// every outstanding handle to it becomes permanently stale (detectable,
    /// not dangling).
    data::Result<void> unregister(const BrokerAssetHandle& handle);

    /// Live slot count.
    size_t slotCount() const noexcept { return liveCount_; }

   private:
    struct Slot {
        SharedMesh cpuObject; // owned shared reference (reset on unregister)
        uint32_t generation = 0u;
        bool live = false;
        uint64_t contentHash{0u};
    };
    std::vector<Slot> slots_;
    std::vector<size_t> freeIndices_;
    std::unordered_map<uint64_t, BrokerAssetHandle> byHash_;
    size_t liveCount_{0u};
};

} // namespace re::broker
