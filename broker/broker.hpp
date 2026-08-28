#pragma once

// broker/broker.hpp — Broker registry keyed by pair {AppT, ReT} (SPEC §11 V3.2b T3, T3 pair-key fix).
//
// OCP: open for extension via registerMapper<T>(unique_ptr<IMapper<T>>),
// closed for modification (no enum switch). Keys are:
//  - AppT/ReT path: hash_combine(type_index(AppT), type_index(ReT)) — the pair
//    key prevents UB from `static_cast<IMapper<AppT,ReT>*>` on a wrong ReT. A
//    mismatch returns nullptr (typed error downstream) instead of a type-punned
//    pointer, and same-AppT/different-ReT registrations become distinct entries
//    (no silent overwrite). This is the T3 A1 fix for the former AppT-only key
//    (broker.hpp:57-65) that was UB on wrong ReT.
//  - MapperT path: type_index(typeid(MapperT)) exact-keyed as before (test
//    convenience — same OCP type_index factory, no enum). That overload is
//    unchanged by the pair-key fix.
// Broker owns one IMapper per {AppT,ReT} pair (single dedup cache per type pair,
// SRP ownership per §11.3.3).
//
// Two overload sets:
//  - registerMapper<AppT,ReT>(unique_ptr<IMapper<AppT,ReT>>) / get<AppT,ReT>()
//    keyed by hash_combine(AppT,ReT) (T3 pair-key, type-safe).
//  - registerMapper<MapperT>(unique_ptr<MapperT>) / get<MapperT>()
//    keyed by MapperT concrete type (exact-keyed, unchanged).
// Both use type_index, both are OCP.

#include <cstddef>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "broker/i_mapper.hpp"
#include "scene/iscene_object.hpp"

