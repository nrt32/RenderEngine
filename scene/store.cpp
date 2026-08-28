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
    obj.setGeneration(tracker_.storeGeneration() + 1);
    SceneKind kind = Derived::Kind;
    auto ptr = std::make_unique<Derived>(std::move(obj));
    partition.emplace(id, std::move(ptr));
    kindIndex_[kind].insert(id);
    tracker_.incStoreGen();
    tracker_.recordDirty(FieldId::Transform);
    tracker_.recordDirty(FieldId::Items);
    return id;
}

bool SceneStore::removeFromPartition_(uint64_t id, std::unordered_map<uint64_t, std::unique_ptr<ISceneObject>>& partition) noexcept {
    auto it = partition.find(id);
    if (it == partition.end()) return false;
    uint64_t gen = it->second->generation();
    SceneKind kind = it->second->kind();
    tracker_.noteTombstone(id, gen + 1);
    auto kit = kindIndex_.find(kind);
    if (kit != kindIndex_.end()) {
        kit->second.erase(id);
        if (kit->second.empty()) kindIndex_.erase(kit);
    }
    partition.erase(it);
    tracker_.incStoreGen();
    tracker_.recordDirty(FieldId::Items);
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

uint64_t SceneStore::addObject(std::unique_ptr<ISceneObject> obj) {
    if (!obj) return 0u;
    SceneKind kind = obj->kind();
    uint64_t id = allocId();
    obj->setId(id);
    obj->setGeneration(tracker_.storeGeneration() + 1);
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
        default: meshObjects_.emplace(id, std::move(obj)); break;
    }
    kindIndex_[kind].insert(id);
    tracker_.incStoreGen();
    tracker_.recordDirty(FieldId::Transform);
    tracker_.recordDirty(FieldId::Items);
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

const ISceneObject* /*borrow*/ SceneStore::getObject(uint64_t id) const noexcept {
    if (auto* p = getMeshObject(id)) return p;
    if (auto* p = getMeshSliceObject(id)) return p;
    if (auto* p = getVolumeObject(id)) return p;
    if (auto* p = getVolumeSliceObject(id)) return p;
    if (auto* p = getPlaneObject(id)) return p;
    if (auto* p = getContourObject(id)) return p;
    return nullptr;
}
ISceneObject* /*borrow*/ SceneStore::getObjectMut(uint64_t id) noexcept {
    if (auto* p = getMeshObjectMut(id)) return p;
    if (auto* p = getVolumeObjectMut(id)) return p;
    if (auto* p = getContourObjectMut(id)) return p;
    auto findIn = [&](auto& partition) -> ISceneObject* {
        auto it = partition.find(id);
        return it == partition.end() ? nullptr : it->second.get();
    };
    if (auto* p = findIn(meshSliceObjects_)) return p;
    if (auto* p = findIn(volumeSliceObjects_)) return p;
    if (auto* p = findIn(planeObjects_)) return p;
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

bool SceneStore::removeMeshObject(uint64_t id) noexcept { return removeFromPartition_(id, meshObjects_); }
bool SceneStore::removeMeshSliceObject(uint64_t id) noexcept { return removeFromPartition_(id, meshSliceObjects_); }
bool SceneStore::removeVolumeObject(uint64_t id) noexcept { return removeFromPartition_(id, volumeObjects_); }
bool SceneStore::removeVolumeSliceObject(uint64_t id) noexcept { return removeFromPartition_(id, volumeSliceObjects_); }
bool SceneStore::removePlaneObject(uint64_t id) noexcept { return removeFromPartition_(id, planeObjects_); }
bool SceneStore::removeContourObject(uint64_t id) noexcept { return removeFromPartition_(id, contourObjects_); }

bool SceneStore::removeObject(uint64_t id) noexcept {
    if (removeFromPartition_(id, meshObjects_)) return true;
    if (removeFromPartition_(id, meshSliceObjects_)) return true;
    if (removeFromPartition_(id, volumeObjects_)) return true;
    if (removeFromPartition_(id, volumeSliceObjects_)) return true;
    if (removeFromPartition_(id, planeObjects_)) return true;
    if (removeFromPartition_(id, contourObjects_)) return true;
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
    tracker_.recordDirty(field);
}

void SceneStore::bump(FieldId field) noexcept {
    tracker_.bump(field);
}

void SceneStore::markDirty(uint64_t id, FieldId field) noexcept {
    (void)id;
    tracker_.markDirty(id, field);
}

std::vector<FieldId> SceneStore::dirtyFieldsSince(uint64_t lastGen) const noexcept {
    return tracker_.dirtyFieldsSince(lastGen);
}

data::Result<void> SceneStore::resolve(uint64_t id) const noexcept {
    if (getObject(id) != nullptr) {
        return data::Result<void>(data::value);
    }
    if (tracker_.hasTombstone(id)) {
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
data::Result<AssetId> SceneStore::registerVolumeAsset(
    AssetRegistry<data::VolumeDataset>::SharedAsset vol) {
    return volumeAssets_.registerAsset(std::move(vol));
}
data::Result<AssetRegistry<data::VolumeDataset>::SharedAsset>
SceneStore::resolveVolumeAsset(AssetId id) const {
    return volumeAssets_.resolve(id);
}
data::Result<void> SceneStore::unregisterVolumeAsset(AssetId id) {
    return volumeAssets_.unregister(id);
}
data::Result<AssetId> SceneStore::registerImageAsset(
    AssetRegistry<data::Image>::SharedAsset img) {
    return imageAssets_.registerAsset(std::move(img));
}
data::Result<AssetRegistry<data::Image>::SharedAsset>
SceneStore::resolveImageAsset(AssetId id) const {
    return imageAssets_.resolve(id);
}
data::Result<void> SceneStore::unregisterImageAsset(AssetId id) {
    return imageAssets_.unregister(id);
}

// ---------------------------------------------------------------------------
// ViewStore
// ---------------------------------------------------------------------------

uint64_t ViewStore::addView(View view) {
    uint64_t id = allocId();
    view.id = id;
    view.generation = tracker_.storeGeneration() + 1;
    view.rectGen = view.generation;
    view.planeGen = view.generation;
    view.cameraGen = view.generation;
    view.itemsGen = view.generation;
    view.clearColorGen = view.generation;
    view.depthTestGen = view.generation;
    views_.emplace(id, std::move(view));
    tracker_.incStoreGen();
    tracker_.recordDirty(FieldId::Rect);
    tracker_.recordDirty(FieldId::Plane);
    tracker_.recordDirty(FieldId::CameraView);
    tracker_.recordDirty(FieldId::Items);
    tracker_.recordDirty(FieldId::ClearColor);
    tracker_.recordDirty(FieldId::DepthTest);
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
    tracker_.noteTombstone(id, it->second.generation + 1);
    views_.erase(it);
    tracker_.incStoreGen();
    tracker_.recordDirty(FieldId::Items);
    return true;
}

void ViewStore::recordDirty_(FieldId field) noexcept {
    tracker_.recordDirty(field);
}

void ViewStore::bump(FieldId field) noexcept {
    tracker_.bump(field);
}

void ViewStore::markDirty(uint64_t id, FieldId field) noexcept {
    (void)id;
    tracker_.markDirty(id, field);
}

std::vector<FieldId> ViewStore::dirtyFieldsSince(uint64_t lastGen) const noexcept {
    return tracker_.dirtyFieldsSince(lastGen);
}

data::Result<void> ViewStore::resolve(uint64_t id) const noexcept {
    if (views_.count(id) != 0u) return data::Result<void>(data::value);
    if (tracker_.hasTombstone(id)) {
        return data::makeError<void>(
            2, "ViewStore::resolve: stale view handle — id " +
                   std::to_string(id) + " was erased (tombstone present)");
    }
    return data::makeError<void>(
        1, "ViewStore::resolve: unknown view id " + std::to_string(id));
}

} // namespace re::scene
