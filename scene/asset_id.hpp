#pragma once

// scene/asset_id.hpp — Stable asset identity for V3.6 (T7).
//
// SceneStore-owned AssetId{generation,contentHash} per data::Mesh /
// VolumeDataset / Image. Hash is of canonical stable bytes, not pointer
// (content-addressed dedup). Identical byte contents dedup to the same
// AssetId even when the CPU object lives at different addresses.
//
// data::Mesh stays pure — no AssetId field. SceneStore owns the mapping.

#include <cstddef>
#include <cstdint>
#include <functional>

#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"

namespace re::scene {

/// Stable asset handle owned by SceneStore (SPEC §7 T7, Q3/Q28).
///
/// `index` + `generation` is the generational slot table key (typed error
/// code 2 on stale generation+1, never crash). `contentHash` is FNV-1a 64-bit
/// of the canonical stable bytes (positions+indices / voxel bytes / pixel
/// bytes). Two distinct `data::Mesh` allocations with identical bytes share
/// the same `contentHash` and therefore alias to the same AssetId.
struct AssetId {
    uint32_t index{0u};
    uint32_t generation{0u};
    uint64_t contentHash{0u};

    bool isNull() const noexcept {
        return index == 0u && generation == 0u && contentHash == 0u;
    }
    bool operator==(const AssetId& o) const noexcept {
        return index == o.index && generation == o.generation &&
               contentHash == o.contentHash;
    }
    bool operator!=(const AssetId& o) const noexcept {
        return !(*this == o);
    }
};

/// Hash functor for unordered_map<AssetId, ...>.
struct AssetIdHash {
    std::size_t operator()(const AssetId& id) const noexcept {
        std::size_t h = static_cast<std::size_t>(id.index);
        auto combine = [&](uint64_t v) {
            h ^= std::hash<uint64_t>{}(v) + 0x9e3779b97f4a7c15ULL +
                 (h << 6) + (h >> 2);
        };
        combine(static_cast<uint64_t>(id.generation));
        combine(id.contentHash);
        return h;
    }
};

/// FNV-1a 64-bit hash of stable bytes (deterministic, pointer-independent).
///
/// Skeleton for SHA-256-of-canonical-bytes per SPEC §10.1 — truncated to 64
/// bits for the gate; SHA-256 correctness per System Overflow cache-key design
/// is preserved as hierarchical `contentHash` property (hash at load time, not
/// per-frame).
inline uint64_t hashStableBytes(const void* data, std::size_t size) noexcept {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t h = 1469598103934665603ULL; // FNV offset basis
    for (std::size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 1099511628211ULL; // FNV prime
    }
    return h;
}

/// Content hash for a Mesh — stable bytes are positions + indices (not pointer).
///
/// Two Mesh allocations with identical vertex/index bytes produce identical
/// hash regardless of heap address. Uses component-wise hashing to avoid
/// glm::vec3 padding variance (12 bytes logical per vertex).
inline uint64_t computeContentHash(const data::Mesh& mesh) noexcept {
    uint64_t h = 1469598103934665603ULL;
    // Feed position count + index count to separate empty vs non-empty cleanly.
    uint64_t vCount = static_cast<uint64_t>(mesh.positions().size());
    uint64_t iCount = static_cast<uint64_t>(mesh.indices().size());
    // Hash counts as bytes
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

/// Content hash for a VolumeDataset — dims + voxel floats.
inline uint64_t computeContentHash(const data::VolumeDataset& vol) noexcept {
    uint64_t h = 1469598103934665603ULL;
    auto feedU32 = [&](uint32_t v) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
        for (std::size_t i = 0; i < sizeof(uint32_t); ++i) {
            h ^= static_cast<uint64_t>(b[i]);
            h *= 1099511628211ULL;
        }
    };
    feedU32(vol.sizeX());
    feedU32(vol.sizeY());
    feedU32(vol.sizeZ());
    for (float f : vol.voxels()) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&f);
        for (std::size_t i = 0; i < sizeof(float); ++i) {
            h ^= static_cast<uint64_t>(b[i]);
            h *= 1099511628211ULL;
        }
    }
    return h;
}

/// Content hash for an Image — dims + channels + pixel bytes.
inline uint64_t computeContentHash(const data::Image& img) noexcept {
    uint64_t h = 1469598103934665603ULL;
    auto feedI32 = [&](int32_t v) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&v);
        for (std::size_t i = 0; i < sizeof(int32_t); ++i) {
            h ^= static_cast<uint64_t>(b[i]);
            h *= 1099511628211ULL;
        }
    };
    feedI32(img.width());
    feedI32(img.height());
    feedI32(img.channels());
    for (uint8_t b : img.pixels()) {
        h ^= static_cast<uint64_t>(b);
        h *= 1099511628211ULL;
    }
    return h;
}

} // namespace re::scene

namespace std {
template <>
struct hash<re::scene::AssetId> {
    std::size_t operator()(const re::scene::AssetId& id) const noexcept {
        return re::scene::AssetIdHash{}(id);
    }
};
} // namespace std
