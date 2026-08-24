#pragma once

// broker/stable_key.hpp — the ONE ReView identity key shared by the
// ViewCompositor (owner of the ReView map) and the ViewSynchronizer (owner of
// the per-view dirty caches). Before this header the two collaborators each
// declared their own divergent twin — the compositor's carried a schema
// `version`, the synchronizer's did not — so the same logical view could be
// keyed differently depending on which file built the key. Unifying them into
// a single value type makes ReView identity a one-definition contract:
// `version` guards against aliasing across key-semantics changes (the same
// policy as CompositeKey's Version field), `layoutId` scopes keys per
// layout/page so two layouts may hold different views under the same id, and
// `viewId` is the app-side stable view handle. Equality compares every field,
// because a key differing in any component denotes different cached render
// state by definition.
//
// Header-only value type, GL-free, RE-free (persistence-honesty follow-up).

#include <cstdint>
#include <functional>

namespace re::broker {

/// Stable identity of one ReView: {schema version, layout scope, view handle}.
struct StableKey {
    /// Key-semantics version — bump whenever the meaning of the fields below
    /// changes so old entries become unreachable instead of mis-read.
    uint32_t version{1};
    /// Owning layout/page scope (prevents cross-layout aliasing).
    uint64_t layoutId{0};
    /// App-side stable view handle (scene View id).
    uint64_t viewId{0};

    /// Identity requires EVERY field to match.
    bool operator==(const StableKey& o) const noexcept {
        return version == o.version && layoutId == o.layoutId &&
               viewId == o.viewId;
    }
    bool operator!=(const StableKey& o) const noexcept {
        return !(*this == o);
    }
};

/// Mint a key under the CURRENT schema version. The version literal lives
/// exactly here so every collaborator (compositor map lookups, synchronizer
/// cache entries) stamps the same one — bumping key semantics means changing
/// this single line plus the field meanings above, never hunting call sites.
inline StableKey makeStableKey(uint64_t layoutId, uint64_t viewId) noexcept {
    return StableKey{1, layoutId, viewId};
}

} // namespace re::broker

namespace std {
/// std::hash specialization so unordered containers can key on StableKey
/// without introducing a second named functor type beside the key itself —
/// this header stays the key's single definition site.
template <>
struct hash<re::broker::StableKey> {
    std::size_t operator()(const re::broker::StableKey& k) const noexcept {
        std::size_t h = static_cast<std::size_t>(k.version);
        auto combine = [&h](uint64_t v) {
            h ^= std::hash<uint64_t>{}(v) + 0x9e3779b97f4a7c15ULL + (h << 6) +
                 (h >> 2);
        };
        combine(k.layoutId);
        combine(k.viewId);
        return h;
    }
};
} // namespace std
