#pragma once

// scene/asset_registry.hpp — Typed content-addressed asset registry (T7 V3.6).
//
// `AssetRegistry<T>` is the extensible typed store per SPEC §7 / Q28 / Q46:
// one template instantiation per asset kind (`AssetRegistry<data::Mesh>`,
// `AssetRegistry<data::VolumeDataset>`, `AssetRegistry<data::Image>`), no
// per-kind duplicate. Dedup is by `contentHash` (hash of stable bytes, not
// pointer), shared across Views/pages. Generational `AssetId` provides stale
// detection (typed error code 2 on generation+1, never crash). data::Mesh
// stays pure — no AssetId field.
//
// Header-only, GL-free, no core/. Scanned by audit via scene/.

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/result.hpp"
#include "scene/asset_id.hpp"

namespace re::scene {

/// Typed content-addressed asset registry — one per asset kind (SPEC §7 T7).
///
/// Extensible via template parameter `T` (OCP via template, SRP per `T`).
/// `T` must have a `data::computeContentHash(const T&)` overload in the
/// GL-free data/content_hash.hpp (the ONE byte-hash definition shared with the
/// render-side asset store). Ownership (T13): the registry holds a
/// `std::shared_ptr<const T>` slot per registered asset — it CO-OWNS every
/// asset it indexes, so a live handle can always resolve to live bytes and no
/// caller-side owner can pull the asset out from under a resolved reference.
/// Dedup is by `contentHash` (hash of stable bytes, not pointer), shared
/// across Views/pages. Generational `AssetId` provides stale detection (typed
/// error code 2 on generation+1, never crash). data::Mesh stays pure — no
/// AssetId field.
template <typename T>
class AssetRegistry {
   public:
    /// The owned immutable view of a stored asset (resolve() returns a copy:
    /// callers share ownership, never borrow).
    using SharedAsset = std::shared_ptr<const T>;

    AssetRegistry() = default;
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;
    AssetRegistry(AssetRegistry&&) noexcept = default;
    AssetRegistry& operator=(AssetRegistry&&) noexcept = default;

    /// Register `asset`; dedup by content hash (identical bytes alias to same
    /// AssetId even when distinct pointers). The registry stores its own
    /// shared reference to `asset` (co-ownership — see class comment).
    /// Returns existing AssetId on hit, otherwise allocates a new generational
    /// slot.
    data::Result<AssetId> registerAsset(SharedAsset asset) {
        if (!asset) {
            return data::makeError<AssetId>(
                4, "AssetRegistry: null asset shared_ptr");
        }
        // Qualified on purpose: the single hash definition lives in
        // data/content_hash.hpp; an unqualified call here would be ambiguous
        // (re::scene forwarders + argument-dependent lookup of the re::data
        // overloads are both visible).
        const uint64_t hash = data::computeContentHash(*asset);
        auto it = byHash_.find(hash);
        if (it != byHash_.end()) {
            const AssetId& existing = it->second;
            // Validate live slot still matches hash/generation
            if (existing.index < slots_.size()) {
                const Slot& s = slots_[existing.index];
                if (s.live && s.generation == existing.generation &&
                    s.contentHash == hash) {
                    return data::makeValue<AssetId>(existing);
                }
            }
            // Stale hash entry (freed slot) — fall through to reallocate and
            // overwrite byHash_ below.
        }

        std::size_t idx = 0;
        uint32_t gen = 0;
        const T* /*borrow*/ raw = asset.get(); // byObject_ diagnostic key;
        // borrow of the bytes this registry's Slot::object (shared_ptr below)
        // co-owns — the key lives exactly as long as the slot (erased on
        // unregister).
        if (!freeIndices_.empty()) {
            idx = freeIndices_.back();
            freeIndices_.pop_back();
            Slot& s = slots_[idx];
            s.object = std::move(asset);
            s.contentHash = hash;
            s.live = true;
            ++s.generation; // bump from last occupant
            gen = s.generation;
        } else {
            Slot s;
            s.object = std::move(asset);
            s.contentHash = hash;
            s.generation = 1u;
            s.live = true;
            idx = slots_.size();
            slots_.push_back(std::move(s));
            gen = 1u;
        }
        ++liveCount_;
        AssetId id{static_cast<uint32_t>(idx), gen, hash};
        byHash_[hash] = id;
        byObject_[raw] = id; // diagnostic shim keyed on stable object address
                             // while the slot lives (erased on unregister);
                             // NOT the dedup key — contentHash is.
        return data::makeValue<AssetId>(id);
    }

