#pragma once

// scene/composite_key.hpp — CompositeKey skeleton for persistence (SPEC §10.1, V3.2a T2).
//
// Content-addressed persistence key: persistence is by CompositeKey{Version,LayoutId,Id,Gen,Hash},
// not by id alone or size. Version prevents aliasing across schema evolution (EOL cache-key
// versioning per Software Patterns Lexicon + Dev Genius version-your-cache-keys 2025-12-25).
// Hash is SHA-256-of-stable-bytes truncated to 64-bit (FNV-1a canonicalization here as
// skeleton; stable bytes, not pointer). Two identical byte arrays produce identical hash
// regardless of allocation address — hash of stable bytes, not pointer.
//
// Pure value type, header-only, GL-free, RE-free. Links only to standard library.
// No behavior change yet — unblocks T3(broker) / T5(View/ReView) / T6(persistence full).

#include <cstddef>
#include <cstdint>
#include <functional>

namespace re::scene {

/// Composite persistence key — value type (SPEC §10.1).
///
/// Fields:
/// - Version: persistence schema version (bump invalidates entire cache)
/// - LayoutId: owning layout/page scope (prevents aliasing same ViewId across layouts)
/// - Id: stable handle (ViewId or ObjectId)
/// - Gen: per-field generation (per SPEC §10.4 per-field split)
/// - Hash: content hash of canonicalized stable bytes (not pointer address)
struct CompositeKey {
    /// Schema version — bump when Re* field inventory or hash algorithm changes.
    uint32_t version{1};
    /// Owning layout/page scope.
    uint64_t layoutId{0};
    /// Stable handle id (ViewId or ObjectId from SceneStore/ViewStore).
    uint64_t id{0};
    /// Per-field generation at time of caching.
    uint64_t gen{0};
    /// Content hash of canonical stable bytes (FNV-1a 64-bit of canonicalized field bytes).
    uint64_t hash{0};

    /// Equality — all fields must match (explainable invariant: key is composite).
    bool operator==(const CompositeKey& o) const noexcept {
        return version == o.version && layoutId == o.layoutId && id == o.id &&
               gen == o.gen && hash == o.hash;
    }
    bool operator!=(const CompositeKey& o) const noexcept { return !(*this == o); }

    /// Hash stable bytes via FNV-1a 64-bit (deterministic, pointer-independent).
    ///
    /// FNV-1a 64-bit basis/prime per spec (canonicalization: normalize bytes before hash;
    /// pointer address never enters the hash — only stable field bytes).
    /// @param data Pointer to stable bytes (must be canonical, e.g. little-endian normalized).
    /// @param size Number of bytes.
    /// @return 64-bit hash (truncate of SHA-256 in spec; FNV here as deterministic skeleton).
    static uint64_t hashStableBytes(const void* data, std::size_t size) noexcept {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        uint64_t h = 1469598103934665603ULL; // FNV offset basis
        for (std::size_t i = 0; i < size; ++i) {
            h ^= static_cast<uint64_t>(bytes[i]);
            h *= 1099511628211ULL; // FNV prime
        }
        return h;
    }

    /// Convenience: hash a typed trivially-copyable value's canonical bytes.
    template <typename T>
    static uint64_t hashValue(const T& v) noexcept {
        static_assert(std::is_trivially_copyable_v<T>, "hashValue requires trivially-copyable");
        return hashStableBytes(&v, sizeof(T));
    }
};

/// Hash functor for unordered_map<CompositeKey, ...>.
struct CompositeKeyHash {
    std::size_t operator()(const CompositeKey& k) const noexcept {
        // Combine fields via FNV-style mixing (boost hash_combine analogue).
        std::size_t h = static_cast<std::size_t>(k.version);
        auto combine = [&](uint64_t v) {
            h ^= std::hash<uint64_t>{}(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        combine(k.layoutId);
        combine(k.id);
        combine(k.gen);
        combine(k.hash);
        return h;
    }
};

} // namespace re::scene

// std::hash specialization for unordered_map ergonomics.
namespace std {
template <>
struct hash<re::scene::CompositeKey> {
    std::size_t operator()(const re::scene::CompositeKey& k) const noexcept {
        return re::scene::CompositeKeyHash{}(k);
    }
};
} // namespace std
