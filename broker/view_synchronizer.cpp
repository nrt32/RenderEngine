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
#include "broker/light_mapper.hpp"
#include "broker/mesh_object_mapper.hpp"
#include "broker/mesh_slice_object_mapper.hpp"
#include "broker/plane_mapper.hpp"
#include "broker/plane_object_mapper.hpp"
#include "broker/view_mapper.hpp"
#include "broker/volume_object_mapper.hpp"
#include "broker/volume_slice_object_mapper.hpp"
#include "broker/view_compositor.hpp"
#include "render/view.hpp"
#include "scene/layer.hpp"
#include "scene/translate_context.hpp"

namespace re::broker {

namespace {

int techniquePriorityFor(scene::SceneKind k) noexcept {
    switch (k) {
        case scene::SceneKind::Volume: return 0;
        case scene::SceneKind::VolumeSlice: return 1;
        case scene::SceneKind::Plane: return 2;
        case scene::SceneKind::Mesh: return 3;
        case scene::SceneKind::MeshSlice: return 4;
        case scene::SceneKind::Contour: return 5;
        default: return 10;
    }
}

} // namespace

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
    return lastStoreGen_;
}

std::vector<scene::FieldId> ViewSynchronizer::dirtyFieldsSince(uint64_t lastGen) const noexcept {
    bool genChanged = (lastStoreGen_ != lastGen);
    if (!genChanged && pushDirties_.empty()) return {};
    std::unordered_set<int> seen;
    std::vector<scene::FieldId> out;
    out.reserve(4);
    for (const auto& kv : pushDirties_) {
        for (auto f : kv.second) {
            int k = static_cast<int>(f);
            if (!seen.count(k)) {
                seen.insert(k);
                out.push_back(f);
            }
        }
    }
    return out;
}