namespace re::broker {

/// Pair-key for {AppT, ReT} — AppT/ReT typed miss returns nullptr instead of UB.
///
/// The former key was `type_index(AppT)` alone; `get<AppT,WrongRe>` then did
/// `static_cast<IMapper<AppT,WrongRe>*>` on the stored `IMapper<AppT,RightRe>`
/// — undefined behaviour (type-punning). The pair key uses
/// `hash_combine(type_index(AppT), type_index(ReT))` so a wrong ReT hashes to
/// a different bucket and misses cleanly (typed nullptr). Same AppT with two
/// different ReTs hashes to two distinct entries (no silent overwrite) — either
/// distinct registration succeeds or an explicit assert fires on collision.
struct AppReKey {
    std::type_index app;
    std::type_index re;
    bool operator==(const AppReKey& o) const noexcept {
        return app == o.app && re == o.re;
    }
};

/// Hash for AppReKey — boost hash_combine of the two type_index hashes.
///
/// `hashCombine(h1,h2) = h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1<<6) + (h1>>2))`
/// is the canonical hash_combine (Boost, SPEC T3 D: hash_combine(type_index(AppT),
/// type_index(ReT))). This keeps distinct ReT for the same AppT in distinct
/// buckets, and makes a wrong-ReT lookup miss with analytic nullptr evidence
/// (R4) rather than UB.
struct AppReKeyHash {
    std::size_t operator()(const AppReKey& k) const noexcept {
        const std::size_t h1 = std::hash<std::type_index>{}(k.app);
        const std::size_t h2 = std::hash<std::type_index>{}(k.re);
        // hash_combine — boost canonical constant 0x9e3779b97f4a7c15ULL
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

/// Heavily abstracted per-type mediation registry (SPEC §11 V3.2b, T3 pair-key).
///
/// Owns one IMapper per {AppT,ReT} pair (AppReKey -> unique_ptr<IMapperBase>).
/// Adding a new AppT/ReT needs zero edits to existing mapper files or
/// ViewMapper — one new *Mapper file + one registerMapper call (OCP via
/// type_index factory, Meyer PV, NDepend point-of-variation).
class Broker {
   public:
    Broker() = default;
    Broker(const Broker&) = delete;
    Broker& operator=(const Broker&) = delete;
    Broker(Broker&&) noexcept = default;
    Broker& operator=(Broker&&) noexcept = default;
    ~Broker() = default;

    /// Combine two type_index hashes — exposed for gate/tooling verification.
    static std::size_t hashCombine(std::type_index a, std::type_index b) noexcept {
        const std::size_t h1 = std::hash<std::type_index>{}(a);
        const std::size_t h2 = std::hash<std::type_index>{}(b);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }

    /// Pair key factory for {AppT, ReT} — builds AppReKey{type_index(AppT), type_index(ReT)} for Broker::get and registerMapper. The pair key is the T3 A1 type-safety fix: it hashes both indices via hash_combine so a wrong ReT cannot alias the correct entry, returning nullptr (typed miss) instead of the former UB static_cast, and guarantees same-AppT/different-ReT registrations occupy distinct buckets without silent overwrite, preserving OCP without an enum switch.
    template <typename AppT, typename ReT>
    static AppReKey pairKey() noexcept {
        return AppReKey{std::type_index(typeid(AppT)), std::type_index(typeid(ReT))};
    }

    /// Pair hash for {AppT, ReT} — the hash_combine value auditable as distinct-entry proof.
    template <typename AppT, typename ReT>
    static std::size_t pairKeyHash() noexcept {
        return hashCombine(std::type_index(typeid(AppT)), std::type_index(typeid(ReT)));
    }

     /// Register a mapper for its {AppT,ReT} pair (keyed by hash_combine(AppT,ReT) via AppReKey).
     /// Ownership transferred; Broker owns exactly one per {AppT,ReT} pair (one cache per pair).
     /// When AppT carries a static SceneKind (the polymorphic hierarchy — T1),
     /// the mapper is also aliased into the SceneKind map so the open
     /// SceneKind registry stays in sync with the type_index registry (one
     /// registration populates both typed and kind-keyed views, keeping the
     /// store open for extension without a second register call).
     template <typename AppT, typename ReT>
     void registerMapper(std::unique_ptr<IMapper<AppT, ReT>> mapper) {
         AppReKey key{std::type_index(typeid(AppT)), std::type_index(typeid(ReT))};
         IMapperBase* /*borrow*/ raw = mapper.get(); // borrow of the object
         // that ownedByApp_ will own after release — alias kept in kind map
         // below so Broker::registeredTypes() sees the kind without double
         // ownership.
         ownedByApp_[key] = std::unique_ptr<IMapperBase>(mapper.release());
         if constexpr (requires { AppT::Kind; }) {
             // AppT is a SceneKind-carrying polymorphic object (MeshObject etc.
             // — the T1 hierarchy). Alias the same mapper into the kind-keyed
             // registry so registeredTypes() reflects the open set and the
             // synchronizer can look up by SceneKind without branching.
             sceneKindAliases_[AppT::Kind] = raw;
         }
     }

    /// Retrieve mapper for {AppT,ReT}, or nullptr if not registered or ReT mismatched.
    ///
    /// Key is hash_combine(type_index(AppT), type_index(ReT)) — a wrong ReT
    /// hashes to a different bucket and returns nullptr (typed miss) instead of
    /// the former UB type-punning static_cast. This is the T3 type-safety gate:
    /// `broker.get<MeshObject, ReWrongType>() == nullptr` is the analytic evidence.
    ///
    /// @note lifetime: returns a NON-OWNING VIEW of a mapper SOLELY OWNED by
    /// this Broker (`unique_ptr` storage). The alias stays valid while the
    /// Broker does and until the caller re-registers the same key
    /// (registerMapper replaces the owned mapper); it must never be deleted,
    /// stored past Broker destruction, or handed across threads.
    template <typename AppT, typename ReT>
    IMapper<AppT, ReT>* get() const {
        AppReKey key{std::type_index(typeid(AppT)), std::type_index(typeid(ReT))};
        auto it = ownedByApp_.find(key);
        if (it != ownedByApp_.end()) return static_cast<IMapper<AppT, ReT>*>(it->second.get());
        // Derived from ownedByMapper_ via pair->mapper type index (T9 A2) — no
        // alias-map raw-pointer store, so a stale raw alias can never survive
        // a re-registration. The pair key is looked up in pairToMapperType_
        // and then the concrete mapper is fetched from ownedByMapper_ (the sole
        // owning map), guaranteeing the returned borrow is always the current
        // live object.
        auto pit = pairToMapperType_.find(key);
        if (pit != pairToMapperType_.end()) {
            auto mit = ownedByMapper_.find(pit->second);
            if (mit != ownedByMapper_.end()) return static_cast<IMapper<AppT, ReT>*>(mit->second.get());
        }
        return nullptr;
    }

    /// Non-const variant for cached mappers needing mapCached (non-const).
    /// Same non-owning-view contract as get().
    template <typename AppT, typename ReT>
    IMapper<AppT, ReT>* getMutable() {
        return const_cast<IMapper<AppT, ReT>*>(static_cast<const Broker*>(this)->get<AppT, ReT>());
    }

     /// Concrete-mapper overload: register by MapperT type (type_index<MapperT>).
     template <typename MapperT>
     void registerMapper(std::unique_ptr<MapperT> mapper) {
         static_assert(std::is_base_of_v<IMapperBase, MapperT>,
                       "MapperT must inherit IMapperBase");
         auto key = std::type_index(typeid(MapperT));
         IMapperBase* /*borrow*/ rawAfter = mapper.get(); // borrow before move, used for kind alias below
         ownedByMapper_[key] = std::unique_ptr<IMapperBase>(mapper.release());
         // Retrieve the canonical borrow from the owning map so the kind alias
         // never holds a stale raw from the released unique_ptr — the owning
         // map is the sole owner, and the alias consults it via the pair index
         // to avoid a dangling borrow after re-registration (T9 A2).
         IMapperBase* /*borrow*/ ownedRaw = ownedByMapper_[key].get();
         if constexpr (requires { typename MapperT::AppType; typename MapperT::ReType; }) {
             using AppT = typename MapperT::AppType;
             using ReT = typename MapperT::ReType;
             AppReKey pair{std::type_index(typeid(AppT)), std::type_index(typeid(ReT))};
             // Derived alias: pair -> mapper type_index, not a raw pointer.
             // get<AppT,ReT>() will derive the borrow from ownedByMapper_ via
             // this indirection, so a re-registration cannot leave a stale raw
             // alias behind (fixes the pre-T9 alias-map stale-raw bug).
             auto pit = pairToMapperType_.find(pair);
             if (pit != pairToMapperType_.end()) pit->second = key;
             else pairToMapperType_.emplace(pair, key);
             if constexpr (requires { AppT::Kind; }) {
                 sceneKindAliases_[AppT::Kind] = ownedRaw;
             }
         } else {
             (void)rawAfter;
         }
     }

    /// Concrete-mapper overload: get by MapperT type, or nullptr if not registered.
    /// @note lifetime: non-owning view over the mapper SOLELY OWNED by this
    /// Broker (ownedByMapper_ `unique_ptr`) — same contract as get<AppT,ReT>()
    /// above: valid while the Broker does and until re-registration of the
    /// same key; never delete or store past Broker destruction.
    template <typename MapperT>
    MapperT* /*borrow*/ get() const {
        auto key = std::type_index(typeid(MapperT));
        auto it = ownedByMapper_.find(key);
        if (it == ownedByMapper_.end()) return nullptr;
        return static_cast<MapperT*>(it->second.get());
    }

    /// Non-const concrete getter (for mapCached).
    /// @note lifetime: same Broker-owned storage view as get<MapperT>().
    template <typename MapperT>
    MapperT* /*borrow*/ getMutable() {
        auto key = std::type_index(typeid(MapperT));
        auto it = ownedByMapper_.find(key);
        if (it == ownedByMapper_.end()) return nullptr;
        return static_cast<MapperT*>(it->second.get());
    }

    /// Whether Broker holds any mapper (for test empty check).
     bool empty() const noexcept {
         return ownedByApp_.empty() && ownedByMapper_.empty() && sceneKindAliases_.empty();
     }

     /// Number of registered mapper types (owned entries).
     size_t size() const noexcept {
         return ownedByApp_.size() + ownedByMapper_.size();
     }

      /// SceneKind-keyed registry view — the T1 open hierarchy map
      /// `SceneKind → IMapperBase*` (Strategy per Kind, one file per mapper).
      /// `ISceneObject` data is processed only by its own mapper; the alias is a
      /// non-owning view into ownedByMapper_/ownedByApp_ storage (see
      /// registerMapper overloads) so one registration populates both views.
      /// Used by ViewSynchronizer's kind-dispatched sync and by the gate's
      /// `Broker::registeredTypes()` check that the 6 technique kinds
      /// (Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour) are registered
      /// — GeometryKind variations (Sphere, Cube, Teapot etc.) share the single
      /// MeshObjectMapper via MeshObject.geometryKind, not a new SceneKind. T1
      /// Phase C, T5 collapse 17→6.
     std::vector<scene::SceneKind> registeredTypes() const {
         std::vector<scene::SceneKind> out;
         out.reserve(sceneKindAliases_.size());
         for (const auto& kv : sceneKindAliases_) out.push_back(kv.first);
         return out;
     }

     /// Retrieve mapper for a SceneKind (open hierarchy), or nullptr if not
     /// registered. The returned pointer is a non-owning view into broker-owned
     /// storage — same lifetime contract as get<AppT,ReT>() — valid while the
     /// Broker does and until re-registration of that kind.
     /// @note lifetime: broker-owned storage borrow (see get<AppT,ReT>() note).
     IMapperBase* /*borrow*/ getByKind(scene::SceneKind kind) const noexcept {
         auto it = sceneKindAliases_.find(kind);
         return it == sceneKindAliases_.end() ? nullptr : it->second;
     }

     /// Typed SceneKind getter — convenience for mappers that know their ReT.
     /// Returns nullptr when kind not registered or ReT mismatched.
     template <typename AppT, typename ReT>
     IMapper<AppT, ReT>* getByKindTyped(scene::SceneKind kind) const noexcept {
         auto* base = getByKind(kind);
         return base ? static_cast<IMapper<AppT, ReT>*>(base) : nullptr;
     }

    /// Test helper: type_index of a MapperT (exposes key for gate invariant).
    template <typename MapperT>
    static std::type_index typeIndex() {
        return std::type_index(typeid(MapperT));
    }

    /// Test helper: type_index of an AppT/ReT pair (legacy name — now returns app type_index for compat).
    /// Prefer pairKey<AppT,ReT>() / pairKeyHash<AppT,ReT>() for the T3 pair-key proof.
    template <typename AppT, typename ReT>
    static std::type_index typeIndexFor() {
        return std::type_index(typeid(AppT));
    }

   private:
     std::unordered_map<AppReKey, std::unique_ptr<IMapperBase>, AppReKeyHash> ownedByApp_;
     // Pair -> mapper-type indirection (T9 A2) — derived from ownedByMapper_
     // instead of a raw-pointer alias store. get<AppT,ReT>() consults this map
     // to find the mapper's type_index and then fetches the live borrow from
     // ownedByMapper_. No stale raw alias can survive a re-registration because
     // the only raw borrows are the transient get() results, not a stored map.
     std::unordered_map<AppReKey, std::type_index, AppReKeyHash> pairToMapperType_;
     std::unordered_map<std::type_index, std::unique_ptr<IMapperBase>> ownedByMapper_;
     // SceneKind-keyed alias view into the same owned storage —Strategy per
     // Kind (one file per mapper). Adding a new kind needs only one new
     // *Mapper file plus one registerMapper call; existing files and the view
     // synchronizer remain closed for modification (OCP via type_index/SceneKind
     // factories). The map holds non-owning borrows into ownedByApp_/
     // ownedByMapper_ storage (same lifetime contract as the former alias-map,
     // now derived).
     std::unordered_map<scene::SceneKind, IMapperBase* /*borrow*/> sceneKindAliases_;
};

} // namespace re::broker