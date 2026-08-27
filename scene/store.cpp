#include "scene/store.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace re::scene {

// ---------------------------------------------------------------------------
// SceneStore — helpers
// ---------------------------------------------------------------------------

template <typename Derived>
uint64_t SceneStore::addTypedObject_(Derived obj, std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition) {
    uint64_t id = allocId();
    obj.setId(id);
    obj.setGeneration(storeGen_ + 1);
    // Polymorphic heap allocation — the extra new per object (plus the
    // existing shared_ptr control block for the immutable asset) is amortised
    // by a future slab/arena in SceneStore (not this task). Today MeshObject
    // copy was memcpy of a 64 B value into the map node (no heap for the
    // object itself); after the move each object is make_unique<Derived> plus
    // map node (one heap for the wrapper, plus the shared asset block). Cost
    // accepted for open extension; bespoke variant visitor updates on every new
    // kind are the blocked path. T1 D.
    SceneKind kind = Derived::Kind;
    auto ptr = std::make_unique<Derived>(std::move(obj));
    partition.emplace(id, std::move(ptr));
    kindIndex_[kind].insert(id);
    ++storeGen_;
    recordDirty_(FieldId::Transform);
    recordDirty_(FieldId::Items);
    return id;
}

bool SceneStore::removeFromPartition_(uint64_t id, std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition) noexcept {
    auto it = partition.find(id);
    if (it == partition.end()) return false;
    uint64_t gen = it->second->generation();
    SceneKind kind = it->second->kind();
    tombstoneGen_[id] = gen + 1;
    auto kit = kindIndex_.find(kind);
    if (kit != kindIndex_.end()) {
        kit->second.erase(id);
        if (kit->second.empty()) kindIndex_.erase(kit);
    }
    partition.erase(it);
    ++storeGen_;
    recordDirty_(FieldId::Items);
    return true;
}

bool SceneStore::containsInPartition_(uint64_t id, const std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition) const noexcept {
    return partition.count(id) != 0u;
}

// ---------------------------------------------------------------------------
// SceneStore — add
// ---------------------------------------------------------------------------

uint64_t SceneStore::addMeshObject(MeshObject obj) { return addTypedObject_(std::move(obj), meshObjects_); }
uint64_t SceneStore::addMeshSliceObject(MeshSliceObject obj) { return addTypedObject_(std::move(obj), meshSliceObjects_); }
uint64_t SceneStore::addVolumeObject(VolumeObject obj) { return addTypedObject_(std::move(obj), volumeObjects_); }
uint64_t SceneStore::addVolumeSliceObject(VolumeSliceObject obj) { return addTypedObject_(std::move(obj), volumeSliceObjects_); }
uint64_t SceneStore::addPlaneObject(PlaneObject obj) { return addTypedObject_(std::move(obj), planeObjects_); }
uint64_t SceneStore::addContourObject(ContourObject obj) { return addTypedObject_(std::move(obj), contourObjects_); }
uint64_t SceneStore::addTeapotObject(TeapotObject obj) { return addTypedObject_(std::move(obj), teapotObjects_); }
uint64_t SceneStore::addSphereObject(SphereObject obj) { return addTypedObject_(std::move(obj), sphereObjects_); }
uint64_t SceneStore::addCubeObject(CubeObject obj) { return addTypedObject_(std::move(obj), cubeObjects_); }
uint64_t SceneStore::addCylinderObject(CylinderObject obj) { return addTypedObject_(std::move(obj), cylinderObjects_); }
uint64_t SceneStore::addTorusObject(TorusObject obj) { return addTypedObject_(std::move(obj), torusObjects_); }
uint64_t SceneStore::addConeObject(ConeObject obj) { return addTypedObject_(std::move(obj), coneObjects_); }
uint64_t SceneStore::addArrowObject(ArrowObject obj) { return addTypedObject_(std::move(obj), arrowObjects_); }
uint64_t SceneStore::addGridObject(GridObject obj) { return addTypedObject_(std::move(obj), gridObjects_); }
uint64_t SceneStore::addAxesObject(AxesObject obj) { return addTypedObject_(std::move(obj), axesObjects_); }
uint64_t SceneStore::addPointCloudObject(PointCloudObject obj) { return addTypedObject_(std::move(obj), pointCloudObjects_); }
uint64_t SceneStore::addCapsuleObject(CapsuleObject obj) { return addTypedObject_(std::move(obj), capsuleObjects_); }

