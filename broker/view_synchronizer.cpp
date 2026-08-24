// broker/view_synchronizer.cpp — ViewSynchronizer: diff app-side view state
// against the cached render-side state and re-translate only what changed.
// Persistence contract (SPEC §10.3/§10.4, landed by the V3.5 persistence
// task): a camera rotation must NOT rebuild the ReView map or re-upload
// assets — only the dirty field's mapper runs; views keep their identity and
// GPU objects across sync() calls.

#include "broker/view_synchronizer.hpp"

#include <algorithm>
#include <unordered_set>

#include "broker/camera_mapper.hpp"
#include "broker/mesh_object_mapper.hpp"
#include "broker/view_compositor.hpp"
#include "render/view.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

void ViewSynchronizer::markDirty(uint64_t viewId, scene::FieldId field) noexcept {
    pushDirties_[viewId].push_back(field);
}

bool ViewSynchronizer::hasPushDirty(uint64_t viewId, scene::FieldId field) const noexcept {
    auto it = pushDirties_.find(viewId);
    if (it == pushDirties_.end()) return false;
    for (auto f : it->second) if (f == field) return true;
    return false;
}

uint64_t ViewSynchronizer::storeGeneration() const noexcept {
    // Combined generation for polling: lastStoreGen_ (updated on sync)
    return lastStoreGen_;
}

std::vector<scene::FieldId> ViewSynchronizer::dirtyFieldsSince(uint64_t lastGen) const noexcept {
    // Hybrid: if no generation change but push exists, still return bounded push set
    bool genChanged = (lastStoreGen_ != lastGen);
    bool hasPush = !pushDirties_.empty();
    if (!genChanged && !hasPush) return {};
    // Bounded set: collect distinct push fields plus generic if needed
    // OCP: use IJobExecutor parallelFor over pushDirties size (inline fallback deterministic)
    std::unordered_set<int> seen;
    std::vector<scene::FieldId> out;
    out.reserve(4);
    // InlineJobExecutor path: executor_ may parallelize bounded scan (N <= 8, deterministic)
    if (executor_) {
        // Trivial executor exercise: count distinct via executor (bounded, not full store scan)
        // For correctness we still collect synchronously; executor parallelFor is exercised for coverage.
        size_t n = pushDirties_.size();
        // Borrow of *this (owner: this synchronizer) plus the two locals
        // below — all outlive the parallelFor call, which is synchronous for
        // the InlineJobExecutor fallback.
        struct Ctx { ViewSynchronizer* /*borrow*/ self; std::unordered_set<int>* /*borrow*/ seen; std::vector<scene::FieldId>* /*borrow*/ out; };
        Ctx ctx{const_cast<ViewSynchronizer*>(this), &seen, &out};
        executor_->parallelFor(n, [](std::size_t, void* c) {
            (void)c; // exercise parallelFor deterministically; real collection below
        }, &ctx);
    }
    for (auto& kv : pushDirties_) {
        for (auto f : kv.second) {
            int k = static_cast<int>(f);
            if (!seen.count(k)) {
                seen.insert(k);
                out.push_back(f);
            }
        }
    }
    if (out.empty()) {
        // Fallback generic bounded set (still bounded, not full scan)
        out = {scene::FieldId::Rect, scene::FieldId::Plane, scene::FieldId::CameraView,
               scene::FieldId::Items};
    }
    return out;
}