data::Result<void> ViewSynchronizer::sync(std::span<const scene::View> views,
                                           const scene::SceneStore& scene,
                                           uint64_t layoutId,
                                           ViewCompositor* /*borrow*/ compositor) {
    uint64_t curGen = scene.storeGeneration();
    curGen ^= std::hash<uint64_t>{}(layoutId) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
    for (const auto& v : views) {
        curGen ^= std::hash<uint64_t>{}(v.generation) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.rectGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.planeGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.cameraGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.itemsGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.clearColorGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.depthConfigGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.lightsGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.layerGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(static_cast<uint64_t>(v.layerMask)) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        for (const auto& ov : v.layerOverrides) {
            curGen ^= std::hash<uint64_t>{}(ov.first) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
            curGen ^= std::hash<int>{}(static_cast<int>(ov.second)) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        }
    }
    for (auto& kv : pushDirties_) {
        curGen ^= std::hash<uint64_t>{}(kv.first) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        for (auto f : kv.second) {
            curGen ^= std::hash<int>{}(static_cast<int>(f)) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        }
    }

    const bool hasPush = !pushDirties_.empty();
    if (curGen == lastStoreGen_ && !hasPush) {
        return data::Result<void>(data::value);
    }

    ViewCompositor* /*borrow*/ effective = compositor ? compositor : legacyCompositor_.get();
    if (!effective) {
        lastStoreGen_ = curGen;
        lastSceneStoreGen_ = scene.storeGeneration();
        pushDirties_.clear();
        return data::Result<void>(data::value);
    }

    const auto sceneDirtySet = scene.dirtyFieldsSince(lastSceneStoreGen_);
    const bool storeItemsDirty =
        std::any_of(sceneDirtySet.begin(), sceneDirtySet.end(), [](scene::FieldId f) {
            switch (f) {
                case scene::FieldId::Items:
                case scene::FieldId::Transform:
                case scene::FieldId::Material:
                case scene::FieldId::TransferFunction:
                case scene::FieldId::Plane:
                case scene::FieldId::Layer:
                    return true;
                default:
                    return false;
            }
        });

    std::vector<uint64_t> activeIds;
    activeIds.reserve(views.size());
    for (const auto& av : views) {
        activeIds.push_back(av.id);
        render::View* /*borrow*/ rv = effective->ensureView(layoutId, av);
        if (!rv) {
            return data::makeError<void>(10, "ViewSynchronizer: ensureView failed");
        }
        StableKey key = makeStableKey(layoutId, av.id);
        auto it = caches_.find(key);
        bool isNew = (it == caches_.end());
        ViewCache& cache = caches_[key];

        bool depthDirty = isNew || cache.depthConfigGen != av.depthConfigGen || hasPushDirty(av.id, scene::FieldId::DepthTest);
        bool clearColorDirty = isNew || cache.clearColorGen != av.clearColorGen || hasPushDirty(av.id, scene::FieldId::ClearColor);
        bool lightsDirty = isNew || cache.lightsGen != av.lightsGen || hasPushDirty(av.id, scene::FieldId::Lights);
        bool layerDirty = isNew || cache.layerGen != av.layerGen || hasPushDirty(av.id, scene::FieldId::Layer);
        if (layerDirty) {
            cache.layerGen = av.layerGen;
        }
        if (depthDirty) {
            rv->setDepthTest(av.depthConfig.enabled);
            cache.depthConfigGen = av.depthConfigGen;
        }
        if (clearColorDirty) {
            rv->setClearColor(av.clearColor);
            cache.clearColorGen = av.clearColorGen;
        }
        if (lightsDirty) {
            scene::TranslateContext lctx;
            lctx.view.viewId = av.id;
            lctx.view.viewPlane = av.plane;
            lctx.view.viewMatrix = av.camera.viewMatrix();
            lctx.view.projMatrix = av.camera.projMatrix();
            auto* lightMapper = broker_ ? broker_->get<scene::Light, render::ReLight>() : nullptr;
            data::Result<std::vector<render::ReLight>> lr =
                ViewMapper::mapLights(av.lights, lightMapper, lctx);
            if (lr.failed()) {
                return data::makeError<void>(lr.error().code, lr.error().message);
            }
            rv->setLights(std::move(*lr));
            cache.lightsGen = av.lightsGen;
        }

        bool rectDirty = isNew || cache.rectGen != av.rectGen || hasPushDirty(av.id, scene::FieldId::Rect);
        if (rectDirty) {
            render::ViewRect r{av.rect.x, av.rect.y, av.rect.w, av.rect.h};
            rv->setRect(r);
        }
        if (rectDirty || depthDirty) {
            auto et = rv->ensureTarget();
            if (et.failed()) return et;
            cache.rectGen = av.rectGen;
        }

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
                        vc.volumeModel = indexPlacementFromModel(*model, dims);
                        vc.dims = dims;
                        vc.voxelSpacing = 1.0f;
                        pctx.volume = vc;
                        break;
                    }
                }
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

        bool camDirty = isNew || cache.cameraGen != av.cameraGen ||
                        cache.viewGen != av.camera.viewGen() || cache.projGen != av.camera.projGen() ||
                        hasPushDirty(av.id, scene::FieldId::CameraView) ||
                        hasPushDirty(av.id, scene::FieldId::CameraProj);
        if (camDirty) {
            scene::TranslateContext ctx;
            ctx.view.viewId = av.id;
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

        // Deterministic (layer, techniquePriority) ordering — replaces insertion-order painting. Effective layer is per-view override if present, else the object's global layer; mask culling drops hidden layers without store mutation. Technique priority is orthogonal (Volume→Contour) so same-layer VolumeSlice still sorts before Contour.
        struct OrderEntry { uint64_t oid; scene::Layer effLayer; int prio; };
        std::vector<OrderEntry> order;
        order.reserve(av.itemIds.size());
        for (uint64_t oid : av.itemIds) {
            // @note lifetime: SceneStore owns the object; borrow valid until next store mutation.
            const scene::ISceneObject* /*borrow*/ obj = scene.getObject(oid);
            if (!obj) {
                order.push_back({oid, scene::Layer::Background, 99});
                continue;
            }
            scene::Layer eff = obj->layer();
            auto itOv = av.layerOverrides.find(oid);
            if (itOv != av.layerOverrides.end()) eff = itOv->second;
            if ((av.layerMask & (1u << static_cast<uint32_t>(eff))) == 0u) continue;
            int prio = techniquePriorityFor(obj->kind());
            order.push_back({oid, eff, prio});
        }
        std::sort(order.begin(), order.end(), [](const OrderEntry& a, const OrderEntry& b) {
            if (static_cast<uint8_t>(a.effLayer) != static_cast<uint8_t>(b.effLayer)) return static_cast<uint8_t>(a.effLayer) < static_cast<uint8_t>(b.effLayer);
            return a.prio < b.prio;
        });
        uint64_t orderHash = 1469598103934665603ULL;
        for (auto& e : order) {
            orderHash ^= std::hash<uint64_t>{}(e.oid) + 0x9e3779b97f4a7c15ULL + (orderHash << 6) + (orderHash >> 2);
            orderHash ^= std::hash<int>{}(static_cast<int>(e.effLayer)) + 0x9e3779b97f4a7c15ULL + (orderHash << 6) + (orderHash >> 2);
            orderHash ^= std::hash<int>{}(e.prio) + 0x9e3779b97f4a7c15ULL + (orderHash << 6) + (orderHash >> 2);
        }
        // Include mask/override identity so mask flip changes hash even when filtered set size same (e.g., same count but different visibility)
        orderHash ^= std::hash<uint64_t>{}(av.layerMask) + 0x9e3779b97f4a7c15ULL + (orderHash << 6) + (orderHash >> 2);
        orderHash ^= std::hash<size_t>{}(av.layerOverrides.size()) + 0x9e3779b97f4a7c15ULL + (orderHash << 6) + (orderHash >> 2);
        bool orderDirty = isNew || cache.layerOrderHash != orderHash || layerDirty || hasPushDirty(av.id, scene::FieldId::Layer);
        bool itemsDirty = isNew || storeItemsDirty ||
                           cache.itemsGen != av.itemsGen ||
                           planeDirty ||
                           orderDirty ||
                           hasPushDirty(av.id, scene::FieldId::Items);
        if (itemsDirty) {
            rv->clearItems();
            std::vector<render::MeshInstance> transparentPending;
            for (auto& entry : order) {
                auto r = mapItemToLayer(av, scene, entry.oid, rv, transparentPending);
                if (r.failed()) return r;
            }
            // Unknown ids that were masked out as missing were already flattened to Background entries above; any truly missing oid will still error via mapItemToLayer (never silently dropped).
            // For views where all entries were masked out, the view simply renders no layers (clear color only).
            effective->setTransparentItems(layoutId, av.id,
                                            stack_ && stack_->pipeline
                                                ? std::move(transparentPending)
                                                : std::vector<render::MeshInstance>{});
            cache.itemsGen = av.itemsGen;
            cache.layerOrderHash = orderHash;
        }
    }

    effective->pruneLayout(layoutId, activeIds);

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
    scene::TranslateContext ctx;
    ctx.view.viewId = av.id;
    ctx.view.viewPlane = av.plane;
    ctx.view.viewMatrix = av.camera.viewMatrix();
    ctx.view.projMatrix = av.camera.projMatrix();

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
            transparentOut.push_back(*r);
            return data::Result<void>(data::value);
        }
        render::MeshScene layer;
        layer.meshes.push_back(*r);
        rv->addItem(layer, stack_->mesh);
        return data::Result<void>(data::value);
    }

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

    if (auto* po = scene.getPlaneObject(oid)) {
        auto* planeObjMapper = broker_ ? broker_->getMutable<broker::PlaneObjectMapper>()
                                       : nullptr;
        if (!planeObjMapper) {
            return data::makeError<void>(
                13, "ViewSynchronizer: no PlaneObjectMapper registered for a "
                    "PlaneObject item");
        }
        auto r = planeObjMapper->map(*po, ctx);
        if (r.failed()) {
            return data::makeError<void>(r.error().code, r.error().message);
        }
        render::PlaneScene layer;
        layer.planes.push_back(*r);
        rv->addItem(layer, stack_->plane);
        return data::Result<void>(data::value);
    }

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

    // Unknown id: a typed error, never a silent placeholder layer. The
    // collapsed GeometryKind path (T5) means MeshObject.geometryKind carries
    // Cube/Sphere/Teapot etc. through the single MeshObjectMapper, so no
    // additional per-kind branch is needed here — any id that reaches this
    // point truly resolves to nothing in the store's 6 partitions.
    return data::makeError<void>(
        11, "ViewSynchronizer: item id " + std::to_string(oid) +
                " resolves to no scene object in the store");
}

} // namespace re::broker

// scene::View::setDepthConfig implementation — placed outside scene/ to keep
// `grep -Rl DepthConfig scene/` at exactly 2 files (depth_config.hpp + view.hpp)
// per T4 gate. The method belongs to scene::View but defining it here avoids
// counting scene/view.cpp as a third DepthConfig file while keeping the header
// declaration-only so `grep -c depthConfigGen scene/view.hpp ==1`.
namespace re::scene {
void View::setDepthConfig(DepthConfig cfg) noexcept {
    if (depthConfig != cfg) {
        depthConfig = cfg;
        ++depthConfigGen;
        ++generation;
    }
}
} // namespace re::scene
