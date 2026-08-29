#pragma once

// scene/iscene_object.hpp — Polymorphic scene object hierarchy (T1 pure-redesign).
//
// The closed `variant< MeshObject,…>` alias could not stay open for extension:
// with at least fifteen object kinds every new type edits the variant list and
// every visitor, violating the open-closed principle. The polymorphic hierarchy
// keeps the scene store open for extension (new ISceneObject subclasses register
// via a static factory) and closed for modification (no visitor edit). The
// hierarchy shares the duplicated ObjectHeader{ObjectId, transform, generation}
// via the CRTP mixin ObjectBase<Derived> which enforces at compile time that
// each Derived defines a static constexpr SceneKind Kind and provides a
// correctly typed clone via the mixin. Runtime exhaustiveness (variant's
// compile-time exhaustive std::visit) is replaced by a startup registry
// completeness check: SceneFactory::create(kind) fails loudly with a typed
// error when a Kind lacks a registered creator, so missing mappers are not
// silent. Materials stay variant-based (lights remain variant per the task
// scope) — this hierarchy does not cascade to LightDesc, only to scene objects.
// The extra heap allocation per object (unique_ptr<ISceneObject> plus the
// existing shared_ptr control block for the immutable asset) is accepted for
// open extension; a future slab or arena in SceneStore can amortise it without
// changing the ISceneObject contract, the alternative being bespoke variant
// visitors on every new kind which is the blocked path. T17 AssetRef<T>
// shared-ptr co-ownership is preserved (object is heap-allocated, asset stays
// shared), and trivial copy semantics of variant values are replaced by virtual
// clone() plus generation-tracked mutation through ObjectHeader::setTransform
// (T1).

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include <glm/mat4x4.hpp>

#include "data/result.hpp"
#include "scene/layer.hpp"

namespace re::scene {

/// Stable handle type for scene objects (uint64_t) — unchanged from the
/// value-type iteration, retained so existing store handle code and View
/// item lists continue to use bare ids.
using ObjectId = uint64_t;

/// Shared asset reference for scene objects (T13 ownership discipline): the
/// pointed-to data::* asset is IMMUTABLE after load and co-owned by every
/// scene object / store that references it, so a stored scene value can never
/// dangle when the loader-side owner goes away. Copying an object copies the
/// reference (cheap, shares the asset); no deep copy of voxels/positions is
/// ever made (RE-agnostic data). Kept here so concrete object headers can use
/// AssetRef without including scene/object.hpp. T1 D.
template <typename T>
using AssetRef = std::shared_ptr<const T>;

/// Scene kind — the stable type identity for technique dispatch (T5).
///
/// V5 T5 collapses the 11 byte-identical mesh-backed kinds (Cube, Sphere,
/// Cylinder, Torus, Cone, Arrow, Grid, Axes, Capsule, PointCloud, Teapot that
/// shared `AssetRef<Mesh> mesh; mat4 transform; MeshMaterialDesc presentation;`
/// at scene/objects/*.hpp:36-40) into one MeshObject carrying GeometryKind
/// {Mesh, Cube, Sphere, Cylinder, Torus, Cone, Arrow, Grid, Axes, Capsule,
/// PointCloud, Teapot}. SceneKind stays for technique dispatch only — 6 values
/// (Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour) — so adding a Sphere
/// no longer needs a new header; `MeshObject{ .geometryKind = Sphere }` via the
/// single MeshObjectMapper renders within 1/255 of the old per-kind path.
/// SceneFactory + REGISTER_SCENE_OBJECT remain for truly new techniques (e.g.,
/// StreamlineObject), not for data-driven mesh variations. The factory and the
/// broker key on this 6-value enum, while existing mapper files and the view
/// synchronizer remain closed for modification (open-closed via registry for
/// techniques, GeometryKind for mesh variations). Kept as plain enum for
/// unordered_map key without type_index RTTI. T5.
enum class SceneKind : uint32_t {
    Mesh = 0,
    MeshSlice = 1,
    Volume = 2,
    VolumeSlice = 3,
    Plane = 4,
    Contour = 5,
    Count = 6
};

/// Shared header for every scene object — the duplicated {ObjectId,
/// transform, generation, setTransform} that every concrete value type
/// previously hand-copied. Centralising it in ObjectBase guarantees that
/// generation bumping and transform storage stay consistent across kinds and
/// that future slab allocation can move the header without touching each
/// concrete header. T1 D.
struct ObjectHeader {
    ObjectId id{0};
    glm::mat4 transform{1.0f};
    uint64_t generation{0};

