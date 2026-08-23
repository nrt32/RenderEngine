#pragma once

// broker/asset_store.hpp — Future AssetStore generational handle placeholder (SPEC §7/§10, T3 T).
//
// Skeleton for the SceneStore-owned AssetId path that lands in T7. Provides the
// generational slot table and typed StaleHandle error (code 2) so that
// T3 can already assert the stale generation+1 lookup invariant without waiting
// for T7's content-hash AssetId. render::AssetRegistry already implements the
// same code path; this is the broker-owned counterpart that will later be keyed
// by AssetId{generation,contentHash} and will dedup by content hash rather than
// pointer identity (T7). No GL, no core/ includes.
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

    /// Register mesh (the store takes a SHARED reference — co-ownership, T13);
    /// dedup by content hash (identical bytes alias; not pointer).
    data::Result<BrokerAssetHandle> registerAsset(SharedMesh mesh);

    /// Resolve handle to the live asset as a SHARED reference (co-owned —
    /// no borrow to track); error code 2 for stale generation+1.
    data::Result<SharedMesh> resolve(const BrokerAssetHandle& handle) const;

    /// Free slot; bumps generation so handle becomes stale.
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
    // Diagnostic shim keyed by the live object's address (erased on
    // unregister so no entry outlives its key); NOT the dedup key.
    std::unordered_map<const data::Mesh*, BrokerAssetHandle> byObject_;
    std::unordered_map<uint64_t, BrokerAssetHandle> byHash_;
    size_t liveCount_{0u};
};

} // namespace re::broker
