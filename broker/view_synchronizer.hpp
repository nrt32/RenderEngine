#pragma once

// broker/view_synchronizer.hpp — ViewSynchronizer (SPEC §11 V3.2b T3).
//
// SRP: single responsibility is poll SceneStore generations / contentHash and
// drive ICachedMapper::mapCached (owns CompositeKey cache). Does not dispatch
// rendering (that's ViewCompositor). Broker owns mapper registry (type_index),
// ViewSynchronizer owns generation/contentHash cache (SRP split per §11.3.1).

#include <span>

#include "broker/broker.hpp"
#include "data/result.hpp"
#include "scene/composite_key.hpp"
#include "scene/store.hpp"
#include "scene/view.hpp"

namespace re::broker {

/// View synchronizer — cache/dirty side of IViewBridge (SRP via composition).
///
/// Polls SceneStore::storeGeneration() as early-out, then iterates
/// dirtyFieldsSince(lastStoreGen) bounded set (hybrid poll+push per §10.4,
/// unblocks T6). For T3 the implementation is a skeleton that drives
/// ICachedMapper::mapCached for known mappers without recreating ReView identity.
class ViewSynchronizer {
   public:
    explicit ViewSynchronizer(Broker* broker) : broker_(broker) {}

    /// Sync views + scene: poll storeGeneration, drive cached mappers.
    data::Result<void> sync(std::span<const scene::View> views,
                            const scene::SceneStore& scene);

    /// For test: last synced store generation.
    uint64_t lastStoreGen() const noexcept { return lastStoreGen_; }

   private:
    Broker* broker_;
    uint64_t lastStoreGen_{0};
};

} // namespace re::broker