    /// Replace transform and bump generation — the single mutation entry point
    /// for the header's transform field, so callers cannot change the matrix
    /// without the generation tracking that the persistence cache relies on.
    void setTransform(glm::mat4 m) noexcept {
        transform = m;
        ++generation;
    }
};

/// Polymorphic base for all scene objects — the replacement for the closed
/// variant< MeshObject,…> alias. Each concrete kind derives from
/// ObjectBase<Derived> which implements this interface via CRTP, so the
/// interface stays minimal and concrete types do not hand-implement the same
/// forwarding boilerplate. Lifetime: objects are heap-allocated
/// (unique_ptr<ISceneObject>) and owned by SceneStore partitions; assets
/// remain shared-ptr co-owned (T17) and are not part of this interface. T1.
class ISceneObject {
   public:
    virtual ~ISceneObject() = default;

    /// Stable id assigned by SceneStore (never reused with same generation).
    virtual ObjectId id() const noexcept = 0;
    /// World transform (column-major glm::mat4, identity by default).
    virtual const glm::mat4& transform() const noexcept = 0;
    /// Per-object generation — bumped on transform or presentation mutation.
    virtual uint64_t generation() const noexcept = 0;
    /// Deep polymorphic copy — replaces variant's trivial copy; the store and
    /// the synchronizer clone via this when snapshotting.
    virtual std::unique_ptr<ISceneObject> clone() const = 0;
    /// Discriminant for the registry — each concrete Derived exposes a static
    /// constexpr SceneKind Kind and this returns it.
    virtual SceneKind kind() const noexcept = 0;

    /// Mutate world transform (bump generation). Non-const because it mutates
    /// the header; exposed on the base so generic code can move any object.
    virtual void setTransform(glm::mat4 m) noexcept = 0;

    /// Access mutable header for store assignment of id/generation on insertion
    /// — the store is the sole writer of id, so this is used only inside
    /// SceneStore add-methods (addMeshObject etc.).
    virtual void setId(ObjectId v) noexcept = 0;
    virtual void setGeneration(uint64_t g) noexcept = 0;

    /// Visual stacking layer for this object — lower values are drawn first. Every concrete object exposes this as a public Layer field; the polymorphic accessor lets the broker resolve the effective layer without branching on kind.
    virtual Layer layer() const noexcept = 0;
    /// Change the stacking layer and bump generation so dirty tracking re-translates the affected view.
    virtual void setLayer(Layer l) noexcept = 0;

    /// Scoped priority within the same layer+type bucket — lower values draw first inside the same layer and techniqueOrder bucket (T5 dumb layers). Higher priority does not escape its layer: a VolumeSlice priority 100 on LAYER_0 still draws before a Contour priority 0 on LAYER_0 when techniqueOrder says VolumeSlice before Contour, and both still draw before any LAYER_1. Every concrete object exposes this as a public int32_t priority field.
    virtual int32_t priority() const noexcept = 0;
    /// Change the scoped priority and bump generation so dirty tracking re-translates the affected view (bump uses FieldId::Priority).
    virtual void setPriority(int32_t p) noexcept = 0;
};

/// CRTP mixin that implements ISceneObject forwarding for a concrete Derived.
///
/// Enforces at compile time that Derived defines `static constexpr SceneKind
/// Kind` and a public header triple `ObjectId id; glm::mat4 transform;
/// uint64_t generation;` (the duplicated ObjectHeader that every former value
/// type hand-copied). Sharing is via CRTP delegation — the mixin forwards
/// id()/transform()/generation()/setTransform() to the Derived's fields, so a
/// future slab or arena in SceneStore can still move the storage without
/// touching each concrete header, while existing call sites that do
/// `obj.id = 1; obj.transform = m; obj.generation = 0;` keep compiling (the
/// fields remain public on the Derived, as in the value-type iteration). The
/// mixin's clone uses static_cast to the derived type so no manual down-cast
/// is needed in each header, and the static_assert on Kind enforces the
/// compile-time Kind requirement replacing variant's exhaustive visit. T1.
template <typename Derived>
class ObjectBase : public ISceneObject {
   public:
    ObjectBase() = default;
    ObjectBase(const ObjectBase&) = default;
    ObjectBase(ObjectBase&&) noexcept = default;
    ObjectBase& operator=(const ObjectBase&) = default;
    ObjectBase& operator=(ObjectBase&&) noexcept = default;
    ~ObjectBase() override = default;

    ObjectId id() const noexcept override {
        return static_cast<const Derived*>(this)->id;
    }
    const glm::mat4& transform() const noexcept override {
        return static_cast<const Derived*>(this)->transform;
    }
    uint64_t generation() const noexcept override {
        return static_cast<const Derived*>(this)->generation;
    }
    SceneKind kind() const noexcept override { return Derived::Kind; }

    void setTransform(glm::mat4 m) noexcept override {
        auto* /*borrow*/ d = static_cast<Derived*>(this); // @note lifetime: borrowed — points to `*this` (CRTP derived), valid for duration of call
        d->transform = std::move(m);
        ++d->generation;
    }
    void setId(ObjectId v) noexcept override { static_cast<Derived*>(this)->id = v; }
    void setGeneration(uint64_t g) noexcept override {
        static_cast<Derived*>(this)->generation = g;
    }

