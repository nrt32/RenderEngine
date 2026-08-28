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
        curGen ^= std::hash<uint64_t>{}(v.depthTestGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
        curGen ^= std::hash<uint64_t>{}(v.lightsGen) + 0x9e3779b97f4a7c15ULL + (curGen << 6) + (curGen >> 2);
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

        bool depthDirty = isNew || cache.depthTestGen != av.depthTestGen || hasPushDirty(av.id, scene::FieldId::DepthTest);
        bool clearColorDirty = isNew || cache.clearColorGen != av.clearColorGen || hasPushDirty(av.id, scene::FieldId::ClearColor);
        bool lightsDirty = isNew || cache.lightsGen != av.lightsGen || hasPushDirty(av.id, scene::FieldId::Lights);
        if (depthDirty) {
            rv->setDepthTest(av.depthTest);
            cache.depthTestGen = av.depthTestGen;
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
            effective->setTransparentItems(layoutId, av.id,
                                            stack_ && stack_->pipeline
                                                ? std::move(transparentPending)
                                                : std::vector<render::MeshInstance>{});
            cache.itemsGen = av.itemsGen;
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
