// broker/asset_store.cpp — broker AssetStore generational handles (T3 skeleton, T7 content-hash).

#include "broker/asset_store.hpp"

#include <string>

namespace re::broker {

namespace {
constexpr int kIndexOutOfRangeCode = 1;
constexpr int kGenerationMismatchCode = 2;
constexpr int kFreedSlotCode = 3;
} // namespace

data::Result<BrokerAssetHandle> AssetStore::registerAsset(const data::Mesh& mesh) {
    auto it = byObject_.find(&mesh);
    if (it != byObject_.end()) {
        return data::makeValue<BrokerAssetHandle>(it->second);
    }
    size_t idx = 0;
    if (!freeIndices_.empty()) {
        idx = freeIndices_.back();
        freeIndices_.pop_back();
        Slot& s = slots_[idx];
        s.cpuObject = &mesh;
        s.live = true;
        ++s.generation;
    } else {
        Slot s;
        s.cpuObject = &mesh;
        s.generation = 1u;
        s.live = true;
        idx = slots_.size();
        slots_.push_back(s);
    }
    ++liveCount_;
    BrokerAssetHandle h{static_cast<uint32_t>(idx), slots_[idx].generation};
    byObject_.emplace(&mesh, h);
    return data::makeValue<BrokerAssetHandle>(h);
}

data::Result<const data::Mesh*> AssetStore::resolve(
    const BrokerAssetHandle& h) const {
    if (h.index >= slots_.size()) {
        return data::makeError<const data::Mesh*>(
            kIndexOutOfRangeCode,
            "AssetStore: handle index " + std::to_string(h.index) + " out of range");
    }
    const Slot& s = slots_[h.index];
    if (s.generation != h.generation) {
        return data::makeError<const data::Mesh*>(
            kGenerationMismatchCode,
            "AssetStore: stale handle (generation " + std::to_string(h.generation) +
                " != slot generation " + std::to_string(s.generation) + ")");
    }
    if (!s.live) {
        return data::makeError<const data::Mesh*>(kFreedSlotCode,
                                                  "AssetStore: handle references freed slot");
    }
    return data::makeValue<const data::Mesh*>(s.cpuObject);
}

data::Result<void> AssetStore::unregister(const BrokerAssetHandle& h) {
    if (h.index >= slots_.size()) {
        return data::makeError<void>(kIndexOutOfRangeCode, "AssetStore: index out of range");
    }
    Slot& s = slots_[h.index];
    if (s.generation != h.generation) {
        return data::makeError<void>(kGenerationMismatchCode, "AssetStore: stale handle");
    }
    if (!s.live) {
        return data::makeError<void>(kFreedSlotCode, "AssetStore: freed slot");
    }
    if (s.cpuObject) byObject_.erase(s.cpuObject);
    s.cpuObject = nullptr;
    s.live = false;
    ++s.generation;
    freeIndices_.push_back(h.index);
    --liveCount_;
    return data::Result<void>(data::value);
}

} // namespace re::broker
