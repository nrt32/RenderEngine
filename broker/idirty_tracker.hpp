#pragma once

// broker/idirty_tracker.hpp — IDirtyTracker + IJobExecutor seam (SPEC §10.4, V3.5 T6).
//
// Hybrid poll+push dirty wiring via IDirtyTracker abstraction (DIP):
// ViewSynchronizer depends on IDirtyTracker, not concrete SceneStore/ViewStore.
// Future ThreadPoolExecutor can batch invalidations without editing synchronizer.
// IJobExecutor is the single-threaded execution seam: its inline synchronous
// fallback keeps ASan/UBSan 1-thread determinism. It deliberately ships with
// ONLY the scalar execute() entry point — the former unused batch entry
// (exercised by a discarded-results call in the synchronizer, a review
// finding) was removed rather than kept as write-only scaffolding; the batched
// form arrives together with a real ThreadPoolExecutor implementation that has
// a genuine consumer.
//
// Pure interfaces, header-only, GL-free.

#include <cstdint>
#include <memory>
#include <vector>

#include "scene/store.hpp" // for FieldId

namespace re::scene {
class SceneStore;
class ViewStore;
} // namespace re::scene

namespace re::broker {

/// Dirty tracking abstraction for hybrid poll/push (SPEC §10.4).
///
/// Poll-mostly with push opt-in: every renderFrame polls
/// storeGeneration() vs lastStoreGen as cheap early-out (no scan if unchanged);
/// if changed, iterates dirtyFieldsSince(lastGen) bounded set.
/// markDirty(ViewId, FieldId) push path for off-frame editor edits.
class IDirtyTracker {
   public:
    virtual ~IDirtyTracker() = default;

    /// Global monotonic generation (SceneStore::storeGeneration or ViewStore).
    virtual uint64_t storeGeneration() const noexcept = 0;

    /// Bounded set of FieldIds that changed since lastGen (not full store scan).
    virtual std::vector<re::scene::FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept = 0;

    /// Push path: mark a specific view/object field dirty (opt-in, off-frame edits).
    virtual void markDirty(uint64_t id, re::scene::FieldId field) noexcept = 0;
};

/// Job executor seam for OCP threading (SPEC §10.4, NFR §5).
///
/// The default is an inline synchronous fallback (execute(f){f();}), which
/// keeps test runs single-threaded and therefore deterministic under
/// ASan/UBSan. A real thread pool can later extend this interface (adding the
/// batched entry point it actually needs) without editing broker/ or scene/
/// code — callers depend only on the abstraction (open/closed principle).
/// Deliberately execute()-only today: no batched API is kept alive by fake or
/// discarded-result call sites.
class IJobExecutor {
   public:
    virtual ~IJobExecutor() = default;
    virtual void execute(void (* /*borrow*/ fn)(void*), void* /*borrow*/ ctx) = 0; // @note lifetime: borrowed — ctx owned by caller, fn is function pointer, valid for duration of call
};

/// Inline synchronous fallback (zero threads, deterministic).
class InlineJobExecutor final : public IJobExecutor {
   public:
    void execute(void (* /*borrow*/ fn)(void*), void* /*borrow*/ ctx) override { fn(ctx); } // @note lifetime: borrowed — ctx owned by caller, valid for duration of call
};

/// Adapter: SceneStore as IDirtyTracker (DIP — broker depends on abstraction, store is detail).
///
/// SceneStore itself stays broker-free (scene/ never includes broker/); the adapter
/// lives in broker/ and translates SceneStore's concrete dirty API to IDirtyTracker.
/// Ownership (T13): the adapter holds a SHARED reference to the store — co-owned
/// with its owner, so a long-lived tracker can never dangle, and no const_cast
/// is needed for markDirty (the store is mutable through the shared handle).
class SceneStoreTracker final : public IDirtyTracker {
    public:
     explicit SceneStoreTracker(std::shared_ptr<re::scene::SceneStore> store)
         : store_(std::move(store)) {}
     uint64_t storeGeneration() const noexcept override {
         return store_ ? store_->storeGeneration() : 0;
     }
     std::vector<re::scene::FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept override {
         return store_ ? store_->dirtyFieldsSince(lastGen) : std::vector<re::scene::FieldId>{};
     }
     void markDirty(uint64_t id, re::scene::FieldId field) noexcept override {
         if (store_) store_->markDirty(id, field);
     }

    private:
     std::shared_ptr<re::scene::SceneStore> store_;
};

/// Adapter: ViewStore as IDirtyTracker (shared ownership — see SceneStoreTracker).
class ViewStoreTracker final : public IDirtyTracker {
    public:
     explicit ViewStoreTracker(std::shared_ptr<re::scene::ViewStore> store)
         : store_(std::move(store)) {}
     uint64_t storeGeneration() const noexcept override {
         return store_ ? store_->storeGeneration() : 0;
     }
     std::vector<re::scene::FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept override {
         return store_ ? store_->dirtyFieldsSince(lastGen) : std::vector<re::scene::FieldId>{};
     }
     void markDirty(uint64_t id, re::scene::FieldId field) noexcept override {
         if (store_) store_->markDirty(id, field);
     }

    private:
     std::shared_ptr<re::scene::ViewStore> store_;
};

} // namespace re::broker