    /// Resolve `id` to its live asset. Returns a SHARED reference (the caller
    /// co-owns the asset with the registry — no borrow to track). Returns
    /// typed errors, never crashes (SPEC §5): code 1 index out of range,
    /// code 2 generation mismatch (stale handle including gen+1), code 3
    /// freed slot.
    data::Result<SharedAsset> resolve(AssetId id) const {
        if (id.index >= slots_.size()) {
            return data::makeError<SharedAsset>(
                1, "AssetRegistry: handle index out of range");
        }
        const Slot& s = slots_[id.index];
        if (s.generation != id.generation) {
            return data::makeError<SharedAsset>(
                2,
                "AssetRegistry: stale handle (generation " +
                    std::to_string(id.generation) + " != slot generation " +
                    std::to_string(s.generation) + ")");
        }
        if (!s.live) {
            return data::makeError<SharedAsset>(
                3, "AssetRegistry: handle references a freed slot");
        }
        if (s.contentHash != id.contentHash) {
            return data::makeError<SharedAsset>(
                2, "AssetRegistry: stale handle (contentHash mismatch)");
        }
        return data::makeValue<SharedAsset>(s.object);
    }

    /// Free the slot `id` references; bumps generation so handle becomes stale.
    data::Result<void> unregister(AssetId id) {
        if (id.index >= slots_.size()) {
            return data::makeError<void>(
                1, "AssetRegistry: handle index out of range");
        }
        Slot& s = slots_[id.index];
        if (s.generation != id.generation) {
            return data::makeError<void>(
                2, "AssetRegistry: stale handle (generation mismatch)");
        }
        if (!s.live) {
            return data::makeError<void>(3, "AssetRegistry: already freed");
        }
        if (s.contentHash != id.contentHash) {
            return data::makeError<void>(2, "AssetRegistry: contentHash mismatch");
        }
        if (s.object) {
            byObject_.erase(s.object.get());
        }
        byHash_.erase(s.contentHash);
        s.object.reset(); // releases the registry's shared reference; other
                          // co-owners (resolved callers) keep the bytes alive
        s.live = false;
        ++s.generation; // future handles to this index are stale
        freeIndices_.push_back(id.index);
        --liveCount_;
        return data::Result<void>(data::value);
    }

    /// Number of currently live assets (one per distinct content hash).
    std::size_t liveCount() const noexcept {
        return liveCount_;
    }
    /// Total slots allocated (including free).
    std::size_t slotCount() const noexcept {
        return slots_.size();
    }

   private:
    struct Slot {
        /// Owned shared reference to the immutable asset (co-ownership — see
        /// class comment). Reset on unregister; other co-owners keep bytes.
        SharedAsset object;
        uint32_t generation = 0u;
        bool live = false;
        uint64_t contentHash = 0u;
    };
    std::vector<Slot> slots_;
    std::vector<std::size_t> freeIndices_;
    std::unordered_map<uint64_t, AssetId> byHash_;
    // Diagnostic shim keyed by the live slot's object address (erased on
    // unregister, so no entry ever outlives the object it keys). Not the
    // dedup key — `byHash_` (content hash) is (T7).
    std::unordered_map<const T*, AssetId> byObject_;
    std::size_t liveCount_{0u};
};

} // namespace re::scene
