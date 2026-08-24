// broker/view_synchronizer.cpp — ViewSynchronizer: diff app-side view state
// against the cached render-side state and re-translate only what changed.
// Persistence contract (SPEC §10.3/§10.4, landed by the V3.5 persistence
// task): a camera rotation must NOT rebuild the ReView map or re-upload
// assets — only the dirty field's mapper runs; views keep their identity and
// GPU objects across sync() calls.
//
// Item translation contract ("no silent drops"): every item id resolves to a
// REAL layer through its per-type mapper + RenderStack renderer (see the
// header comment). A plane in Space::VoxelIndex converts to world through the
// registered PlaneMapper rule against the view's first volume-family item —
// an impossible conversion is a typed error, never a wrong or missing plane.

#include "broker/view_synchronizer.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>

#include "broker/camera_mapper.hpp"
#include "broker/contour_mapper.hpp"
#include "broker/mesh_object_mapper.hpp"
#include "broker/mesh_slice_object_mapper.hpp"
#include "broker/plane_mapper.hpp"
#include "broker/plane_object_mapper.hpp"
#include "broker/volume_object_mapper.hpp"
#include "broker/volume_slice_object_mapper.hpp"
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
    std::unordered_set<int> seen;
    std::vector<scene::FieldId> out;
    out.reserve(4);
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

    // Lock the weak compositor back-pointer: the synchronizer does not own
    // the compositor, it co-exists with it inside ViewBridge; an expired
    // pointer means the bridge was never wired (or already torn down).
    std::shared_ptr<ViewCompositor> compositor = compositor_.lock();

    if (!compositor) {
        // Not wired to a compositor yet (early skeleton wiring): consume the
        // generations so stale dirt is not re-reported once a compositor is
        // attached, then return without touching any render state.
        lastStoreGen_ = curGen;
        lastSceneStoreGen_ = scene.storeGeneration();
        pushDirties_.clear();
        return data::Result<void>(data::value);
    }

    // Conservative item-affecting store dirt: any object mutation since our
    // last completed sync (transform/material/TF/plane setters bump the STORE
    // generation while the VIEW generations stand still) forces an item
    // re-translation this pass. The dirty set is a conservative superset
    // (§10.4), so over-re-translating is safe — cached mappers make repeats
    // cheap and the GPU asset store dedups uploads by content hash.
    const auto sceneDirtySet = scene.dirtyFieldsSince(lastSceneStoreGen_);
    const bool storeItemsDirty =
        std::any_of(sceneDirtySet.begin(), sceneDirtySet.end(), [](scene::FieldId f) {
            switch (f) {
                case scene::FieldId::Items:
                case scene::FieldId::Transform:
                case scene::FieldId::Material:
                case scene::FieldId::TransferFunction:
                case scene::FieldId::Plane:
                    return true;
                default:
                    return false;
            }
        });

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

        // Presentation flags applied on every non-early-out pass (cheap
        // setters): the render side's ensureTarget recreates its target when
        // the depth mode flipped, and the pass prologue picks up the clear
        // color. Defaults match the engine's historical values, so views that
        // never set them behave exactly as before.
        rv->setDepthTest(av.depthTest);

        // Rect (size) — hash includes physical pixels
        bool rectDirty = isNew || cache.rectGen != av.rectGen || hasPushDirty(av.id, scene::FieldId::Rect);
        if (rectDirty) {
            render::ViewRect r{av.rect.x, av.rect.y, av.rect.w, av.rect.h};
            rv->setRect(r);
        }
        rv->setClearColor(av.clearColor);
        if (rectDirty) {
            // Recreate ViewTarget inner FBO if size changed (size hash includes physical pixels + contentScale)
            auto et = rv->ensureTarget();
            if (et.failed()) return et;
            cache.rectGen = av.rectGen;
        }

        // Plane (2D vs 3D) — converted to world space through the ONE
        // PlaneMapper conversion rule (VoxelIndex planes lift via the first
        // volume-family item's dims/transform; World passes through).
        bool planeDirty = isNew || cache.planeGen != av.planeGen || hasPushDirty(av.id, scene::FieldId::Plane);
        if (planeDirty) {
            if (av.plane.has_value()) {
                scene::TranslateContext pctx;
                pctx.view.viewPlane = av.plane;
                for (uint64_t vid : av.itemIds) {
                    const glm::mat4* model = nullptr;
                    glm::ivec3 dims{0};
                    if (auto* vo = scene.getVolumeObject(vid)) {
                        model = &vo->transform;
                        dims = glm::ivec3{static_cast<int>(vo->volume->sizeX()),
                                          static_cast<int>(vo->volume->sizeY()),
                                          static_cast<int>(vo->volume->sizeZ())};
                    } else if (auto* vs = scene.getVolumeSliceObject(vid)) {
                        model = &vs->transform;
                        dims = glm::ivec3{static_cast<int>(vs->volume->sizeX()),
                                          static_cast<int>(vs->volume->sizeY()),
                                          static_cast<int>(vs->volume->sizeZ())};
                    }
                    if (model != nullptr) {
                        scene::VolumeContext vc;
                        // The INDEX-space placement recovered from the
                        // canonical unit-cube model (see plane_mapper.hpp
                        // binding semantics) — for the standard MPR display
                        // models this is the pure axis permutation.
                        vc.volumeModel = indexPlacementFromModel(*model, dims);
                        vc.dims = dims;
                        vc.voxelSpacing = 1.0f;
                        pctx.volume = vc;
                        break;
                    }
                }
                // Prefer the REGISTERED mapper (OCP registry path); fall back
                // to the shared free function when none is wired — both are
                // the single definition of the conversion (plane_mapper.*).
                data::Result<render::ClipPlane> cp(
                    data::makeError<render::ClipPlane>(
                        1, "ViewSynchronizer: VoxelIndex plane without volume context"));
                if (auto* planeMapper =
                        broker_ ? broker_->get<scene::PlaneDesc, render::ClipPlane>()
                                : nullptr) {
                    cp = planeMapper->map(*av.plane, pctx);
                } else {
                    cp = convertViewPlaneToClipPlane(
                        *av.plane,
                        pctx.volume ? *pctx.volume : scene::VolumeContext{});
                }
                if (cp.failed()) {
                    return data::makeError<void>(cp.error().code,
                                                 std::string{"ViewSynchronizer: view "
                                                             "plane conversion failed: "} +
                                                     cp.error().message);
                }
                rv->setClipPlane(*cp);
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
            auto* camMapper = broker_ ? broker_->getMutable<broker::CameraMapper>() : nullptr;
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

        // Items — rebind plane+items without ReView map churn (2D→3D toggle).
        // Rebuild triggers: the view's own items generation, a plane change
        // (slice-family items bake the converted plane into their RE value),
        // a push-dirty, or conservative store-level item dirt (object field
        // mutations).
        bool itemsDirty = isNew || storeItemsDirty ||
                          cache.itemsGen != av.itemsGen ||
                          planeDirty ||
                          hasPushDirty(av.id, scene::FieldId::Items);
        if (itemsDirty) {
            rv->clearItems();
            std::vector<render::MeshInstance> transparentPending;
            for (uint64_t oid : av.itemIds) {
                auto r = mapItemToLayer(av, scene, oid, rv, transparentPending);
                if (r.failed()) return r;
            }
            // Transparent mesh instances captured out-of-band when the stack
            // carries the OIT pipeline (the compositor runs capture+composite
            // right after the view pass); otherwise they were added inline.
            compositor->setTransparentItems(layoutId, av.id,
                                            stack_ && stack_->pipeline
                                                ? std::move(transparentPending)
                                                : std::vector<render::MeshInstance>{});
            cache.itemsGen = av.itemsGen;
        }
    }

    // Layout count/set change -> insert/erase ReViews (prune those not in active set)
    compositor->pruneLayout(layoutId, activeIds);

    lastStoreGen_ = curGen;
    lastSceneStoreGen_ = scene.storeGeneration();
    pushDirties_.clear();
    return data::Result<void>(data::value);
}

