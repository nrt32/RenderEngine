#pragma once

// scene/composite_key.hpp — CompositeKey for persistence (SPEC §10.1, V3.2a T2, T12 SHA-256 prod).
//
// Content-addressed persistence key: persistence is by CompositeKey{Version,LayoutId,Id,Gen,Hash},
// not by id alone or size. Version prevents aliasing across schema evolution: bump it whenever
// the persisted field inventory or hash layout changes, and every key minted under the old
// schema stops matching (old cache entries become unreachable instead of mis-decoded).
// Hash is SHA-256-of-canonical-stable-bytes truncated to 64-bit (FNV-1a skeleton at T7, SHA-256
// prod at T12 via data/content_hash.hpp:31 source of truth, hierarchical
// Version:LayoutId:Type:Hash determinism across runs). Two identical byte arrays produce
// identical hash regardless of allocation address — hash of stable bytes, not pointer, LE via
// memcpy+htole32 with NaN canonicalized (-NaN→NaN).
//
// Pure value type, header-only, GL-free, RE-free. Links only to standard library.
// No behavior change yet — unblocks T3(broker) / T5(View/ReView) / T6(persistence full).

#include <cstddef>
#include <cstdint>
#include <functional>

#include "data/content_hash.hpp"

namespace re::scene {

/// Composite persistence key — value type (SPEC §10.1, V3.5 T6 full).
///
/// Fields (SPEC §10.1 hierarchical Version:LayoutId:Type:Hash):
/// - Version: persistence schema version (bump invalidates entire cache)
/// - LayoutId: owning layout/page scope (prevents aliasing same ViewId across layouts)
/// - Id: stable handle (ViewId or ObjectId)
/// - Gen: per-field generation (per SPEC §10.4 per-field split)
/// - Hash: content hash of canonicalized stable bytes (not pointer address)
/// - TypeHash: hash of TypeIndex (stable type identity, added V3.5 to complete
///   CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash} per SPEC §10.1).
struct CompositeKey {
    /// Schema version — bump whenever the render-side field inventory or the
    /// hash algorithm changes, invalidating every cached key.
    uint32_t version{1};
    /// Owning layout/page scope.
    uint64_t layoutId{0};
    /// Stable handle id (ViewId or ObjectId from SceneStore/ViewStore).
    uint64_t id{0};
    /// Per-field generation at time of caching.
    uint64_t gen{0};
    /// Content hash of canonical stable bytes (SHA-256 truncated 64 of canonical LE field bytes).
    uint64_t hash{0};
    /// Stable type hash (std::type_index hash; 0 = "unspecified", the value
    /// early skeleton users wrote before the field existed — keeping 0 valid
    /// means their persisted keys still compare equal after the field was
    /// added).
    uint64_t typeHash{0};

    /// Equality requires EVERY field to match: a key is a composite identity,
    /// and two keys differing in any single component (schema version, layout
    /// scope, handle, generation, content, or type) denote different cached
    /// state by definition.
    bool operator==(const CompositeKey& o) const noexcept {
        return version == o.version && layoutId == o.layoutId && id == o.id &&
               gen == o.gen && hash == o.hash && typeHash == o.typeHash;
    }
    bool operator!=(const CompositeKey& o) const noexcept { return !(*this == o); }

    /// Hash stable bytes via SHA-256 truncated 64 (deterministic, pointer-independent).
    ///
    /// SHA-256 of canonical little-endian bytes per SPEC §10.1 (FNV skeleton
    /// at T7, SHA-256 prod at T12). Delegates to the single source of truth
    /// data::hashStableBytes (data/content_hash.hpp:31) — hierarchical
    /// Version:LayoutId:Type:Hash determinism across runs. Pointer address
    /// never enters the hash — only stable field bytes, canonical LE via
    /// memcpy+htole32 and NaN payload canonicalized (-NaN→NaN).
    /// @param data Pointer to stable bytes (must be canonical, e.g. little-endian normalized).
    /// @param size Number of bytes.
    /// @return 64-bit hash (SHA-256 truncated to 64, LE interpretation of first 8 digest bytes).
    static uint64_t hashStableBytes(const void* /*borrow*/ data, std::size_t size) noexcept { // @note lifetime: borrowed — owned by caller, valid for duration of call
        return ::re::data::hashStableBytes(data, size);
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
        combine(k.typeHash);
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