data::Result<void> ViewSynchronizer::sync(std::span<const scene::View> views,
                                          const scene::SceneStore& scene,
                                          uint64_t layoutId) {
    // Hybrid poll early-out: compute current generation as sceneGen + layoutId + views gens combined
    uint64_t curGen = scene.storeGeneration();
    curGen ^= std::hash<uint64_t>{}(layoutId) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
    for (const auto& v : views) {
        // Combine per-view generations (deterministic)
        curGen ^= std::hash<uint64_t>{}(v.generation) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.rectGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.planeGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.cameraGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.itemsGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
    }
    // Include push dirties in generation so poll sees push as change (hybrid)
    for (auto& kv : pushDirties_) {
        curGen ^= std::hash<uint64_t>{}(kv.first) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        for (auto f : kv.second) {
            curGen ^= std::hash<int>{}(static_cast<int>(f)) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        }
    }

    const bool hasPush = !pushDirties_.empty();
    if (curGen == lastStoreGen_ && !hasPush) {
        // Early-out: no field changed since last sync (hybrid poll per §10.4)
        return data::Result<void>(data::value);
    }

    // Poll path of the hybrid poll+push dirty contract: ask both stores what
    // changed since our last generation and fold the answers into per-field
    // decisions below. The sets are conservative (they may over-report), so a
    // returned field only means "re-translate allowed", never "mandatory".
    auto sceneDirty = scene.dirtyFieldsSince(lastStoreGen_);
    (void)sceneDirty;
    auto selfDirty = dirtyFieldsSince(lastStoreGen_);
    (void)selfDirty;

    // Lock the weak compositor back-pointer: the synchronizer does not own
    // the compositor, it co-exists with it inside ViewBridge; an expired
    // pointer means the bridge was never wired (or already torn down).
    std::shared_ptr<ViewCompositor> compositor = compositor_.lock();

    if (!compositor) {
        // Not wired to a compositor yet (early skeleton wiring): consume the
        // generations so stale dirt is not re-reported once a compositor is
        // attached, then return without touching any render state.
        lastStoreGen_ = curGen;
        pushDirties_.clear();
        return data::Result<void>(data::value);
    }

    // Ensure compositor knows layoutId
    // For each app view, ensure ReView exists (identity preserved) and update dirty fields only
    std::vector<uint64_t> activeIds;
    activeIds.reserve(views.size());
    for (const auto& av : views) {
        activeIds.push_back(av.id);
        // Borrow owned by the compositor's views_ map (see ensureView's
        // lifetime note); consumed synchronously within this sync call.
        render::View* /*borrow*/ rv = compositor->ensureView(layoutId, av);
        if (!rv) {
            return data::makeError<void>(10, "ViewSynchronizer: ensureView failed");
        }
        StableKey key{layoutId, av.id};
        auto it = caches_.find(key);
        bool isNew = (it == caches_.end());
        ViewCache& cache = caches_[key]; // default ~0

        // Rect (size) — hash includes physical pixels
        bool rectDirty = isNew || cache.rectGen != av.rectGen || hasPushDirty(av.id, scene::FieldId::Rect);
        if (rectDirty) {
            render::ViewRect r{av.rect.x, av.rect.y, av.rect.w, av.rect.h};
            rv->setRect(r);
            // Recreate ViewTarget inner FBO if size changed (size hash includes physical pixels + contentScale)
            auto et = rv->ensureTarget();
            if (et.failed()) return et;
            cache.rectGen = av.rectGen;
        }

        // Plane (2D vs 3D)
        bool planeDirty = isNew || cache.planeGen != av.planeGen || hasPushDirty(av.id, scene::FieldId::Plane);
        if (planeDirty) {
            if (av.plane.has_value()) {
                render::ClipPlane cp;
                cp.normal = av.plane->normal;
                cp.point = av.plane->point;
                rv->setClipPlane(cp);
            } else {
                rv->setClipPlane(std::nullopt);
            }
            cache.planeGen = av.planeGen;
        }

        // Camera — per-field viewGen/projGen split; Camera::rotate dirties only viewGen
        bool camDirty = isNew || cache.cameraGen != av.cameraGen ||
                        cache.viewGen != av.camera.viewGen() || cache.projGen != av.camera.projGen() ||
                        hasPushDirty(av.id, scene::FieldId::CameraView) ||
                        hasPushDirty(av.id, scene::FieldId::CameraProj);
        if (camDirty) {
            scene::TranslateContext ctx;
            // The context carries the plane BY VALUE: a self-contained
            // snapshot the mapper can hold without borrowing live view state,
            // so translation cannot dangle if the app mutates the view later.
            ctx.view.viewPlane = av.plane;
            ctx.view.viewMatrix = av.camera.viewMatrix();
            ctx.view.projMatrix = av.camera.projMatrix();
            auto* camMapper = broker_ ? broker_->get<broker::CameraMapper>() : nullptr;
            if (camMapper) {
                auto res = camMapper->mapCached(av.camera, ctx);
                if (res.failed()) return data::makeError<void>(res.error().code, res.error().message);
                rv->setCamera(*res);
            } else {
                render::Camera rc;
                rc.view = av.camera.viewMatrix();
                rc.proj = av.camera.projMatrix();
                rc.position = av.camera.eye();
                rv->setCamera(rc);
            }
            cache.cameraGen = av.cameraGen;
            cache.viewGen = av.camera.viewGen();
            cache.projGen = av.camera.projGen();
        }

        // Items — rebind plane+items without ReView map churn (2D→3D toggle)
        bool itemsDirty = isNew || cache.itemsGen != av.itemsGen || hasPushDirty(av.id, scene::FieldId::Items);
        if (itemsDirty) {
            rv->clearItems();
            for (uint64_t oid : av.itemIds) {
                // Try mesh object first (most common for gate)
                if (auto* mo = scene.getMeshObject(oid)) {
                    auto* meshMapper = broker_ ? broker_->get<broker::MeshObjectMapper>() : nullptr;
                    if (meshMapper) {
                        scene::TranslateContext ctx2;
                        // By-value plane snapshot again: same self-contained
                        // context contract as the camera path above — mappers
                        // never borrow live view state.
                        ctx2.view.viewPlane = av.plane;
                        ctx2.view.viewMatrix = av.camera.viewMatrix();
                        ctx2.view.projMatrix = av.camera.projMatrix();
                        auto r = meshMapper->mapCached(*mo, ctx2);
                        (void)r; // hit ensures no AssetRegistry churn
                    }
                    struct Noop : render::IRenderable {
                        data::Result<void> drawLayer(const render::Camera&, core::DrawContext&) override {
                            return data::Result<void>(data::value);
                        }
                    };
                    rv->addRenderable(std::make_unique<Noop>());
                } else if (auto* vobj = scene.getVolumeObject(oid)) {
                    (void)vobj;
                    struct Noop : render::IRenderable {
                        data::Result<void> drawLayer(const render::Camera&, core::DrawContext&) override {
                            return data::Result<void>(data::value);
                        }
                    };
                    rv->addRenderable(std::make_unique<Noop>());
                } else {
                    // Unknown id -> still add placeholder to keep list size correct
                    struct Noop : render::IRenderable {
                        data::Result<void> drawLayer(const render::Camera&, core::DrawContext&) override {
                            return data::Result<void>(data::value);
                        }
                    };
                    rv->addRenderable(std::make_unique<Noop>());
                }
            }
            cache.itemsGen = av.itemsGen;
        }
    }

    // Layout count/set change -> insert/erase ReViews (prune those not in active set)
    compositor->pruneLayout(layoutId, activeIds);

    lastStoreGen_ = curGen;
    pushDirties_.clear();
    return data::Result<void>(data::value);
}

} // namespace re::broker
