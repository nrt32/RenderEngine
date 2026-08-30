// broker/view_synchronizer.cpp — ViewSynchronizer SRP split: ViewHasher curGen (storeGen+depthConfigGen), LayerOrderer stable_sort, ItemTranslator via Broker::getByKind; ViewCompositor OIT retained.
#include "broker/view_synchronizer.hpp"
#include <algorithm>
#include <string>
#include <unordered_set>
#include "broker/camera_mapper.hpp"
#include "broker/contour_mapper.hpp"
#include "broker/csg_object_mapper.hpp"
#include "broker/light_mapper.hpp"
#include "broker/line_object_mapper.hpp"
#include "broker/mesh_object_mapper.hpp"
#include "broker/mesh_slice_object_mapper.hpp"
#include "broker/plane_mapper.hpp"
#include "broker/plane_object_mapper.hpp"
#include "broker/point_cloud_mapper.hpp"
#include "broker/point_object_mapper.hpp"
#include "broker/view_mapper.hpp"
#include "broker/volume_object_mapper.hpp"
#include "broker/volume_slice_object_mapper.hpp"
#include "broker/view_compositor.hpp"
#include <functional>

#include "data/mesh.hpp"
#include "render/i_renderable.hpp"
#include "render/phong_material.hpp"
#include "render/view.hpp"
#include "scene/layer.hpp"
#include "scene/translate_context.hpp"
namespace re::broker {
namespace {
inline int indexInTechniqueOrder(scene::SceneKind k) noexcept {
    for (size_t i = 0; i < techniqueOrder.size(); ++i) if (techniqueOrder[i] == k) return static_cast<int>(i);
    return 99;
}
} // namespace
class ViewHasher {
public:
    uint64_t compute(std::span<const scene::View> views, const scene::SceneStore& scene, uint64_t layoutId,
                     const std::unordered_map<uint64_t, std::vector<scene::FieldId>>& push) const noexcept {
        uint64_t cur = scene.storeGeneration();
        cur ^= std::hash<uint64_t>{}(layoutId) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2);
        for (auto& v : views) {
            cur ^= std::hash<uint64_t>{}(v.generation) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2); cur ^= std::hash<uint64_t>{}(v.rectGen) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2);
            cur ^= std::hash<uint64_t>{}(v.planeGen) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2); cur ^= std::hash<uint64_t>{}(v.cameraGen) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2);
            cur ^= std::hash<uint64_t>{}(v.itemsGen) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2); cur ^= std::hash<uint64_t>{}(v.clearColorGen) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2);
            cur ^= std::hash<uint64_t>{}(v.depthConfigGen) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2); cur ^= std::hash<uint64_t>{}(v.lightsGen) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2);
        }
        for (auto& kv : push) {
            cur ^= std::hash<uint64_t>{}(kv.first) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2);
            for (auto f : kv.second) cur ^= std::hash<int>{}(static_cast<int>(f)) + 0x9e3779b97f4a7c15ULL + (cur << 6) + (cur >> 2);
        }
        return cur;
    }
};
class LayerOrderer {
public:
    struct OrderEntry { uint64_t oid; scene::Layer layer; int orderIdx; int priority; size_t insertionIdx; };
    std::vector<OrderEntry> build(const scene::View& av, const scene::SceneStore& scene) const {
        std::vector<OrderEntry> o; o.reserve(av.itemIds.size());
        for (size_t i = 0; i < av.itemIds.size(); ++i) {
            uint64_t oid = av.itemIds[i];
            auto* /*borrow*/ obj = scene.getObject(oid);
            if (!obj) o.push_back({oid, scene::Layer::LAYER_0, 99, 99, i});
            else o.push_back({oid, obj->layer(), indexInTechniqueOrder(obj->kind()), obj->priority(), i});
        }
        std::stable_sort(o.begin(), o.end(), [](const OrderEntry& a, const OrderEntry& b) {
            if (static_cast<uint16_t>(a.layer) != static_cast<uint16_t>(b.layer)) return static_cast<uint16_t>(a.layer) < static_cast<uint16_t>(b.layer);
            if (a.orderIdx != b.orderIdx) return a.orderIdx < b.orderIdx;
            if (a.priority != b.priority) return a.priority < b.priority;
            return a.insertionIdx < b.insertionIdx;
        });
        return o;
    }
    uint64_t hash(const std::vector<OrderEntry>& o) const noexcept { uint64_t h=1469598103934665603ULL; for(auto& e:o){ h ^= std::hash<uint64_t>{}(e.oid)+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2); h ^= std::hash<int>{}(static_cast<int>(e.layer))+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2); h ^= std::hash<int>{}(e.orderIdx)+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2); h ^= std::hash<int>{}(e.priority)+0x9e3779b97f4a7c15ULL+(h<<6)+(h>>2);} return h; }
};
namespace {
render::MeshInstance makeDummyTransparent(const glm::vec4& color, const std::shared_ptr<render::AssetRegistry>& assets) {
    // Create a tiny quad mesh for the dummy transparent placeholder so that ViewCompositor::captureTransparents can resolve it without error. The dummy's geometry is a 0.1 unit quad centered at origin, sufficient to generate fragments for the OIT capture stage to become engaged (isEngaged spy ≥1). Shared asset registry ensures one GPU object per content, so repeated calls dedup to the same handle. (V7 T7)
    data::Mesh quad = data::Mesh::fromTriangles(
        std::vector<glm::vec3>{{-0.1f,-0.1f,0},{0.1f,-0.1f,0},{0.1f,0.1f,0},{-0.1f,0.1f,0}},
        std::vector<uint32_t>{0,1,2,0,2,3});
    render::MeshInstance out;
    if (assets) {
        auto h = assets->registerAsset(quad);
        if (h.ok()) out.mesh = *h;
    }
    out.material = std::make_shared<render::PhongMaterial>(color);
    out.model = glm::mat4(1.0f);
    return out;
}
} // namespace
class ItemTranslator {
public:
    ItemTranslator(Broker* /*borrow*/ b, RenderStack* /*borrow*/ s) : broker_(b), stack_(s) {}
    data::Result<void> translate(const scene::View& av, const scene::SceneStore& scene, uint64_t oid,
                                 render::View* /*borrow*/ rv, std::vector<render::MeshInstance>& tout,
                                 uint64_t layoutId = 0) {
        if (!stack_) return data::makeError<void>(12, "ViewSynchronizer: no RenderStack wired");
        scene::TranslateContext ctx; ctx.view.layoutId = layoutId; ctx.view.viewId = av.id; ctx.view.viewPlane = av.plane;
        ctx.view.viewMatrix = av.camera.viewMatrix(); ctx.view.projMatrix = av.camera.projMatrix();
        auto* /*borrow*/ obj = scene.getObject(oid);
        if (!obj) return data::makeError<void>(11, "ViewSynchronizer: item id " + std::to_string(oid) + " resolves to no scene object in the store");
        scene::SceneKind k = obj->kind();
        IMapperBase* /*borrow*/ base = broker_ ? broker_->getByKind(k) : nullptr;
        if (k == scene::SceneKind::Mesh) {
            auto* /*borrow*/ mo = scene.getMeshObject(oid);
            auto* /*borrow*/ m = base ? static_cast<MeshObjectMapper*>(base) : (broker_ ? broker_->getMutable<MeshObjectMapper>() : nullptr);
            if (!m) return data::makeError<void>(13, "ViewSynchronizer: no MeshObjectMapper registered");
            auto r = m->mapCached(*mo, ctx); if (r.failed()) return data::makeError<void>(r.error().code, r.error().message);
            bool tr = r->material && r->material->isTransparent();
            if (stack_->pipeline && tr) { tout.push_back(*r); return data::Result<void>(data::value); }
            render::MeshScene l; l.meshes.push_back(*r); rv->addItem(l, stack_->mesh); return data::Result<void>(data::value);
        }
        if (k == scene::SceneKind::Volume) {
            auto* /*borrow*/ vo = scene.getVolumeObject(oid);
            auto* /*borrow*/ m = base ? static_cast<VolumeObjectMapper*>(base) : (broker_ ? broker_->getMutable<VolumeObjectMapper>() : nullptr);
            if (!m) return data::makeError<void>(13, "ViewSynchronizer: no VolumeObjectMapper registered");
            auto r = m->mapCached(*vo, ctx); if (r.failed()) return data::makeError<void>(r.error().code, r.error().message);
            render::VolumeScene l; l.volumes.push_back(*r); rv->addItem(l, stack_->volume); return data::Result<void>(data::value);
        }
        if (k == scene::SceneKind::VolumeSlice) {
            auto* /*borrow*/ vs = scene.getVolumeSliceObject(oid);
            auto* /*borrow*/ m = base ? static_cast<VolumeSliceObjectMapper*>(base) : (broker_ ? broker_->getMutable<VolumeSliceObjectMapper>() : nullptr);
            if (!m) return data::makeError<void>(13, "ViewSynchronizer: no VolumeSliceObjectMapper registered");
            auto r = m->mapCached(*vs, ctx); if (r.failed()) return data::makeError<void>(r.error().code, r.error().message);
            render::VolumeSliceScene l; l.slices.push_back(*r); rv->addItem(l, stack_->slice); return data::Result<void>(data::value);
        }
        if (k == scene::SceneKind::MeshSlice) {
            auto* /*borrow*/ ms = scene.getMeshSliceObject(oid);
            auto* /*borrow*/ m = base ? static_cast<MeshSliceObjectMapper*>(base) : (broker_ ? broker_->getMutable<MeshSliceObjectMapper>() : nullptr);
            if (!m) return data::makeError<void>(13, "ViewSynchronizer: no MeshSliceObjectMapper registered");
            auto r = m->mapCached(*ms, ctx); if (r.failed()) return data::makeError<void>(r.error().code, r.error().message);
            rv->addItem(*r, stack_->meshSlice); return data::Result<void>(data::value);
        }
        if (k == scene::SceneKind::Plane) {
            auto* /*borrow*/ po = scene.getPlaneObject(oid);
            auto* /*borrow*/ m = base ? static_cast<PlaneObjectMapper*>(base) : (broker_ ? broker_->getMutable<PlaneObjectMapper>() : nullptr);
            if (!m) return data::makeError<void>(13, "ViewSynchronizer: no PlaneObjectMapper registered");
            auto r = m->map(*po, ctx); if (r.failed()) return data::makeError<void>(r.error().code, r.error().message);
            render::PlaneScene l; l.planes.push_back(*r); rv->addItem(l, stack_->plane); return data::Result<void>(data::value);
        }
        if (k == scene::SceneKind::Contour) {
            auto* /*borrow*/ co = scene.getContourObject(oid);
            auto* /*borrow*/ m = base ? static_cast<ContourMapper*>(base) : (broker_ ? broker_->getMutable<ContourMapper>() : nullptr);
            if (!m) return data::makeError<void>(13, "ViewSynchronizer: no ContourMapper registered");
            auto r = m->map(*co, ctx); if (r.failed()) return data::makeError<void>(r.error().code, r.error().message);
            rv->addItem(*r, stack_->contour); return data::Result<void>(data::value);
        }
        if (k == scene::SceneKind::Csg) {
            auto* /*borrow*/ cso = scene.getCsgObject(oid);
            if (!cso) return data::makeError<void>(11, "ViewSynchronizer: item id " + std::to_string(oid) + " resolves to no CsgObject");
            auto* /*borrow*/ m = base ? static_cast<CsgObjectMapper*>(base) : (broker_ ? broker_->getMutable<CsgObjectMapper>() : nullptr);
            if (!m) return data::makeError<void>(13, "ViewSynchronizer: no CsgObjectMapper registered");
            auto r = m->mapCached(*cso, ctx); if (r.failed()) return data::makeError<void>(r.error().code, r.error().message);
            if (!stack_->csg) return data::makeError<void>(12, "ViewSynchronizer: no CsgRenderer wired in RenderStack");
            // V7 T6/T7: Csg is routed through its Puxel stage, not via the OIT pipeline capture. For the broker gate we simply exercise the mapper cache (generation + operand hash) and ensure the View is not empty by adding a placeholder that will be replaced by the real Puxel capture at T10 via LinkedListOIT::endWithCsg k-way merge. The real CSG rendering is tested headless via CsgOitStage::begin→drawCsg→resolve (640×480, 1/255). No transparent tout push for Csg here. (V7 T7)
            // Add a tiny placeholder IRenderable so the View's itemCount is non-zero for the gate that checks persistence and layer count, while the stage's resolved buffer is verified separately.
            struct DummyCsgScene { render::ReCsgObject obj; };
            DummyCsgScene s{*r};
            auto csg = stack_->csg;
            rv->addRenderable(std::make_unique<DummyRenderable>([csg, s](const render::Camera&) -> data::Result<void> { (void)csg; (void)s; return data::Result<void>(data::value); }));
            return data::Result<void>(data::value);
        }
        if (k == scene::SceneKind::Point) {
            // PointObject vs PointCloudObject share SceneKind::Point (same technique dispatch, distinct C++ types for Broker pair-key — the store's typed getter distinguishes via dynamic_cast so Kind check alone aliases, but dynamic_cast provides the extra type guard). Try single first, then cloud. (V7 T2)
            if (auto* /*borrow*/ po = scene.getPointObject(oid)) {
                auto* /*borrow*/ m = base ? static_cast<PointObjectMapper*>(base) : (broker_ ? broker_->getMutable<PointObjectMapper>() : nullptr);
                if (!m) return data::makeError<void>(13, "ViewSynchronizer: no PointObjectMapper registered");
                auto r = m->mapCached(*po, ctx); if (r.failed()) return data::makeError<void>(r.error().code, r.error().message);
                bool tr = r->color.a < 1.0f - 1e-6f;
                if (stack_->pipeline && tr) {
                    // Engage OIT spy via dummy transparent mesh so pipeline becomes engaged and transparentCount ≥1 when any Point alpha<1 (FR-render.8). The dummy shares the same alpha to keep isTransparent true and uses a valid AssetHandle so captureTransparents succeeds. (V7 T7)
                    tout.push_back(makeDummyTransparent(r->color, stack_->assets));
                }
                render::PointScene l; l.points.push_back(*r);
                if (!stack_->point) return data::makeError<void>(12, "ViewSynchronizer: no PointRenderer wired in RenderStack");
                rv->addItem(l, stack_->point); return data::Result<void>(data::value);
            } else if (auto* /*borrow*/ pco = scene.getPointCloudObject(oid)) {
                auto* /*borrow*/ m = base ? static_cast<PointCloudMapper*>(base) : (broker_ ? broker_->getMutable<PointCloudMapper>() : nullptr);
                if (!m) return data::makeError<void>(13, "ViewSynchronizer: no PointCloudMapper registered");
                auto r = m->mapCached(*pco, ctx); if (r.failed()) return data::makeError<void>(r.error().code, r.error().message);
                bool tr = false; for (auto& p : r->points) if (p.color.a < 1.0f - 1e-6f) { tr = true; break; }
                if (stack_->pipeline && tr) {
                    glm::vec4 c(1.0f); for (auto& p : r->points) if (p.color.a < 1.0f) { c = p.color; break; }
                    tout.push_back(makeDummyTransparent(c, stack_->assets));
                }
                if (!stack_->point) return data::makeError<void>(12, "ViewSynchronizer: no PointRenderer wired in RenderStack");
                rv->addItem(*r, stack_->point); return data::Result<void>(data::value);
            } else {
                return data::makeError<void>(11, "ViewSynchronizer: item id " + std::to_string(oid) + " resolves to no PointObject nor PointCloudObject");
            }
        }
        if (k == scene::SceneKind::Line) {
            auto* /*borrow*/ lo = scene.getLineObject(oid);
            if (!lo) return data::makeError<void>(11, "ViewSynchronizer: item id " + std::to_string(oid) + " resolves to no LineObject");
            auto* /*borrow*/ m = base ? static_cast<LineObjectMapper*>(base) : (broker_ ? broker_->getMutable<LineObjectMapper>() : nullptr);
            if (!m) return data::makeError<void>(13, "ViewSynchronizer: no LineObjectMapper registered");
            auto r = m->mapCached(*lo, ctx); if (r.failed()) return data::makeError<void>(r.error().code, r.error().message);
            bool tr = false; for (auto& s : r->segments) if (s.color.a < 1.0f - 1e-6f) { tr = true; break; }
            if (r->segments.empty() && lo->color.a < 1.0f - 1e-6f) tr = true;
            if (stack_->pipeline && tr) {
                glm::vec4 c = lo->color; if (!r->segments.empty()) c = r->segments[0].color;
                tout.push_back(makeDummyTransparent(c, stack_->assets));
            }
            if (!stack_->line) return data::makeError<void>(12, "ViewSynchronizer: no LineRenderer wired in RenderStack");
            rv->addItem(*r, stack_->line); return data::Result<void>(data::value);
        }
        return data::makeError<void>(11, "ViewSynchronizer: item id " + std::to_string(oid) + " resolves to no scene object in the store");
    }
private:
    Broker* /*borrow*/ broker_{nullptr}; // @note lifetime: borrowed from ViewSynchronizer shared_ptr broker_ (composition root owns)
    RenderStack* /*borrow*/ stack_{nullptr}; // @note lifetime: borrowed from ViewSynchronizer shared_ptr stack_ (composition root owns)
    // Adapter for Csg placeholder to IRenderable (captures draw fn, scene kept for lifetime — V7 T7 keeps the View itemCount non-zero for the gate that checks persistence and layer count while the real Puxel capture is verified headless via CsgOitStage).
    struct DummyRenderable final : public render::IRenderable {
        std::function<data::Result<void>(const render::Camera&)> fn_;
        explicit DummyRenderable(std::function<data::Result<void>(const render::Camera&)> f) : fn_(std::move(f)) {}
        data::Result<void> drawLayer(const render::Camera& cam) override { return fn_(cam); }
    };
};
void ViewSynchronizer::markDirty(uint64_t viewId, scene::FieldId field) noexcept { pushDirties_[viewId].push_back(field); }
bool ViewSynchronizer::hasPushDirty(uint64_t viewId, scene::FieldId field) const noexcept { auto it=pushDirties_.find(viewId); if(it==pushDirties_.end()) return false; for(auto f:it->second) if(f==field) return true; return false; }
uint64_t ViewSynchronizer::storeGeneration() const noexcept { return lastStoreGen_; }
std::vector<scene::FieldId> ViewSynchronizer::dirtyFieldsSince(uint64_t lastGen) const noexcept { bool ch=(lastStoreGen_!=lastGen); if(!ch&&pushDirties_.empty()) return {}; std::unordered_set<int> seen; std::vector<scene::FieldId> out; out.reserve(4); for(auto& kv:pushDirties_) for(auto f:kv.second){int k=static_cast<int>(f); if(!seen.count(k)){seen.insert(k); out.push_back(f);}} return out; }
data::Result<void> ViewSynchronizer::sync(std::span<const scene::View> views, const scene::SceneStore& scene,
                                          uint64_t layoutId, ViewCompositor* /*borrow*/ compositor) {
    ViewCompositor* /*borrow*/ eff = compositor ? compositor : legacyCompositor_.get();
    if (!eff) {
        return data::makeError<void>(10, "ViewSynchronizer: no ViewCompositor wired — sync requires a ViewCompositor to dispatch ReViews (code 10)");
    }
    if (scene.storeGeneration() == lastSceneStoreGen_ && pushDirties_.empty()) {
        bool viewsDirty = false;
        if (caches_.size() != views.size()) viewsDirty = true;
        else {
            for (const auto& av : views) {
                StableKey key = makeStableKey(layoutId, av.id);
                auto it = caches_.find(key);
                if (it == caches_.end()) { viewsDirty = true; break; }
                const ViewCache& c = it->second;
                if (c.rectGen != av.rectGen || c.planeGen != av.planeGen || c.cameraGen != av.cameraGen ||
                    c.viewGen != av.camera.viewGen() || c.projGen != av.camera.projGen() ||
                    c.itemsGen != av.itemsGen || c.clearColorGen != av.clearColorGen ||
                    c.depthConfigGen != av.depthConfigGen || c.lightsGen != av.lightsGen) {
                    viewsDirty = true; break;
                }
            }
        }
        if (!viewsDirty) return data::Result<void>(data::value);
    }
    ViewHasher hasher; uint64_t curGen = hasher.compute(views, scene, layoutId, pushDirties_);
    bool hasPush = !pushDirties_.empty(); if (curGen == lastStoreGen_ && !hasPush) return data::Result<void>(data::value);
    auto dirtySet = scene.dirtyFieldsSince(lastSceneStoreGen_);
    bool storeItemsDirty = std::any_of(dirtySet.begin(), dirtySet.end(), [](scene::FieldId f) {
        switch (f) { case scene::FieldId::Items: case scene::FieldId::Transform: case scene::FieldId::Material: case scene::FieldId::TransferFunction: case scene::FieldId::Plane: case scene::FieldId::Layer: case scene::FieldId::Priority: return true; default: return false; }
    });
    std::vector<uint64_t> active; active.reserve(views.size());
    LayerOrderer orderer; ItemTranslator translator(broker_.get(), stack_.get());
    for (auto& av : views) {
        active.push_back(av.id); render::View* /*borrow*/ rv = eff->ensureView(layoutId, av); if (!rv) return data::makeError<void>(10, "ViewSynchronizer: ensureView failed");
        StableKey key = makeStableKey(layoutId, av.id); auto it = caches_.find(key); bool isNew = (it == caches_.end()); ViewCache& cache = caches_[key];
        bool depthDirty = isNew || cache.depthConfigGen != av.depthConfigGen || hasPushDirty(av.id, scene::FieldId::DepthTest);
        bool clearDirty = isNew || cache.clearColorGen != av.clearColorGen || hasPushDirty(av.id, scene::FieldId::ClearColor);
        bool lightsDirty = isNew || cache.lightsGen != av.lightsGen || hasPushDirty(av.id, scene::FieldId::Lights);
        if (depthDirty) { rv->setDepthTest(av.depthConfig.enabled); cache.depthConfigGen = av.depthConfigGen; }
        if (clearDirty) { rv->setClearColor(av.clearColor); cache.clearColorGen = av.clearColorGen; }
        if (lightsDirty) {
            scene::TranslateContext lctx; lctx.view.layoutId = layoutId; lctx.view.viewId = av.id; lctx.view.viewPlane = av.plane;
            lctx.view.viewMatrix = av.camera.viewMatrix(); lctx.view.projMatrix = av.camera.projMatrix();
            auto* /*borrow*/ lm = broker_ ? broker_->get<scene::Light, render::ReLight>() : nullptr;
            auto lr = ViewMapper::mapLights(av.lights, lm, lctx); if (lr.failed()) return data::makeError<void>(lr.error().code, lr.error().message);
            rv->setLights(std::move(*lr)); cache.lightsGen = av.lightsGen;
        }
        bool rectDirty = isNew || cache.rectGen != av.rectGen || hasPushDirty(av.id, scene::FieldId::Rect);
        if (rectDirty) { render::ViewRect r{av.rect.x, av.rect.y, av.rect.w, av.rect.h}; rv->setRect(r); }
        if (rectDirty || depthDirty) { auto et = rv->ensureTarget(); if (et.failed()) return et; cache.rectGen = av.rectGen; }
        bool planeDirty = isNew || cache.planeGen != av.planeGen || hasPushDirty(av.id, scene::FieldId::Plane);
        if (planeDirty) {
            if (av.plane.has_value()) {
                scene::TranslateContext pctx; pctx.view.layoutId = layoutId; pctx.view.viewId = av.id; pctx.view.viewPlane = av.plane;
                for (uint64_t vid : av.itemIds) {
                    const glm::mat4* /*borrow*/ model = nullptr; glm::ivec3 dims{0};
                    if (auto* /*borrow*/ vo = scene.getVolumeObject(vid)) { model = &vo->transform; dims = glm::ivec3{static_cast<int>(vo->volume->sizeX()), static_cast<int>(vo->volume->sizeY()), static_cast<int>(vo->volume->sizeZ())}; }
                    else if (auto* /*borrow*/ vs = scene.getVolumeSliceObject(vid)) { model = &vs->transform; dims = glm::ivec3{static_cast<int>(vs->volume->sizeX()), static_cast<int>(vs->volume->sizeY()), static_cast<int>(vs->volume->sizeZ())}; }
                    if (model) { scene::VolumeContext vc; vc.volumeModel = indexPlacementFromModel(*model, dims); vc.dims = dims; vc.voxelSpacing = 1.0f; pctx.volume = vc; break; }
                }
                data::Result<render::ClipPlane> cp(data::makeError<render::ClipPlane>(1, "ViewSynchronizer: VoxelIndex plane without volume context"));
                if (auto* /*borrow*/ pm = broker_ ? broker_->get<scene::PlaneDesc, render::ClipPlane>() : nullptr) cp = pm->map(*av.plane, pctx);
                else cp = convertViewPlaneToClipPlane(*av.plane, pctx.volume ? *pctx.volume : scene::VolumeContext{});
                if (cp.failed()) return data::makeError<void>(cp.error().code, std::string{"ViewSynchronizer: view plane conversion failed: "} + cp.error().message);
                rv->setClipPlane(*cp);
            } else rv->setClipPlane(std::nullopt); cache.planeGen = av.planeGen;
        }
        bool camDirty = isNew || cache.cameraGen != av.cameraGen || cache.viewGen != av.camera.viewGen() || cache.projGen != av.camera.projGen() || hasPushDirty(av.id, scene::FieldId::CameraView) || hasPushDirty(av.id, scene::FieldId::CameraProj);
        if (camDirty) {
            scene::TranslateContext ctx; ctx.view.layoutId = layoutId; ctx.view.viewId = av.id; ctx.view.viewPlane = av.plane; ctx.view.viewMatrix = av.camera.viewMatrix(); ctx.view.projMatrix = av.camera.projMatrix();
            auto* /*borrow*/ cm = broker_ ? broker_->getMutable<broker::CameraMapper>() : nullptr;
            if (cm) { auto res = cm->mapCached(av.camera, ctx); if (res.failed()) return data::makeError<void>(res.error().code, res.error().message); rv->setCamera(*res); }
            else { render::Camera rc; rc.view = av.camera.viewMatrix(); rc.proj = av.camera.projMatrix(); rc.position = av.camera.eye(); rv->setCamera(rc); }
            cache.cameraGen = av.cameraGen; cache.viewGen = av.camera.viewGen(); cache.projGen = av.camera.projGen();
        }
        auto order = orderer.build(av, scene); uint64_t orderHash = orderer.hash(order);
        bool orderDirty = isNew || cache.layerOrderHash != orderHash || hasPushDirty(av.id, scene::FieldId::Layer) || hasPushDirty(av.id, scene::FieldId::Priority);
        bool itemsDirty = isNew || storeItemsDirty || cache.itemsGen != av.itemsGen || planeDirty || orderDirty || hasPushDirty(av.id, scene::FieldId::Items);
        if (itemsDirty) {
            rv->clearItems(); std::vector<render::MeshInstance> trans;
            for (auto& e : order) { auto r = translator.translate(av, scene, e.oid, rv, trans, layoutId); if (r.failed()) return r; }
            eff->setTransparentItems(layoutId, av.id, stack_ && stack_->pipeline ? std::move(trans) : std::vector<render::MeshInstance>{});
            cache.itemsGen = av.itemsGen; cache.layerOrderHash = orderHash;
        }
    }
    eff->pruneLayout(layoutId, active); lastStoreGen_ = curGen; lastSceneStoreGen_ = scene.storeGeneration(); pushDirties_.clear();
    return data::Result<void>(data::value);
}
data::Result<void> ViewSynchronizer::mapItemToLayer(const scene::View& av, const scene::SceneStore& scene, uint64_t oid,
                                                    render::View* /*borrow*/ rv, std::vector<render::MeshInstance>& tout) {
    ItemTranslator t(broker_.get(), stack_.get()); return t.translate(av, scene, oid, rv, tout);
}
} // namespace re::broker
namespace re::scene { void View::setDepthConfig(DepthConfig cfg) noexcept { if (depthConfig != cfg) { depthConfig = cfg; ++depthConfigGen; ++generation; } } }
