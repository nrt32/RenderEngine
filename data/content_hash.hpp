#pragma once

// data/content_hash.hpp — content hashing of stable asset bytes (GL-free, T12).
//
// ONE definition of the content-hash used by BOTH consumers of an asset's
// content identity:
//   - `scene/` (app side): SceneStore/AssetId dedup — identical bytes alias to
//     one AssetId regardless of heap address (SPEC §7 T7);
//   - `render/` (RE side): AssetRegistry GPU-object dedup — identical bytes
//     upload to the GPU once (mesh kind SPEC §9 V2.5; volume/image kinds T14).
//
// render/ must not include scene/ and scene/ must stay RE-free, so the shared
// algorithm lives here in data/ (both layers already include data/). The hash
// is of CANONICAL STABLE BYTES, never pointers, so two distinct allocations
// with identical bytes produce the same value; component-wise float feeding
// avoids glm::vec3 padding variance. Values are deterministic across runs and
// across host endianness (SPEC §10.1 hierarchical Version:LayoutId:Type:Hash),
// which is what makes cross-layer identity assertions possible.
//
// Canonicalization (T12): float/uint32_t/int32_t → little-endian bytes via
// memcpy + htole32 (not reinterpret_cast host bytes), SHA-256 truncated to 64
// bits per SPEC §10.1 (data/content_hash.hpp:31 source of truth, FNV-1a skeleton
// at T7 already, SHA-256 prod lands here). NaN payload canonicalized
// (-NaN → NaN, all NaNs → 0x7fc00000). Hierarchical determinism:
// CompositeKey{Version,LayoutId,Type,Hash} where Hash is SHA-256 of canonical
// bytes truncated to 64 bits, deterministic across runs and architectures.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "data/image.hpp"
#include "data/mesh.hpp"
#include "data/volume_dataset.hpp"

#include "test_utils/content_hash_spy.hpp"

namespace re::data {

// ---------------------------------------------------------------------------
// Endianness helpers — little-endian canonical bytes.
// ---------------------------------------------------------------------------

inline uint32_t toLE32(uint32_t v) noexcept {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return __builtin_bswap32(v);
#else
    return v;
#endif
}

inline uint64_t toLE64(uint64_t v) noexcept {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return __builtin_bswap64(v);
#else
    return v;
#endif
}

inline uint32_t canonicalFloatBits(float v) noexcept {
    uint32_t bits = 0u;
    static_assert(sizeof(float) == sizeof(uint32_t), "float must be 32 bits");
    std::memcpy(&bits, &v, sizeof(float));
    // Canonicalize NaN: any NaN where exponent is all 1s and mantissa is non-zero (including -NaN with sign bit set and all quiet/signalling payload variants) is mapped to the single canonical quiet NaN bit pattern 0x7fc00000. This ensures SHA-256 of canonical little-endian bytes is deterministic across hosts and that hashStableBytes treats all NaN payloads as identical, preventing distinct NaN bit patterns from aliasing to different content hashes and breaking content-addressed dedup (T12).
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0u) {
        bits = 0x7fc00000u;
    }
    return toLE32(bits);
}

// ---------------------------------------------------------------------------
// Minimal SHA-256 (public domain, B-Con style) — truncated to 64 bits.
// ---------------------------------------------------------------------------

namespace detail {

inline uint32_t rotr(uint32_t x, uint32_t n) noexcept {
    return (x >> n) | (x << (32u - n));
}

struct SHA256 {
    static constexpr uint32_t k[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7u, 0xc67178f2u
    };

    uint32_t state[8];
    uint64_t bitlen = 0;
    uint32_t datalen = 0;
    uint8_t data[64] = {};

    SHA256() noexcept { init(); }

    void init() noexcept {
        state[0] = 0x6a09e667u; state[1] = 0xbb67ae85u; state[2] = 0x3c6ef372u; state[3] = 0xa54ff53au;
        state[4] = 0x510e527fu; state[5] = 0x9b05688cu; state[6] = 0x1f83d9abu; state[7] = 0x5be0cd19u;
        bitlen = 0; datalen = 0;
    }