uint64_t SceneStore::addObject(std::unique_ptr<ISceneObject> obj) {
    if (!obj) return 0u;
    SceneKind kind = obj->kind();
    uint64_t id = allocId();
    obj->setId(id);
    obj->setGeneration(storeGen_ + 1);
    // Route to correct partition by kind — the open dispatch that keeps store
    // closed for modification when new kinds are added (new partition line is
    // the only edit, and kindIndex_ absorbs the typed iteration without branch).
    auto emplaceIn = [&](auto& partition) {
        partition.emplace(id, std::move(obj));
    };
    switch (kind) {
        case SceneKind::Mesh: emplaceIn(meshObjects_); break;
        case SceneKind::MeshSlice: emplaceIn(meshSliceObjects_); break;
        case SceneKind::Volume: emplaceIn(volumeObjects_); break;
        case SceneKind::VolumeSlice: emplaceIn(volumeSliceObjects_); break;
        case SceneKind::Plane: emplaceIn(planeObjects_); break;
        case SceneKind::Contour: emplaceIn(contourObjects_); break;
        case SceneKind::Teapot: emplaceIn(teapotObjects_); break;
        case SceneKind::Sphere: emplaceIn(sphereObjects_); break;
        case SceneKind::Cube: emplaceIn(cubeObjects_); break;
        case SceneKind::Cylinder: emplaceIn(cylinderObjects_); break;
        case SceneKind::Torus: emplaceIn(torusObjects_); break;
        case SceneKind::Cone: emplaceIn(coneObjects_); break;
        case SceneKind::Arrow: emplaceIn(arrowObjects_); break;
        case SceneKind::Grid: emplaceIn(gridObjects_); break;
        case SceneKind::Axes: emplaceIn(axesObjects_); break;
        case SceneKind::PointCloud: emplaceIn(pointCloudObjects_); break;
        case SceneKind::Capsule: emplaceIn(capsuleObjects_); break;
        default: meshObjects_.emplace(id, std::move(obj)); break;
    }
    kindIndex_[kind].insert(id);
    ++storeGen_;
    recordDirty_(FieldId::Transform);
    recordDirty_(FieldId::Items);
    return id;
}

// ---------------------------------------------------------------------------
// SceneStore — getters (borrow)
// ---------------------------------------------------------------------------

template <typename Derived>
static const Derived* /*borrow*/ getTyped_(uint64_t id, const std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition) noexcept {
    auto it = partition.find(id);
    if (it == partition.end()) return nullptr;
    return static_cast<const Derived*>(it->second.get());
}
template <typename Derived>
static Derived* /*borrow*/ getTypedMut_(uint64_t id, std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition) noexcept {
    auto it = partition.find(id);
    if (it == partition.end()) return nullptr;
    return static_cast<Derived*>(it->second.get());
}

const MeshObject* /*borrow*/ SceneStore::getMeshObject(uint64_t id) const noexcept { return getTyped_<MeshObject>(id, meshObjects_); }
const MeshSliceObject* /*borrow*/ SceneStore::getMeshSliceObject(uint64_t id) const noexcept { return getTyped_<MeshSliceObject>(id, meshSliceObjects_); }
const VolumeObject* /*borrow*/ SceneStore::getVolumeObject(uint64_t id) const noexcept { return getTyped_<VolumeObject>(id, volumeObjects_); }
const VolumeSliceObject* /*borrow*/ SceneStore::getVolumeSliceObject(uint64_t id) const noexcept { return getTyped_<VolumeSliceObject>(id, volumeSliceObjects_); }
const PlaneObject* /*borrow*/ SceneStore::getPlaneObject(uint64_t id) const noexcept { return getTyped_<PlaneObject>(id, planeObjects_); }
const ContourObject* /*borrow*/ SceneStore::getContourObject(uint64_t id) const noexcept { return getTyped_<ContourObject>(id, contourObjects_); }
const TeapotObject* /*borrow*/ SceneStore::getTeapotObject(uint64_t id) const noexcept { return getTyped_<TeapotObject>(id, teapotObjects_); }
const SphereObject* /*borrow*/ SceneStore::getSphereObject(uint64_t id) const noexcept { return getTyped_<SphereObject>(id, sphereObjects_); }
const CubeObject* /*borrow*/ SceneStore::getCubeObject(uint64_t id) const noexcept { return getTyped_<CubeObject>(id, cubeObjects_); }
const CylinderObject* /*borrow*/ SceneStore::getCylinderObject(uint64_t id) const noexcept { return getTyped_<CylinderObject>(id, cylinderObjects_); }
const TorusObject* /*borrow*/ SceneStore::getTorusObject(uint64_t id) const noexcept { return getTyped_<TorusObject>(id, torusObjects_); }
const ConeObject* /*borrow*/ SceneStore::getConeObject(uint64_t id) const noexcept { return getTyped_<ConeObject>(id, coneObjects_); }
const ArrowObject* /*borrow*/ SceneStore::getArrowObject(uint64_t id) const noexcept { return getTyped_<ArrowObject>(id, arrowObjects_); }
const GridObject* /*borrow*/ SceneStore::getGridObject(uint64_t id) const noexcept { return getTyped_<GridObject>(id, gridObjects_); }
const AxesObject* /*borrow*/ SceneStore::getAxesObject(uint64_t id) const noexcept { return getTyped_<AxesObject>(id, axesObjects_); }
const PointCloudObject* /*borrow*/ SceneStore::getPointCloudObject(uint64_t id) const noexcept { return getTyped_<PointCloudObject>(id, pointCloudObjects_); }
const CapsuleObject* /*borrow*/ SceneStore::getCapsuleObject(uint64_t id) const noexcept { return getTyped_<CapsuleObject>(id, capsuleObjects_); }