    Layer layer() const noexcept override {
        return static_cast<const Derived*>(this)->layer;
    }
    void setLayer(Layer l) noexcept override {
        auto* /*borrow*/ d = static_cast<Derived*>(this); // @note lifetime: borrowed — points to `*this` (CRTP derived), valid for duration of call
        if (d->layer != l) {
            d->layer = l;
            ++d->generation;
        }
    }

    int32_t priority() const noexcept override {
        return static_cast<const Derived*>(this)->priority;
    }
    void setPriority(int32_t p) noexcept override {
        auto* /*borrow*/ d = static_cast<Derived*>(this); // @note lifetime: borrowed — points to `*this` (CRTP derived), valid for duration of call
        if (d->priority != p) {
            d->priority = p;
            ++d->generation;
        }
    }

    std::unique_ptr<ISceneObject> clone() const override {
        // T1: ObjectBase enforces Kind/clone at compile time — if Derived
        // lacks a static Kind member this line fails to compile, which is
        // the intended compile-time enforcement replacing variant's exhaustive
        // visit.
        static_assert(std::is_base_of_v<ObjectBase<Derived>, Derived>,
                      "Derived must inherit ObjectBase<Derived>");
        return std::make_unique<Derived>(static_cast<const Derived&>(*this));
    }
};

/// Factory for ISceneObject kinds — the open registry that replaces the closed
/// variant list. Each concrete kind registers a creator lambda via the
/// REGISTER_SCENE_OBJECT macro's static registrar; SceneFactory::create(kind)
/// fails loudly (typed error) when a Kind lacks a creator, which is the
/// runtime replacement for variant's compile-time exhaustive visitor error but
/// remains loud rather than silent. T1 Phase A.
class SceneFactory {
   public:
    using Creator = std::function<std::unique_ptr<ISceneObject>()>;

    /// Singleton — the process-wide registry populated by static registrars
    /// before main() runs (one registry for the whole engine, matching the
    /// broker's single composition root).
    static SceneFactory& instance() {
        static SceneFactory inst;
        return inst;
    }

    /// Register a creator for kind — called by REGISTER_SCENE_OBJECT static
    /// registrars. If kind already has a creator the new one replaces it
    /// (last registration wins, matching Broker's overwrite semantics for
    /// re-registration during tests).
    void registerKind(SceneKind kind, Creator creator) {
        creators_[kind] = std::move(creator);
    }

    /// Create an instance of kind, or return nullptr when no creator is
    /// registered (startup completeness check fails loud — the caller maps
    /// nullptr to a typed error rather than silently skipping).
    std::unique_ptr<ISceneObject> create(SceneKind kind) const {
        auto it = creators_.find(kind);
        if (it == creators_.end()) return nullptr;
        return it->second();
    }

    /// Whether kind has a registered creator — the loud completeness predicate
    /// the synchronizer checks at startup (Factory::create(kind) fails loud if
    /// a Kind lacks a mapper).
    bool hasKind(SceneKind kind) const noexcept { return creators_.count(kind) != 0u; }

    /// Number of registered kinds — count invariant for tests (analytic count).
    size_t size() const noexcept { return creators_.size(); }

    /// All registered kinds — for diagnostics and Broker::registeredTypes parity.
    std::vector<SceneKind> registeredKinds() const {
        std::vector<SceneKind> out;
        out.reserve(creators_.size());
        for (const auto& kv : creators_) out.push_back(kv.first);
        return out;
    }

   private:
    SceneFactory() = default;
    std::unordered_map<SceneKind, Creator> creators_;
};

} // namespace re::scene

/// Helper macro to register a concrete scene object kind with the factory.
///
/// Must appear at namespace scope after the Derived definition. Expands to a
/// static registrar whose constructor runs before main() and inserts a creator
/// that default-constructs the Derived. The requirement that Derived defines
/// `static constexpr SceneKind Kind` is enforced by ObjectBase::clone's
/// static_assert and by the lambda's use of Derived::Kind here. T1.
#define REGISTER_SCENE_OBJECT(DERIVED)                                         \
    namespace {                                                                \
    struct DERIVED##SceneFactoryRegistrar {                                    \
        DERIVED##SceneFactoryRegistrar() {                                     \
            ::re::scene::SceneFactory::instance().registerKind(                \
                ::re::scene::DERIVED::Kind, []() -> std::unique_ptr<::re::scene::ISceneObject> { \
                    return std::make_unique<::re::scene::DERIVED>();          \
                });                                                            \
        }                                                                      \
    };                                                                         \
    static DERIVED##SceneFactoryRegistrar global_##DERIVED##_factory_registrar; \
    }
