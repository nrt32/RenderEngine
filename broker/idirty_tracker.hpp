#pragma once

// broker/idirty_tracker.hpp — IDirtyTracker + IJobExecutor skeletons (SPEC §10.4, V3.5 T6).
//
// Hybrid poll+push dirty wiring via IDirtyTracker abstraction (DIP):
// ViewSynchronizer depends on IDirtyTracker, not concrete SceneStore/ViewStore.
// Future ThreadPoolExecutor can batch invalidations without editing synchronizer.
// IJobExecutor with inline synchronous fallback keeps ASan/UBSan 1-thread determinism.
//
// Pure interfaces, header-only, GL-free.

#include <cstdint>
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

/// Job executor abstraction for OCP threading (SPEC §10.4, NFR §5).
///
/// Header-only concept with inline synchronous fallback (execute(f){f();})
/// keeps ASan/UBSan 1-thread determinism; future ThreadPoolExecutor (V4)
/// injected without editing broker/scene (OCP via parallelFor).
class IJobExecutor {
   public:
    virtual ~IJobExecutor() = default;
    virtual void execute(void (*fn)(void*), void* ctx) = 0;
    virtual void parallelFor(std::size_t n, void (*fn)(std::size_t, void*), void* ctx) = 0;
};

/// Inline synchronous fallback (zero threads, deterministic).
class InlineJobExecutor final : public IJobExecutor {
    public:
     void execute(void (*fn)(void*), void* ctx) override { fn(ctx); }
     void parallelFor(std::size_t n, void (*fn)(std::size_t, void*), void* ctx) override {
         for (std::size_t i = 0; i < n; ++i) fn(i, ctx);
     }
};

/// Adapter: SceneStore as IDirtyTracker (DIP — broker depends on abstraction, store is detail).
///
/// SceneStore itself stays broker-free (scene/ never includes broker/); the adapter
/// lives in broker/ and translates SceneStore's concrete dirty API to IDirtyTracker.
class SceneStoreTracker final : public IDirtyTracker {
    public:
     explicit SceneStoreTracker(const re::scene::SceneStore* store) : store_(store) {}
     uint64_t storeGeneration() const noexcept override {
         return store_ ? store_->storeGeneration() : 0;
     }
     std::vector<re::scene::FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept override {
         return store_ ? store_->dirtyFieldsSince(lastGen) : std::vector<re::scene::FieldId>{};
     }
     void markDirty(uint64_t id, re::scene::FieldId field) noexcept override {
         if (store_) const_cast<re::scene::SceneStore*>(store_)->markDirty(id, field);
     }

    private:
     const re::scene::SceneStore* store_;
};

/// Adapter: ViewStore as IDirtyTracker.
class ViewStoreTracker final : public IDirtyTracker {
    public:
     explicit ViewStoreTracker(const re::scene::ViewStore* store) : store_(store) {}
     uint64_t storeGeneration() const noexcept override {
         return store_ ? store_->storeGeneration() : 0;
     }
     std::vector<re::scene::FieldId> dirtyFieldsSince(uint64_t lastGen) const noexcept override {
         return store_ ? store_->dirtyFieldsSince(lastGen) : std::vector<re::scene::FieldId>{};
     }
     void markDirty(uint64_t id, re::scene::FieldId field) noexcept override {
         if (store_) const_cast<re::scene::ViewStore*>(store_)->markDirty(id, field);
     }

    private:
     const re::scene::ViewStore* store_;
};

} // namespace re::broker