const ISceneObject* /*borrow*/ SceneStore::getObject(uint64_t id) const noexcept {
    if (auto* p = getMeshObject(id)) return p;
    if (auto* p = getMeshSliceObject(id)) return p;
    if (auto* p = getVolumeObject(id)) return p;
    if (auto* p = getVolumeSliceObject(id)) return p;
    if (auto* p = getPlaneObject(id)) return p;
    if (auto* p = getContourObject(id)) return p;
    if (auto* p = getTeapotObject(id)) return p;
    if (auto* p = getSphereObject(id)) return p;
    if (auto* p = getCubeObject(id)) return p;
    if (auto* p = getCylinderObject(id)) return p;
    if (auto* p = getTorusObject(id)) return p;
    if (auto* p = getConeObject(id)) return p;
    if (auto* p = getArrowObject(id)) return p;
    if (auto* p = getGridObject(id)) return p;
    if (auto* p = getAxesObject(id)) return p;
    if (auto* p = getPointCloudObject(id)) return p;
    if (auto* p = getCapsuleObject(id)) return p;
    return nullptr;
}
ISceneObject* /*borrow*/ SceneStore::getObjectMut(uint64_t id) noexcept {
    if (auto* p = getMeshObjectMut(id)) return p;
    if (auto* p = getVolumeObjectMut(id)) return p;
    if (auto* p = getContourObjectMut(id)) return p;
    if (auto* p = getTeapotObjectMut(id)) return p;
    // For other types, search generically via typed mut helpers or fallback to base search
    auto findIn = [&](auto& partition) -> ISceneObject* {
        auto it = partition.find(id);
        return it == partition.end() ? nullptr : it->second.get();
    };
    if (auto* p = findIn(meshSliceObjects_)) return p;
    if (auto* p = findIn(volumeSliceObjects_)) return p;
    if (auto* p = findIn(planeObjects_)) return p;
    if (auto* p = findIn(sphereObjects_)) return p;
    if (auto* p = findIn(cubeObjects_)) return p;
    if (auto* p = findIn(cylinderObjects_)) return p;
    if (auto* p = findIn(torusObjects_)) return p;
    if (auto* p = findIn(coneObjects_)) return p;
    if (auto* p = findIn(arrowObjects_)) return p;
    if (auto* p = findIn(gridObjects_)) return p;
    if (auto* p = findIn(axesObjects_)) return p;
    if (auto* p = findIn(pointCloudObjects_)) return p;
    if (auto* p = findIn(capsuleObjects_)) return p;
    return nullptr;
}

std::vector<const ISceneObject*> /*borrow*/ SceneStore::objectsOfKind(SceneKind k) const noexcept {
    std::vector<const ISceneObject*> out;
    auto kit = kindIndex_.find(k);
    if (kit == kindIndex_.end()) return out;
    out.reserve(kit->second.size());
    for (uint64_t id : kit->second) {
        if (const ISceneObject* /*borrow*/ obj = getObject(id)) out.push_back(obj);
    }
    return out;
}
std::vector<ISceneObject*> /*borrow*/ SceneStore::objectsOfKindMut(SceneKind k) noexcept {
    std::vector<ISceneObject*> out;
    auto kit = kindIndex_.find(k);
    if (kit == kindIndex_.end()) return out;
    out.reserve(kit->second.size());
    for (uint64_t id : kit->second) {
        if (ISceneObject* /*borrow*/ obj = getObjectMut(id)) out.push_back(obj);
    }
    return out;
}

