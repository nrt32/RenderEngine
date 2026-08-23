#pragma once

// render/texture_cache_key.hpp — weak-observer cache keys for CPU assets
// (T13 ownership discipline).
//
// The per-renderer GPU texture caches are keyed by an OBSERVER
// (`std::weak_ptr`) of the immutable CPU asset instead of a raw pointer, so:
//   - a destroyed asset can never be looked up again (its key expires with
//     it — no dangling key serving stale GPU data), and
//   - the GPU cache never keeps a CPU asset alive by itself (observer, not
//     owner — scene objects co-own the assets they reference).
// Hash/equality use OWNER identity (stable for shared_ptr/weak_ptr even after
// the pointee dies): equal keys == same control block. Hashing locks briefly
// to derive a stable address from the live object; all expired owners hash to
// nullptr and merely collide (harmless — expired entries are pruned on hit).

#include <cstddef>
#include <memory>

namespace re::render {

/// Owner-based weak observer key (the "asset identity" side of the cache).
template <typename T>
using WeakAssetKey = std::weak_ptr<const T>;

/// Owner-equivalent equality: true iff both observers share the SAME control
/// block (same live object, or both expired). Stable across expiry.
template <typename T>
struct WeakAssetOwnerEq {
    bool operator()(const WeakAssetKey<T>& a, const WeakAssetKey<T>& b) const noexcept {
        return !a.owner_before(b) && !b.owner_before(a);
    }
};

/// Owner-consistent hash: two owner-equivalent keys hash equal because they
/// lock() to the same object address (or both to null when expired).
template <typename T>
struct WeakAssetOwnerHash {
    std::size_t operator()(const WeakAssetKey<T>& k) const noexcept {
        return std::hash<const T*>{}(k.lock().get());
    }
};

} // namespace re::render
