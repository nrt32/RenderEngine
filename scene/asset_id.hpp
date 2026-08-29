#pragma once

// scene/asset_id.hpp — Stable asset identity for V3.6 (T7).
//
// SceneStore-owned AssetId{generation,contentHash} per data::Mesh /
// VolumeDataset / Image. Hash is of canonical stable bytes, not pointer
// (content-addressed dedup). Identical byte contents dedup to the same
// AssetId even when the CPU object lives at different addresses.
//
// data::Mesh stays pure — no AssetId field. SceneStore owns the mapping.
//
// Hash provenance: the FNV-1a byte-hash algorithm has exactly ONE definition,
// in the GL-free header data/content_hash.hpp (both the app-side scene layer
// and the RE-side render asset store must agree on an asset's content
// identity, and neither may include the other — data/ is the shared GL-free
// root). The re::scene functions below are thin forwarders kept so existing
// scene-side callers keep their qualified spelling; the render-side registry
// calls the re::data overloads directly.

#include <cstddef>
#include <cstdint>
#include <functional>

#include "data/content_hash.hpp"
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
/// Thin forwarder to the single definition in data/content_hash.hpp (see the
/// file-header hash-provenance note): one algorithm, shared by scene and
/// render asset identity.
inline uint64_t hashStableBytes(const void* /*borrow*/ data, std::size_t size) noexcept { // @note lifetime: borrowed — owned by caller, valid for duration of call
    return data::hashStableBytes(data, size);
}

/// Content hash for a Mesh — stable bytes are positions + indices (not pointer).
///
/// Forwarder to the single definition in data/content_hash.hpp: two Mesh
/// allocations with identical vertex/index bytes produce identical hashes
/// regardless of heap address.
inline uint64_t computeContentHash(const data::Mesh& mesh) noexcept {
    return data::computeContentHash(mesh);
}

/// Content hash for a VolumeDataset — dims + voxel floats. Forwarder to the
/// single definition in data/content_hash.hpp.
inline uint64_t computeContentHash(const data::VolumeDataset& vol) noexcept {
    return data::computeContentHash(vol);
}

/// Content hash for an Image — dims + channels + pixel bytes. Forwarder to
/// the single definition in data/content_hash.hpp.
inline uint64_t computeContentHash(const data::Image& img) noexcept {
    return data::computeContentHash(img);
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