MeshObject* /*borrow*/ SceneStore::getMeshObjectMut(uint64_t id) noexcept { return getTypedMut_<MeshObject>(id, meshObjects_); }
VolumeObject* /*borrow*/ SceneStore::getVolumeObjectMut(uint64_t id) noexcept { return getTypedMut_<VolumeObject>(id, volumeObjects_); }
ContourObject* /*borrow*/ SceneStore::getContourObjectMut(uint64_t id) noexcept { return getTypedMut_<ContourObject>(id, contourObjects_); }
TeapotObject* /*borrow*/ SceneStore::getTeapotObjectMut(uint64_t id) noexcept { return getTypedMut_<TeapotObject>(id, teapotObjects_); }

bool SceneStore::removeMeshObject(uint64_t id) noexcept { return removeFromPartition_(id, meshObjects_); }
bool SceneStore::removeMeshSliceObject(uint64_t id) noexcept { return removeFromPartition_(id, meshSliceObjects_); }
bool SceneStore::removeVolumeObject(uint64_t id) noexcept { return removeFromPartition_(id, volumeObjects_); }
bool SceneStore::removeVolumeSliceObject(uint64_t id) noexcept { return removeFromPartition_(id, volumeSliceObjects_); }
bool SceneStore::removePlaneObject(uint64_t id) noexcept { return removeFromPartition_(id, planeObjects_); }
bool SceneStore::removeContourObject(uint64_t id) noexcept { return removeFromPartition_(id, contourObjects_); }
bool SceneStore::removeTeapotObject(uint64_t id) noexcept { return removeFromPartition_(id, teapotObjects_); }
bool SceneStore::removeSphereObject(uint64_t id) noexcept { return removeFromPartition_(id, sphereObjects_); }
bool SceneStore::removeCubeObject(uint64_t id) noexcept { return removeFromPartition_(id, cubeObjects_); }
bool SceneStore::removeCylinderObject(uint64_t id) noexcept { return removeFromPartition_(id, cylinderObjects_); }
bool SceneStore::removeTorusObject(uint64_t id) noexcept { return removeFromPartition_(id, torusObjects_); }
bool SceneStore::removeConeObject(uint64_t id) noexcept { return removeFromPartition_(id, coneObjects_); }
bool SceneStore::removeArrowObject(uint64_t id) noexcept { return removeFromPartition_(id, arrowObjects_); }
bool SceneStore::removeGridObject(uint64_t id) noexcept { return removeFromPartition_(id, gridObjects_); }
bool SceneStore::removeAxesObject(uint64_t id) noexcept { return removeFromPartition_(id, axesObjects_); }
bool SceneStore::removePointCloudObject(uint64_t id) noexcept { return removeFromPartition_(id, pointCloudObjects_); }
bool SceneStore::removeCapsuleObject(uint64_t id) noexcept { return removeFromPartition_(id, capsuleObjects_); }

bool SceneStore::removeObject(uint64_t id) noexcept {
    if (removeFromPartition_(id, meshObjects_)) return true;
    if (removeFromPartition_(id, meshSliceObjects_)) return true;
    if (removeFromPartition_(id, volumeObjects_)) return true;
    if (removeFromPartition_(id, volumeSliceObjects_)) return true;
    if (removeFromPartition_(id, planeObjects_)) return true;
    if (removeFromPartition_(id, contourObjects_)) return true;
    if (removeFromPartition_(id, teapotObjects_)) return true;
    if (removeFromPartition_(id, sphereObjects_)) return true;
    if (removeFromPartition_(id, cubeObjects_)) return true;
    if (removeFromPartition_(id, cylinderObjects_)) return true;
    if (removeFromPartition_(id, torusObjects_)) return true;
    if (removeFromPartition_(id, coneObjects_)) return true;
    if (removeFromPartition_(id, arrowObjects_)) return true;
    if (removeFromPartition_(id, gridObjects_)) return true;
    if (removeFromPartition_(id, axesObjects_)) return true;
    if (removeFromPartition_(id, pointCloudObjects_)) return true;
    if (removeFromPartition_(id, capsuleObjects_)) return true;
    return false;
}

size_t SceneStore::totalObjectCount() const noexcept {
    size_t n = 0;
    for (const auto& kv : kindIndex_) n += kv.second.size();
    return n;
}
size_t SceneStore::countOfKind(SceneKind k) const noexcept {
    auto it = kindIndex_.find(k);
    return it == kindIndex_.end() ? 0u : it->second.size();
}

