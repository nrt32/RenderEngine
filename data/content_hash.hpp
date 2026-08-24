#pragma once

// data/content_hash.hpp — content hashing of stable asset bytes (GL-free).
//
// ONE definition of the FNV-1a 64-bit byte-hash used by BOTH consumers of an
// asset's content identity:
//   - `scene/` (app side): SceneStore/AssetId dedup — identical bytes alias to
//     one AssetId regardless of heap address (SPEC §7 T7);
//   - `render/` (RE side): AssetRegistry GPU-object dedup — identical bytes
//     upload to the GPU once (mesh kind SPEC §9 V2.5; volume/image kinds T14).
//
// render/ must not include scene/ and scene/ must stay RE-free, so the shared
// algorithm lives here in data/ (both layers already include data/). The hash
// is of CANONICAL STABLE BYTES, never pointers, so two distinct allocations
// with identical bytes produce the same value; component-wise float feeding
// avoids glm::vec3 padding variance. Values are deterministic across runs,
// which is what makes cross-layer identity assertions possible.

#include <cstddef>
#include <cstdint>

#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"

namespace re::data {

/// FNV-1a 64-bit hash of raw stable bytes (deterministic,
/// pointer-independent): basis 1469598103934665603, prime 1099511628211.
/// Skeleton for SHA-256-of-canonical-bytes per SPEC §10.1 — truncated to 64
/// bits for the gates; hashed at load/register time, never per frame.
inline uint64_t hashStableBytes(const void* data, std::size_t size) noexcept {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint64_t h = 1469598103934665603ULL; // FNV offset basis
    for (std::size_t i = 0; i < size; ++i) {
        h ^= static_cast<uint64_t>(bytes[i]);
        h *= 1099511628211ULL; // FNV prime
    }
    return h;
}

/// Content hash for a Mesh — stable bytes are positions + indices (not
/// pointer).
///
/// Two Mesh allocations with identical vertex/index bytes produce identical
/// hashes regardless of heap address. Uses component-wise hashing to avoid
/// glm::vec3 padding variance (12 bytes logical per vertex); the position and
/// index counts are fed first so empty vs non-empty inputs separate cleanly.
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

/// Content hash for a VolumeDataset — dims as u32, then the voxel floats.
inline uint64_t computeContentHash(
    const data::VolumeDataset& vol) noexcept {
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

/// Content hash for an Image — w/h/channels as i32, then the pixel bytes.
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

} // namespace re::data
