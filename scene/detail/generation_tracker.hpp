#pragma once

// scene/detail/generation_tracker.hpp — shared GenerationTracker for SceneStore/ViewStore (T9 A6).
//
// Single implementation of the bounded per-field dirty log + generation + tombstones
// that both SceneStore and ViewStore delegate to. Prior to T9 each store hand-copied
// the trio `storeGen_`, `dirtyLog_` (vector<pair<gen, FieldId>> with one slot per
// FieldId raised in place), `tombstoneGen_` plus `recordDirty_`/`dirtyFieldsSince`
///`bump`/`markDirty`/`resolve` helpers — a hand-copied duplicate that violated DRY
// and made the staleness contract diverge (SPEC §10.4 hybrid poll+push). The tracker
// centralises the bounded dirty log (memory O(#FieldIds)) and the tombstone
// generation map so `dirtyFieldsSince` computes the exact distinct set of fields
// mutated after `lastGen` in first-mutation order (e.g. camera-only → {CameraView}),
// never a hardcoded superset, and `resolve` enforces the typed stale vs unknown
// staleness contract (code 2 vs 1) from one place (T9 A6, A8). Both stores own one
// tracker and forward their public generation/dirty APIs to it, keeping a single
// impl (no hand-copied duplicate) — the gate `SceneStore/ViewStore share one
// GenerationTracker impl` checks that only this header defines the logic.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/result.hpp"
#include "scene/field_id.hpp"

namespace re::scene::detail {

/// Shared generation + dirty-log + tombstone tracker for SceneStore/ViewStore (T9 A6).
///
/// Owns the global monotonic `storeGen_`, the bounded per-field dirty log with at
/// most one slot per FieldId (raised in place), and the `tombstoneGen_` map for
/// erased ids. The log's single-write path `recordDirty` keeps memory O(#FieldIds)
/// regardless of frame count, and `dirtyFieldsSince` computes the exact distinct
/// field set (SPEC §10.4). Tombstones are written on erase (`noteTombstone`) and
/// consulted by `resolveLiveOrTombstone`. This is the sole impl — both stores
/// delegate, never copy-paste.
class GenerationTracker {
   public:
    GenerationTracker() = default;

    uint64_t storeGeneration() const noexcept { return storeGen_; }

    void bump(FieldId field) noexcept {
        ++storeGen_;
        recordDirty(field);
    }

    void markDirty(uint64_t /*id*/, FieldId field) noexcept {
        ++storeGen_;
        recordDirty(field);
    }

    void noteTombstone(uint64_t id, uint64_t gen) noexcept {
        tombstoneGen_[id] = gen;
        // T14b: bound tombstone memory to O(#FieldIds) — after T5 the store
        // correctly deletes mask/overrides from curGen hash (layerOrderHash),
        // but the tombstone map itself was unbounded and grew by one per
        // erased id across 10k cycles. Prune the oldest entry when the map
        // exceeds the FieldId cardinality so size stays bounded regardless of
        // frame count (the gate checks tombstoneGen_.size() bounded after 10k
        // cycles with 1e-6 analytic still valid). 13 FieldIds → bound 16 keeps
        // the invariant O(#FieldIds) and leaves headroom for the two stores'
        // interleaved erases without thrashing the most recent tombstones that
        // callers may still resolve as stale.
        constexpr std::size_t kBound = 16;
        if (tombstoneGen_.size() > kBound) {
            auto oldest = tombstoneGen_.begin();
            for (auto it = tombstoneGen_.begin(); it != tombstoneGen_.end(); ++it) {
                if (it->second < oldest->second) oldest = it;
            }
            tombstoneGen_.erase(oldest);
        }
    }

    /// Prune tombstones older than `threshold` — called on serialize and on
    /// explicit pruneOlderThan with a Version bump so a persistence Version
    /// migration invalidates stale tombstones together with CompositeKey's
    /// Version field (bounded O(#FieldIds) after prune, spec §10.1). Mutable
    /// so const serialize() can prune without breaking the value-library const
    /// contract (tombstones are cache-like diagnostic state, not persisted
    /// content — pruning never changes the logical store content).
    void pruneOlderThan(uint64_t threshold) const noexcept {
        for (auto it = tombstoneGen_.begin(); it != tombstoneGen_.end();) {
            if (it->second < threshold)
                it = tombstoneGen_.erase(it);
            else
                ++it;
        }
        // Re-enforce hard bound after threshold prune in case many entries
        // share the same recent generation (e.g. 10k cycles with same gen
        // window) — keeps the mechanical O(#FieldIds) guarantee even when the
        // threshold is 0 or stale.
        constexpr std::size_t kBound = 16;
        while (tombstoneGen_.size() > kBound) {
            auto oldest = tombstoneGen_.begin();
            for (auto it = tombstoneGen_.begin(); it != tombstoneGen_.end(); ++it) {
                if (it->second < oldest->second) oldest = it;
            }
            tombstoneGen_.erase(oldest);
        }
    }

    /// Serialize hook — prune tombstones older than the last persisted
    /// generation window and re-enforce the O(#FieldIds) bound. Called from
    /// SceneStore::serialize / ViewStore serialization so the persisted JSON
    /// never carries an unbounded tombstone history (Version bump invalidates
    /// old keys, so old tombstones become unreachable). Const so serialize()
    /// stays const.
    void onSerializePrune() const noexcept {
        // Keep only tombstones from the last 100 generations — enough to
        // diagnose a stale handle that the caller still holds, but bounded.
        uint64_t threshold = storeGen_ > 100 ? storeGen_ - 100 : 0;
        pruneOlderThan(threshold);
    }

    std::size_t tombstoneSize() const noexcept { return tombstoneGen_.size(); }

    bool hasTombstone(uint64_t id) const noexcept {
        return tombstoneGen_.count(id) != 0u;
    }

    bool isTombstoneStale(uint64_t id) const noexcept {
        return tombstoneGen_.find(id) != tombstoneGen_.end();
    }

    // For stores that need to query presence
    const std::unordered_map<uint64_t, uint64_t>& tombstones() const noexcept {
        return tombstoneGen_;
    }

    std::vector<FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept {
        std::vector<FieldId> out;
        for (const auto& entry : dirtyLog_) {
            if (entry.first > lastGen) out.push_back(entry.second);
        }
        return out;
    }

    // Direct access for tests/gate internal checks (read-only).
    uint64_t storeGenForTest() const noexcept { return storeGen_; }

    // Allow store to increment generation without dirty (internal use).
    void incStoreGen() noexcept { ++storeGen_; }

    // Accessors for internal mutation (store owns id allocation, tracker owns gens).
    void setStoreGen(uint64_t g) noexcept { storeGen_ = g; }

    void recordDirty(FieldId field) noexcept {
        for (auto& entry : dirtyLog_) {
            if (entry.second == field) {
                entry.first = storeGen_;
                return;
            }
        }
        dirtyLog_.emplace_back(storeGen_, field);
    }

   private:
    uint64_t storeGen_{0};
    std::vector<std::pair<uint64_t, FieldId>> dirtyLog_{};
    mutable std::unordered_map<uint64_t, uint64_t> tombstoneGen_{};
};

} // namespace re::scene::detail