void SceneStore::recordDirty_(FieldId field) noexcept {
    for (auto& entry : dirtyLog_) {
        if (entry.second == field) {
            entry.first = storeGen_;
            return;
        }
    }
    dirtyLog_.emplace_back(storeGen_, field);
}

void SceneStore::bump(FieldId field) noexcept {
    ++storeGen_;
    recordDirty_(field);
}

void SceneStore::markDirty(uint64_t /*id*/, FieldId field) noexcept {
    ++storeGen_;
    recordDirty_(field);
}

std::vector<FieldId> SceneStore::dirtyFieldsSince(uint64_t lastGen) const noexcept {
    std::vector<FieldId> out;
    for (const auto& entry : dirtyLog_) {
        if (entry.first > lastGen) out.push_back(entry.second);
    }
    return out;
}

data::Result<void> SceneStore::resolve(uint64_t id) const noexcept {
    if (getObject(id) != nullptr) {
        return data::Result<void>(data::value);
    }
    if (tombstoneGen_.count(id) != 0u) {
        return data::makeError<void>(
            2, "SceneStore::resolve: stale object handle — id " +
                   std::to_string(id) + " was erased (tombstone present)");
    }
    return data::makeError<void>(
        1, "SceneStore::resolve: unknown object id " + std::to_string(id));
}

data::Result<AssetId> SceneStore::registerMeshAsset(
    AssetRegistry<data::Mesh>::SharedAsset mesh) {
    return meshAssets_.registerAsset(std::move(mesh));
}
data::Result<AssetRegistry<data::Mesh>::SharedAsset>
SceneStore::resolveMeshAsset(AssetId id) const {
    return meshAssets_.resolve(id);
}
data::Result<void> SceneStore::unregisterMeshAsset(AssetId id) {
    return meshAssets_.unregister(id);
}

// ---------------------------------------------------------------------------
// ViewStore
// ---------------------------------------------------------------------------

uint64_t ViewStore::addView(View view) {
    uint64_t id = allocId();
    view.id = id;
    view.generation = storeGen_ + 1;
    view.rectGen = view.generation;
    view.planeGen = view.generation;
    view.cameraGen = view.generation;
    view.itemsGen = view.generation;
    views_.emplace(id, std::move(view));
    ++storeGen_;
    recordDirty_(FieldId::Rect);
    recordDirty_(FieldId::Plane);
    recordDirty_(FieldId::CameraView);
    recordDirty_(FieldId::Items);
    return id;
}
const View* /*borrow*/ ViewStore::getView(uint64_t id) const noexcept {
    auto it = views_.find(id);
    return it == views_.end() ? nullptr : &it->second;
}
View* /*borrow*/ ViewStore::getViewMut(uint64_t id) noexcept {
    auto it = views_.find(id);
    return it == views_.end() ? nullptr : &it->second;
}
bool ViewStore::removeView(uint64_t id) noexcept {
    auto it = views_.find(id);
    if (it == views_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    views_.erase(it);
    ++storeGen_;
    recordDirty_(FieldId::Items);
    return true;
}

void ViewStore::recordDirty_(FieldId field) noexcept {
    for (auto& entry : dirtyLog_) {
        if (entry.second == field) {
            entry.first = storeGen_;
            return;
        }
    }
    dirtyLog_.emplace_back(storeGen_, field);
}

void ViewStore::bump(FieldId field) noexcept {
    ++storeGen_;
    recordDirty_(field);
}

void ViewStore::markDirty(uint64_t /*id*/, FieldId field) noexcept {
    ++storeGen_;
    recordDirty_(field);
}

std::vector<FieldId> ViewStore::dirtyFieldsSince(uint64_t lastGen) const noexcept {
    std::vector<FieldId> out;
    for (const auto& entry : dirtyLog_) {
        if (entry.first > lastGen) out.push_back(entry.second);
    }
    return out;
}

data::Result<void> ViewStore::resolve(uint64_t id) const noexcept {
    if (views_.count(id) != 0u) return data::Result<void>(data::value);
    if (tombstoneGen_.count(id) != 0u) {
        return data::makeError<void>(
            2, "ViewStore::resolve: stale view handle — id " +
                   std::to_string(id) + " was erased (tombstone present)");
    }
    return data::makeError<void>(
        1, "ViewStore::resolve: unknown view id " + std::to_string(id));
}

} // namespace re::scene
