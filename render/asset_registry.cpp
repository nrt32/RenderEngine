// render/asset_registry.cpp — AssetRegistry implementation (SPEC §9 V2.5).

#include "render/asset_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace re::render {

namespace {

// The typed-error codes of the registry's handle-validation paths (SPEC §5).
constexpr int kIndexOutOfRangeCode = 1;
constexpr int kGenerationMismatchCode = 2;
constexpr int kFreedSlotCode = 3;

} // namespace

data::Result<AssetHandle> AssetRegistry::registerAsset(const data::Mesh& mesh) {
    // Dedup by CPU-object identity: one GPU object per individual CPU object
    // (SPEC §9 V2.5). Registering the same mesh again returns the existing
    // handle without uploading anything.
    const auto existing = byObject_.find(&mesh);
    if (existing != byObject_.end()) {
        return data::makeValue<AssetHandle>(existing->second);
    }

    // Upload first so a failure never mutates the slot table.
    auto geometry = MeshGeometry::create(mesh);
    if (geometry.failed()) {
        return data::makeError<AssetHandle>(geometry.error().code,
                                            geometry.error().message);
    }

    std::size_t index = 0u;
    if (!freeIndices_.empty()) {
        // Reuse a freed slot: its generation was bumped at free time and is
        // bumped again here, so every handle to the previous occupant stays
        // stale.
        index = freeIndices_.back();
        freeIndices_.pop_back();
        Slot& slot = slots_[index];
        slot.geometry = std::make_unique<MeshGeometry>(std::move(*geometry));
        slot.cpuObject = &mesh;
        ++slot.generation;
    } else {
        // Fresh slot: generation starts at 1 (0 is the never-allocated/null
        // handle marker).
        Slot slot;
        slot.geometry = std::make_unique<MeshGeometry>(std::move(*geometry));
        slot.cpuObject = &mesh;
        ++slot.generation;
        index = slots_.size();
        slots_.push_back(std::move(slot));
    }

    ++liveCount_;
    const AssetHandle handle{static_cast<std::uint32_t>(index),
                             slots_[index].generation};
    byObject_.emplace(&mesh, handle);
    return data::makeValue<AssetHandle>(handle);
}

data::Result<MeshGeometry*> AssetRegistry::resolve(const AssetHandle& handle) {
    if (handle.index >= slots_.size()) {
        return data::makeError<MeshGeometry*>(
            kIndexOutOfRangeCode, "AssetRegistry: handle index " +
                                      std::to_string(handle.index) +
                                      " out of range (slot table size " +
                                      std::to_string(slots_.size()) + ")");
    }
    const Slot& slot = slots_[handle.index];
    if (slot.generation != handle.generation) {
        return data::makeError<MeshGeometry*>(
            kGenerationMismatchCode,
            "AssetRegistry: stale handle (generation " +
                std::to_string(handle.generation) + " != slot generation " +
                std::to_string(slot.generation) + ")");
    }
    if (!slot.geometry) {
        return data::makeError<MeshGeometry*>(
            kFreedSlotCode, "AssetRegistry: handle references a freed slot");
    }
    return data::makeValue<MeshGeometry*>(slot.geometry.get());
}

data::Result<void> AssetRegistry::unregister(const AssetHandle& handle) {
    if (handle.index >= slots_.size()) {
        return data::makeError<void>(
            kIndexOutOfRangeCode, "AssetRegistry: handle index out of range");
    }
    Slot& slot = slots_[handle.index];
    if (slot.generation != handle.generation) {
        return data::makeError<void>(
            kGenerationMismatchCode,
            "AssetRegistry: stale handle (generation mismatch)");
    }
    if (!slot.geometry) {
        return data::makeError<void>(kFreedSlotCode,
                                     "AssetRegistry: handle references a freed "
                                     "slot");
    }
    if (slot.cpuObject != nullptr) {
        byObject_.erase(slot.cpuObject);
    }
    slot.geometry.reset(); // destroys the GPU object
    slot.cpuObject = nullptr;
    ++slot.generation; // every outstanding handle to this slot is now stale
    freeIndices_.push_back(handle.index);
    --liveCount_;
    return data::Result<void>(data::value);
}

} // namespace re::render