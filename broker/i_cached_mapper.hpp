#pragma once

// broker/i_cached_mapper.hpp — ICachedMapper declaration for DIP stability (SPEC §11.2.1 Q33).
//
// ICachedMapper is the ISP-segregated extension of IMapper that adds the cached
// path mapCached and invalidate. It lives in its own header so the one-mapper-
// per-file rule (broker_per_type) stays green: i_mapper.hpp owns the single
// IMapper declaration, this header owns the single ICachedMapper declaration,
// and no header defines two mapper interfaces. A pure mapper includes only
// i_mapper.hpp, a cached mapper includes this header which re-exports the base.

#include "broker/i_mapper.hpp"

namespace re::broker {

/// Cached mapper — ISP extension of IMapper with generation/contentHash cache.
///
/// Adds mapCached (cache-aware) and invalidate (per-id eviction).
/// Follows SPEC §10.4 per-field generation + SPEC §11.2.1 ISP split. This is
/// the sole sanctioned declaration of mapCached; the audit rule
/// isp_mapper_forbid allowlists this header and forbids any IMapper-derived
/// mapper from exposing mapCached, preserving the interface segregation.
/// @tparam AppT App-side value type.
/// @tparam ReT RE-side value type.
template <typename AppT, typename ReT>
class ICachedMapper : public IMapper<AppT, ReT> {
   public:
    virtual ~ICachedMapper() = default;

    /// Cache-aware translation: short-circuits when per-field gen+hash unchanged.
    virtual data::Result<ReT> mapCached(const AppT& app,
                                        const scene::TranslateContext& ctx) = 0;

    /// Invalidate cached entry for the given stable id (e.g. ViewId/ObjectId).
    virtual void invalidate(uint64_t id) = 0;
};

} // namespace re::broker