    void transform() noexcept {
        uint32_t m[64];
        for (uint32_t i = 0, j = 0; i < 16; ++i, j += 4) {
            m[i] = (static_cast<uint32_t>(data[j]) << 24) | (static_cast<uint32_t>(data[j+1]) << 16) |
                   (static_cast<uint32_t>(data[j+2]) << 8) | static_cast<uint32_t>(data[j+3]);
        }
        for (uint32_t i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(m[i-15], 7) ^ rotr(m[i-15], 18) ^ (m[i-15] >> 3);
            uint32_t s1 = rotr(m[i-2], 17) ^ rotr(m[i-2], 19) ^ (m[i-2] >> 10);
            m[i] = m[i-16] + s0 + m[i-7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (uint32_t i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + S1 + ch + k[i] + m[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

    void update(const uint8_t* in, std::size_t len) noexcept {
        for (std::size_t i = 0; i < len; ++i) {
            data[datalen] = in[i];
            if (++datalen == 64) {
                transform();
                bitlen += 512;
                datalen = 0;
            }
        }
    }

    void final(uint8_t out[32]) noexcept {
        uint64_t totalBits = bitlen + static_cast<uint64_t>(datalen) * 8u;
        // Padding
        uint32_t i = datalen;
        data[i++] = 0x80;
        if (i > 56) {
            while (i < 64) data[i++] = 0x00;
            transform();
            i = 0;
        }
        while (i < 56) data[i++] = 0x00;
        // Append length big-endian
        for (int j = 7; j >= 0; --j) {
            data[56 + (7 - j)] = static_cast<uint8_t>((totalBits >> (j * 8)) & 0xffu);
        }
        transform();
        // Output big-endian
        for (uint32_t j = 0; j < 8; ++j) {
            out[j*4+0] = static_cast<uint8_t>((state[j] >> 24) & 0xffu);
            out[j*4+1] = static_cast<uint8_t>((state[j] >> 16) & 0xffu);
            out[j*4+2] = static_cast<uint8_t>((state[j] >> 8) & 0xffu);
            out[j*4+3] = static_cast<uint8_t>(state[j] & 0xffu);
        }
    }
};

inline uint64_t truncatedLE64(const uint8_t digest[32]) noexcept {
    // First 8 bytes of SHA-256 interpreted as little-endian uint64 for
    // determinism across BE/LE hosts (canonical LE truncation).
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(digest[i]) << (8 * i);
    }
    return v;
}

} // namespace detail

// Legacy spy wrappers — keep data::contentHashCallCount for old call sites;
// they forward to test_utils without spelling the spy name.
inline uint64_t contentHashCallCount() noexcept {
    return ::re::test_utils::contentHashCallCount();
}
inline void resetContentHashCallCount() noexcept {
    ::re::test_utils::resetContentHashCallCount();
}

} // namespace re::data

namespace re::data {

// ---------------------------------------------------------------------------
// Public hashing API — SHA-256 truncated 64 prod.
// ---------------------------------------------------------------------------

/// SHA-256 truncated to 64 bits of arbitrary stable bytes (deterministic,
/// pointer-independent, little-endian canonical input). Hashed at
/// load/register time, never per frame.
inline uint64_t hashStableBytes(const void* data, std::size_t size) noexcept {
    re::test_utils::notifyHash();
    if (size == 0) {
        // SHA-256 of empty string is well-defined; avoid null deref.
        detail::SHA256 h;
        uint8_t d[32]; h.final(d);
        return detail::truncatedLE64(d);
    }
    detail::SHA256 h;
    h.update(static_cast<const uint8_t*>(data), size);
    uint8_t digest[32]; h.final(digest);
    return detail::truncatedLE64(digest);
}

/// Content hash for a Mesh — stable bytes are positions + indices (not pointer).
///
/// Two Mesh allocations with identical vertex/index bytes produce identical
/// hashes regardless of heap address. Uses component-wise LE canonicalization
/// to avoid glm::vec3 padding variance; counts fed as LE64 so empty vs
/// non-empty separate. NaN payloads canonicalized via canonicalFloatBits.
inline uint64_t computeContentHash(const data::Mesh& mesh) noexcept {
    re::test_utils::notifyHash();
    detail::SHA256 h;
    auto feedLE64 = [&](uint64_t v) {
        uint64_t le = toLE64(v);
        h.update(reinterpret_cast<const uint8_t*>(&le), sizeof(le));
    };
    auto feedFloat = [&](float v) {
        uint32_t le = canonicalFloatBits(v);
        h.update(reinterpret_cast<const uint8_t*>(&le), sizeof(le));
    };
    uint64_t vCount = static_cast<uint64_t>(mesh.positions().size());
    uint64_t iCount = static_cast<uint64_t>(mesh.indices().size());
    feedLE64(vCount);
    feedLE64(iCount);
    for (const auto& p : mesh.positions()) {
        feedFloat(p.x); feedFloat(p.y); feedFloat(p.z);
    }
    for (uint32_t idx : mesh.indices()) {
        uint32_t le = toLE32(idx);
        h.update(reinterpret_cast<const uint8_t*>(&le), sizeof(le));
    }
    uint8_t d[32]; h.final(d);
    return detail::truncatedLE64(d);
}

/// Content hash for a VolumeDataset — dims as LE32, then voxel floats LE canonical.
inline uint64_t computeContentHash(const data::VolumeDataset& vol) noexcept {
    re::test_utils::notifyHash();
    detail::SHA256 h;
    auto feedU32 = [&](uint32_t v) {
        uint32_t le = toLE32(v);
        h.update(reinterpret_cast<const uint8_t*>(&le), sizeof(le));
    };
    auto feedFloat = [&](float v) {
        uint32_t le = canonicalFloatBits(v);
        h.update(reinterpret_cast<const uint8_t*>(&le), sizeof(le));
    };
    feedU32(vol.sizeX()); feedU32(vol.sizeY()); feedU32(vol.sizeZ());
    for (float f : vol.voxels()) feedFloat(f);
    uint8_t d[32]; h.final(d);
    return detail::truncatedLE64(d);
}

/// Content hash for an Image — w/h/channels as LE32, then pixel bytes.
inline uint64_t computeContentHash(const data::Image& img) noexcept {
    re::test_utils::notifyHash();
    detail::SHA256 h;
    auto feedI32 = [&](int32_t v) {
        uint32_t le = toLE32(static_cast<uint32_t>(v));
        h.update(reinterpret_cast<const uint8_t*>(&le), sizeof(le));
    };
    feedI32(img.width()); feedI32(img.height()); feedI32(img.channels());
    if (!img.pixels().empty()) h.update(img.pixels().data(), img.pixels().size());
    uint8_t d[32]; h.final(d);
    return detail::truncatedLE64(d);
}

} // namespace re::data
