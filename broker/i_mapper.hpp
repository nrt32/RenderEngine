#pragma once

// i_mapper.hpp — Mapper interfaces ISP-split (SPEC §11 V3.2b T3).
//
// Policy-owned abstractions (DIP): broker/ defines the mapping contract,
// low-level render/ and scene/ types satisfy it. ISP segregation per
// SPEC §11.2.1 (code-note-vr/Baeldung ISP fat-interface fix):
//   IMapper<AppT,ReT>  — pure translation  map(App, Ctx) -> Re
//   ICachedMapper<AppT,ReT> : IMapper — adds mapCached + invalidate (cached path)
// A pure mapper (e.g. PlaneMapper) implements only IMapper; a cached mapper
// (e.g. CameraMapper, MeshObjectMapper) implements ICachedMapper.
// Broker stores type-erased IMapperBase (OCP via type_index factory, no enum).
//
// Only lib that may include both scene/ and render/ (SPEC §3/§11 ACL).

#include <cstdint>

#include "data/result.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

/// Type-erased mapper base for Broker registry (OCP factory, no enum switch).
struct IMapperBase {
    virtual ~IMapperBase() = default;
};

/// Pure mapper — single translation with no cache (ISP: no mapCached here).
/// The ISP segregation guarantees that a pure mapper never exposes the cached
/// path; only the derived ICachedMapper adds mapCached and invalidate. This
/// separation lets PlaneMapper stay lean while CameraMapper can memoize, and
/// the audit rule isp_mapper_forbid verifies that no IMapper-derived class
/// mentions mapCached, with the sole allowlist being the ICachedMapper
/// interface header where the cached path is intentionally declared.
/// The Broker stores type-erased IMapperBase so the registry remains open for
/// extension without an enum switch, and the per-file mapper count stays one.
/// @tparam AppT App-side value type (re::scene::...).
/// @tparam ReT  RE-side value type (re::render::...).
template <typename AppT, typename ReT>
class IMapper : public IMapperBase {
   public:
    virtual ~IMapper() = default;

    /// Pure translation: app value + segregated context -> RE value.
    ///
    /// Uniform signature keeps all mappers substitutable (LSP).
    /// Never throws; errors are typed Result codes.
    virtual data::Result<ReT> map(const AppT& app,
                                  const scene::TranslateContext& ctx) const = 0;
};

} // namespace re::broker
