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

// FNV-1a hash of Mesh stable bytes (mirrors scene::computeContentHash, but
// render must not include scene/ per disposition_render — duplicate locally).
uint64_t meshContentHash(const data::Mesh& mesh) noexcept {
    uint64_t h = 1469598103934665603ULL;
    uint64_t vCount = static_cast<uint64_t>(mesh.positions().size());
    uint64_t iCount = static_cast<uint64_t>(mesh.indices().size());
    const uint8_t* cb = reinterpret_cast<const uint8_t*>(&vCount);
    for (std::size_t i = 0; i < sizeof(vCount); ++i) {
        h ^= static_cast<uint64_t>(cb[i]);
        h *= 1099511628211ULL;
    }
    cb = reinterpret_cast<const uint8_t*>(&iCount);
    for (std::size_t i = 0; i < sizeof(iCount); ++i) {
        h ^= static_cast<uint64_t>(cb[i]);
        h *= 1099511628211ULL;
    }
    for (const auto& p : mesh.positions()) {
        const uint8_t* xb = reinterpret_cast<const uint8_t*>(&p.x);
        for (std::size_t i = 0; i < sizeof(float); ++i) {
            h ^= static_cast<uint64_t>(xb[i]);
            h *= 1099511628211ULL;
        }
        const uint8_t* yb = reinterpret_cast<const uint8_t*>(&p.y);
        for (std::size_t i = 0; i < sizeof(float); ++i) {
            h ^= static_cast<uint64_t>(yb[i]);
            h *= 1099511628211ULL;
        }
        const uint8_t* zb = reinterpret_cast<const uint8_t*>(&p.z);
        for (std::size_t i = 0; i < sizeof(float); ++i) {
            h ^= static_cast<uint64_t>(zb[i]);
            h *= 1099511628211ULL;
        }
    }
    for (uint32_t idx : mesh.indices()) {
        const uint8_t* ib = reinterpret_cast<const uint8_t*>(&idx);
        for (std::size_t i = 0; i < sizeof(uint32_t); ++i) {
            h ^= static_cast<uint64_t>(ib[i]);
            h *= 1099511628211ULL;
        }
    }
    return h;
}

} // namespace

data::Result<AssetHandle> AssetRegistry::registerAsset(const data::Mesh& mesh) {
    // T7 content-hash dedup (primary): identical bytes alias even for distinct
    // pointers. Pointer-identity byObject_ is retained as dual-key shim (V3.6)
    // for diagnostics; hash is the binding key from SceneStore::AssetId.
    const uint64_t hash = meshContentHash(mesh);
    auto hashIt = byHash_.find(hash);
    if (hashIt != byHash_.end()) {
        const AssetHandle& existing = hashIt->second;
        if (existing.index < slots_.size()) {
            const Slot& s = slots_[existing.index];
            if (s.generation == existing.generation && s.geometry &&
                s.contentHash == hash) {
                return data::makeValue<AssetHandle>(existing);
            }
        }
    }
    // Fallback pointer shim (dual-key) — same pointer dedup if hash not yet hit.
    const auto existing = byObject_.find(&mesh);
    if (existing != byObject_.end()) {
        // If pointer hit but hash differs (reused memory with different content),
        // treat as new asset — hash check above would have missed, so fall through
        // only when hash differs: pointer reuse is rare, so keep shim.
        // For same content, hash hit already returned above, so this is redundant.
        auto hitHash = meshContentHash(*existing->first);
        if (hitHash == hash) {
            return data::makeValue<AssetHandle>(existing->second);
        }
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
        slot.contentHash = hash;
        ++slot.generation;
    } else {
        // Fresh slot: generation starts at 1 (0 is the never-allocated/null
        // handle marker).
        Slot slot;
        slot.geometry = std::make_unique<MeshGeometry>(std::move(*geometry));
        slot.cpuObject = &mesh;
        slot.contentHash = hash;
        ++slot.generation;
        index = slots_.size();
        slots_.push_back(std::move(slot));
    }

    ++liveCount_;
    const AssetHandle handle{static_cast<std::uint32_t>(index),
                             slots_[index].generation};
    byObject_.emplace(&mesh, handle);
    byHash_[hash] = handle;
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
    byHash_.erase(slot.contentHash);
    slot.geometry.reset(); // destroys the GPU object
    slot.cpuObject = nullptr;
    slot.contentHash = 0u;
    ++slot.generation; // every outstanding handle to this slot is now stale
    freeIndices_.push_back(handle.index);
    --liveCount_;
    return data::Result<void>(data::value);
}

} // namespace re::render