data::Result<void> ViewSynchronizer::mapItemToLayer(
    const scene::View& av, const scene::SceneStore& scene, uint64_t oid,
    render::View* /*borrow*/ rv,
    std::vector<render::MeshInstance>& transparentOut) {
    if (!stack_) {
        return data::makeError<void>(
            12,
            "ViewSynchronizer: no RenderStack wired — item layers cannot be "
            "built; construct the synchronizer/bridge with a RenderStack");
    }
    // Self-contained context per item: the plane snapshot travels by value
    // (mappers never borrow live view state), matrices from the view camera.
    scene::TranslateContext ctx;
    ctx.view.viewPlane = av.plane;
    ctx.view.viewMatrix = av.camera.viewMatrix();
    ctx.view.projMatrix = av.camera.projMatrix();

    // --- MeshObject → MeshRenderer -----------------------------------------
    if (auto* mo = scene.getMeshObject(oid)) {
        auto* meshMapper = broker_ ? broker_->getMutable<broker::MeshObjectMapper>()
                                   : nullptr;
        if (!meshMapper) {
            return data::makeError<void>(
                13, "ViewSynchronizer: no MeshObjectMapper registered for a "
                    "MeshObject item (register mappers at the composition root)");
        }
        auto r = meshMapper->mapCached(*mo, ctx);
        if (r.failed()) {
            return data::makeError<void>(r.error().code, r.error().message);
        }
        const bool isTransparent =
            r->material && r->material->isTransparent();
        if (stack_->pipeline && isTransparent) {
            // Out-of-band capture path: View layers never engage the
            // pipeline (render/view.hpp contract), so the compositor runs
            // begin/capture/end right after this view's opaque pass.
            transparentOut.push_back(*r);
            return data::Result<void>(data::value);
        }
        render::MeshScene layer;
        layer.meshes.push_back(*r);
        rv->addItem(layer, stack_->mesh);
        return data::Result<void>(data::value);
    }

    // --- VolumeObject → VolumeRenderer: a REAL ray-cast layer, never a
    // silent no-op placeholder ----------------------------------------------
    if (auto* vo = scene.getVolumeObject(oid)) {
        auto* volMapper =
            broker_ ? broker_->getMutable<broker::VolumeObjectMapper>() : nullptr;
        if (!volMapper) {
            return data::makeError<void>(
                13, "ViewSynchronizer: no VolumeObjectMapper registered for a "
                    "VolumeObject item");
        }
        auto r = volMapper->mapCached(*vo, ctx);
        if (r.failed()) {
            return data::makeError<void>(r.error().code, r.error().message);
        }
        render::VolumeScene layer;
        layer.volumes.push_back(*r);
        rv->addItem(layer, stack_->volume);
        return data::Result<void>(data::value);
    }

    // --- VolumeSliceObject → VolumeSliceRenderer (GPU extraction) -----------
    if (auto* vs = scene.getVolumeSliceObject(oid)) {
        auto* sliceMapper = broker_
                                ? broker_->getMutable<broker::VolumeSliceObjectMapper>()
                                : nullptr;
        if (!sliceMapper) {
            return data::makeError<void>(
                13, "ViewSynchronizer: no VolumeSliceObjectMapper registered "
                    "for a VolumeSliceObject item");
        }
        auto r = sliceMapper->mapCached(*vs, ctx);
        if (r.failed()) {
            return data::makeError<void>(r.error().code, r.error().message);
        }
        render::VolumeSliceScene layer;
        layer.slices.push_back(*r);
        rv->addItem(layer, stack_->slice);
        return data::Result<void>(data::value);
    }

    // --- MeshSliceObject → SliceRenderer (geometry-shader clip) -------------
    if (auto* ms = scene.getMeshSliceObject(oid)) {
        auto* msMapper = broker_
                             ? broker_->getMutable<broker::MeshSliceObjectMapper>()
                             : nullptr;
        if (!msMapper) {
            return data::makeError<void>(
                13, "ViewSynchronizer: no MeshSliceObjectMapper registered "
                    "for a MeshSliceObject item");
        }
        auto r = msMapper->mapCached(*ms, ctx);
        if (r.failed()) {
            return data::makeError<void>(r.error().code, r.error().message);
        }
        rv->addItem(*r, stack_->meshSlice);
        return data::Result<void>(data::value);
    }

    // --- PlaneObject → PlaneRenderer (textured quad) ------------------------
    if (auto* po = scene.getPlaneObject(oid)) {
        auto* planeObjMapper = broker_ ? broker_->getMutable<broker::PlaneObjectMapper>()
                                       : nullptr;
        if (!planeObjMapper) {
            return data::makeError<void>(
                13, "ViewSynchronizer: no PlaneObjectMapper registered for a "
                    "PlaneObject item");
        }
        auto r = planeObjMapper->map(*po, ctx); // stateless pure mapper (ISP: no cache)
        if (r.failed()) {
            return data::makeError<void>(r.error().code, r.error().message);
        }
        render::PlaneScene layer;
        layer.planes.push_back(*r);
        rv->addItem(layer, stack_->plane);
        return data::Result<void>(data::value);
    }

    // --- ContourObject → ContourRenderer (GPU outline overlay) --------------
    if (auto* co = scene.getContourObject(oid)) {
        auto* contourMapper =
            broker_ ? broker_->getMutable<broker::ContourMapper>() : nullptr;
        if (!contourMapper) {
            return data::makeError<void>(
                13, "ViewSynchronizer: no ContourMapper registered for a "
                    "ContourObject item");
        }
        auto r = contourMapper->map(*co, ctx);
        if (r.failed()) {
            return data::makeError<void>(r.error().code, r.error().message);
        }
        rv->addItem(*r, stack_->contour);
        return data::Result<void>(data::value);
    }

    // Unknown id: a typed error, NEVER a silent placeholder layer (a skipped
    // item looks exactly like "renders nothing" from the outside).
    return data::makeError<void>(
        11, "ViewSynchronizer: item id " + std::to_string(oid) +
                " resolves to no scene object in the store");
}

} // namespace re::broker
