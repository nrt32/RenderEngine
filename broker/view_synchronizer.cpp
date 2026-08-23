// broker/view_synchronizer.cpp — ViewSynchronizer skeleton (no GL, poll early-out).

#include "broker/view_synchronizer.hpp"

#include <span>

namespace re::broker {

data::Result<void> ViewSynchronizer::sync(std::span<const scene::View> /*views*/,
                                          const scene::SceneStore& scene) {
    const uint64_t gen = scene.storeGeneration();
    if (gen == lastStoreGen_) {
        // Early-out: no field changed since last sync (hybrid poll per §10.4).
        return data::Result<void>(data::value);
    }
    // Bounded scan placeholder: for T3 we just update lastStoreGen.
    // Full dirtyFieldsSince + per-field ICachedMapper::mapCached lands in T6.
    lastStoreGen_ = gen;
    return data::Result<void>(data::value);
}

} // namespace re::broker
