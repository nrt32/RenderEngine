#pragma once

// broker/broker.hpp — Broker registry keyed by std::type_index (SPEC §11 V3.2b T3).
//
// OCP: open for extension via registerMapper<T>(unique_ptr<IMapper<T>>),
// closed for modification (no enum switch). Key is std::type_index(typeid(AppT))
// or typeid(MapperT) per overload — both are type_index factories, never an
// enum. Broker owns one IMapper per AppT (single dedup cache per type, SRP
// ownership per §11.3.3).
//
// Two overload sets:
//  - registerMapper<AppT,ReT>(unique_ptr<IMapper<AppT,ReT>>) / get<AppT,ReT>()
//    keyed by AppT (SPEC §11.3 Broker template<AppT,ReT> get()).
//  - registerMapper<MapperT>(unique_ptr<MapperT>) / get<MapperT>()
//    keyed by MapperT concrete type (test convenience — same OCP type_index
//    factory, no enum).
// Both use type_index, both are OCP.

#include <memory>
#include <typeindex>
#include <unordered_map>

#include "broker/i_mapper.hpp"

namespace re::broker {

/// Heavily abstracted per-type mediation registry (SPEC §11 V3.2b).
///
/// Owns one IMapper per AppT (type_index -> unique_ptr<IMapperBase>).
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

    /// Register a mapper for its AppT/ReT (keyed by type_index<AppT>).
    /// Ownership transferred; Broker owns exactly one per AppT (one cache per type).
    template <typename AppT, typename ReT>
    void registerMapper(std::unique_ptr<IMapper<AppT, ReT>> mapper) {
        auto key = std::type_index(typeid(AppT));
        ownedByApp_[key] = std::unique_ptr<IMapperBase>(mapper.release());
    }

    /// Retrieve mapper for AppT/ReT, or nullptr if not registered.
    ///
    /// @note lifetime: returns a NON-OWNING VIEW of a mapper SOLELY OWNED by
    /// this Broker (`unique_ptr` storage). The alias stays valid while the
    /// Broker does and until the caller re-registers the same key
    /// (registerMapper replaces the owned mapper); it must never be deleted,
    /// stored past Broker destruction, or handed across threads.
    template <typename AppT, typename ReT>
    IMapper<AppT, ReT>* get() const {
        auto key = std::type_index(typeid(AppT));
        auto it = ownedByApp_.find(key);
        if (it != ownedByApp_.end()) return static_cast<IMapper<AppT, ReT>*>(it->second.get());
        auto aliasIt = aliasByApp_.find(key);
        if (aliasIt != aliasByApp_.end()) return static_cast<IMapper<AppT, ReT>*>(aliasIt->second);
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
        MapperT* /*borrow*/ raw = mapper.get(); // borrow of the unique_ptr's
        // object; ownership moves into ownedByMapper_ below, which becomes the
        // sole owner — the alias is recorded in aliasByApp_/ownedByMapper_.
        ownedByMapper_[key] = std::unique_ptr<IMapperBase>(mapper.release());
        if constexpr (requires { typename MapperT::AppType; typename MapperT::ReType; }) {
            using AppT = typename MapperT::AppType;
            aliasByApp_[std::type_index(typeid(AppT))] = raw;
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
        return ownedByApp_.empty() && ownedByMapper_.empty();
    }

    /// Number of registered mapper types (owned entries).
    size_t size() const noexcept {
        return ownedByApp_.size() + ownedByMapper_.size();
    }

    /// Test helper: type_index of a MapperT (exposes key for gate invariant).
    template <typename MapperT>
    static std::type_index typeIndex() {
        return std::type_index(typeid(MapperT));
    }

    /// Test helper: type_index of an AppT/ReT pair (key used by app-typed path).
    template <typename AppT, typename ReT>
    static std::type_index typeIndexFor() {
        return std::type_index(typeid(AppT));
    }

   private:
    std::unordered_map<std::type_index, std::unique_ptr<IMapperBase>> ownedByApp_;
    // Non-owning aliases into ownedByMapper_ storage (keyed by AppT for the
    // app-typed get() overload). Every alias points at a mapper owned by
    // ownedByMapper_; re-registration overwrites both entries together.
    std::unordered_map<std::type_index, IMapperBase*> aliasByApp_;
    std::unordered_map<std::type_index, std::unique_ptr<IMapperBase>> ownedByMapper_;
};

} // namespace re::broker
