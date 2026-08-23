// broker/asset_store.cpp — broker AssetStore generational handles (T3 skeleton, T7 content-hash).

#include "broker/asset_store.hpp"

#include <string>

namespace re::broker {

namespace {
constexpr int kIndexOutOfRangeCode = 1;
constexpr int kGenerationMismatchCode = 2;
constexpr int kFreedSlotCode = 3;
} // namespace

data::Result<BrokerAssetHandle> AssetStore::registerAsset(SharedMesh mesh) {
    if (!mesh) {
        return data::makeError<BrokerAssetHandle>(
            4, "AssetStore: null mesh shared_ptr");
    }
    const uint64_t hash = scene::computeContentHash(*mesh);
    auto hit = byHash_.find(hash);
    if (hit != byHash_.end()) {
        const BrokerAssetHandle& existing = hit->second;
        if (existing.index < slots_.size()) {
            const Slot& s = slots_[existing.index];
            if (s.live && s.generation == existing.generation &&
                s.contentHash == hash) {
                return data::makeValue<BrokerAssetHandle>(existing);
            }
        }
    }
    // Pointer shim second check (dual-key diagnostic, not dedup key — hash is primary).
    auto it = byObject_.find(mesh.get());
    if (it != byObject_.end()) {
        // Pointer hit but hash mismatch means different content at same address
        // (reused memory) — treat as new; otherwise hash hit already returned.
        if (scene::computeContentHash(*it->first) == hash) {
            return data::makeValue<BrokerAssetHandle>(it->second);
        }
    }
    size_t idx = 0;
    const data::Mesh* /*borrow*/ raw = mesh.get(); // byObject_ diagnostic key;
    // borrow of the bytes this store's Slot::cpuObject (shared_ptr below)
    // co-owns — the key lives exactly as long as the slot (erased on
    // unregister).
    if (!freeIndices_.empty()) {
        idx = freeIndices_.back();
        freeIndices_.pop_back();
        Slot& s = slots_[idx];
        s.cpuObject = std::move(mesh);
        s.contentHash = hash;
        s.live = true;
        ++s.generation;
    } else {
        Slot s;
        s.cpuObject = std::move(mesh);
        s.contentHash = hash;
        s.generation = 1u;
        s.live = true;
        idx = slots_.size();
        slots_.push_back(std::move(s));
    }
    ++liveCount_;
    BrokerAssetHandle h{static_cast<uint32_t>(idx), slots_[idx].generation};
    byObject_.emplace(raw, h);
    byHash_[hash] = h;
    return data::makeValue<BrokerAssetHandle>(h);
}

data::Result<AssetStore::SharedMesh> AssetStore::resolve(
    const BrokerAssetHandle& h) const {
    if (h.index >= slots_.size()) {
        return data::makeError<SharedMesh>(
            kIndexOutOfRangeCode,
            "AssetStore: handle index " + std::to_string(h.index) + " out of range");
    }
    const Slot& s = slots_[h.index];
    if (s.generation != h.generation) {
        return data::makeError<SharedMesh>(
            kGenerationMismatchCode,
            "AssetStore: stale handle (generation " + std::to_string(h.generation) +
                " != slot generation " + std::to_string(s.generation) + ")");
    }
    if (!s.live) {
        return data::makeError<SharedMesh>(kFreedSlotCode,
                                                  "AssetStore: handle references freed slot");
    }
    return data::makeValue<SharedMesh>(s.cpuObject);
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
    if (s.cpuObject) byObject_.erase(s.cpuObject.get());
    byHash_.erase(s.contentHash);
    s.cpuObject.reset(); // releases the store's shared reference; other
                         // co-owners keep the bytes alive
    s.live = false;
    ++s.generation;
    freeIndices_.push_back(h.index);
    --liveCount_;
    return data::Result<void>(data::value);
}

} // namespace re::broker
