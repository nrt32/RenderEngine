#pragma once

// broker/cached_mapper_base.hpp — CachedMapperBase deduplication (T16, SPEC §11, gap G2).
//
// Every *ObjectMapper (MeshObjectMapper, VolumeObjectMapper,
// MeshSliceObjectMapper, VolumeSliceObjectMapper) repeated the same
// generation-cache boilerplate — struct Entry{generation, instance} plus
// unordered_map<uint64_t,Entry> cache_ and the mapCached generation
// short-circuit plus invalidate(id) and clear(). This header is the ONE
// definition that owns that cache. Derived cached mappers inherit from it
// and implement only map(); the base supplies mapCached/invalidate/clear
// and the Entry storage. PlaneMapper and PlaneObjectMapper stay stateless
// IMapper (ISP — they never cache, so they do not inherit this base).
// A slice mapper that also needs plane identity overrides the two hooks
// isCacheHit/fillEntry to include the view plane in the key; a simple
// mesh/volume mapper uses the default generation-only hit.

#include <cstdint>
#include <unordered_map>

#include "broker/i_cached_mapper.hpp"
#include "scene/plane_desc.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Generic cached mapper base — owns the per-id generation cache.
///
/// @tparam AppT App-side value type (must have `uint64_t id` and `uint64_t generation`).
/// @tparam ReT  RE-side value type (copyable).
///
/// The cache lives only here (analytic gate: grep count for
/// unordered_map.*Entry.*cache_ on broker/*_object_mapper.hpp == 0 after
/// consolidation, and the single definition in this file == 1). Derived types implement
/// only `map()`; `mapCached` short-circuits on generation hit (spy map call
/// count 2→1) and `invalidate(id)` evicts exactly that id (per-id probe).
template <typename AppT, typename ReT>
class CachedMapperBase : public ICachedMapper<AppT, ReT> {
   public:
    CachedMapperBase() = default;
    ~CachedMapperBase() override = default;

    /// Cache-aware translation — short-circuits when per-field generation
    /// (and, for slice mappers, view-plane identity) unchanged.
    data::Result<ReT> mapCached(const AppT& app,
                                const scene::TranslateContext& ctx) override {
        auto it = cache_.find(app.id);
        if (it != cache_.end() && isCacheHit(app, ctx, it->second)) {
            return data::makeValue<ReT>(it->second.instance);
        }
        auto r = this->map(app, ctx);
        if (r.ok()) {
            Entry e;
            fillEntry(e, app, ctx, *r);
            cache_[app.id] = std::move(e);
        } else {
            cache_.erase(app.id);
        }
        return r;
    }

    /// Invalidate cached entry for the given stable id.
    void invalidate(uint64_t id) override { cache_.erase(id); }

    /// Clear entire cache (used by tests and synchronizer resets).
    virtual void clear() { cache_.clear(); }

    /// Number of cached entries (test spy for eviction proof).
    std::size_t cacheSize() const noexcept { return cache_.size(); }

   protected:
    /// Cached entry — generation plus translated instance, plus optional view
    /// plane for contextual (slice) mappers. Simple mappers leave plane
    /// fields at their defaults; slice overrides populate and check them.
    struct Entry {
        uint64_t generation{0};
        ReT instance{};
        scene::PlaneDesc plane{};
        bool hasPlane{false};
    };

    std::unordered_map<uint64_t, Entry> cache_;

    /// Cache-hit predicate — default is generation equality only (mesh/volume
    /// path: the view plane is irrelevant, so a plane change does not
    /// invalidate). Slice mappers override to also require plane identity.
    virtual bool isCacheHit(const AppT& app,
                            const scene::TranslateContext& ctx,
                            const Entry& e) const {
        (void)ctx;
        return e.generation == app.generation;
    }

    /// Populate entry after a successful map — default stores generation and
    /// instance only. Slice overrides also store the view plane.
    virtual void fillEntry(Entry& e, const AppT& app,
                           const scene::TranslateContext& ctx,
                           const ReT& instance) const {
        (void)ctx;
        e.generation = app.generation;
        e.instance = instance;
        e.hasPlane = false;
    }
};

} // namespace re::broker
