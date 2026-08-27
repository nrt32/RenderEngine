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
    }

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
    std::unordered_map<uint64_t, uint64_t> tombstoneGen_{};
};

} // namespace re::scene::detail
