#include "scene/store.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace re::scene {

// ---------------------------------------------------------------------------
// SceneStore — add (single-map T6)
// ---------------------------------------------------------------------------

uint64_t SceneStore::addMeshObject(MeshObject obj) { return addObject<MeshObject>(std::move(obj)); }
uint64_t SceneStore::addMeshSliceObject(MeshSliceObject obj) { return addObject<MeshSliceObject>(std::move(obj)); }
uint64_t SceneStore::addVolumeObject(VolumeObject obj) { return addObject<VolumeObject>(std::move(obj)); }
uint64_t SceneStore::addVolumeSliceObject(VolumeSliceObject obj) { return addObject<VolumeSliceObject>(std::move(obj)); }
uint64_t SceneStore::addPlaneObject(PlaneObject obj) { return addObject<PlaneObject>(std::move(obj)); }
uint64_t SceneStore::addContourObject(ContourObject obj) { return addObject<ContourObject>(std::move(obj)); }

uint64_t SceneStore::addObject(std::unique_ptr<ISceneObject> obj) {
    if (!obj) return 0u;
    SceneKind kind = obj->kind();
    uint64_t id = allocId();
    obj->setId(id);
    obj->setGeneration(tracker_.storeGeneration() + 1);
    objects_.emplace(id, std::move(obj));
    kindIndex_[kind].insert(id);
    tracker_.incStoreGen();
    tracker_.recordDirty(FieldId::Transform);
    tracker_.recordDirty(FieldId::Items);
    return id;
}

// ---------------------------------------------------------------------------
// SceneStore — getters (borrow) single-map
// ---------------------------------------------------------------------------

const MeshObject* /*borrow*/ SceneStore::getMeshObject(uint64_t id) const noexcept { return get<MeshObject>(id); }
const MeshSliceObject* /*borrow*/ SceneStore::getMeshSliceObject(uint64_t id) const noexcept { return get<MeshSliceObject>(id); }
const VolumeObject* /*borrow*/ SceneStore::getVolumeObject(uint64_t id) const noexcept { return get<VolumeObject>(id); }
const VolumeSliceObject* /*borrow*/ SceneStore::getVolumeSliceObject(uint64_t id) const noexcept { return get<VolumeSliceObject>(id); }
const PlaneObject* /*borrow*/ SceneStore::getPlaneObject(uint64_t id) const noexcept { return get<PlaneObject>(id); }
const ContourObject* /*borrow*/ SceneStore::getContourObject(uint64_t id) const noexcept { return get<ContourObject>(id); }

const ISceneObject* /*borrow*/ SceneStore::getObject(uint64_t id) const noexcept {
    auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : it->second.get();
}
ISceneObject* /*borrow*/ SceneStore::getObjectMut(uint64_t id) noexcept {
    auto it = objects_.find(id);
    return it == objects_.end() ? nullptr : it->second.get();
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

MeshObject* /*borrow*/ SceneStore::getMeshObjectMut(uint64_t id) noexcept { return getMut<MeshObject>(id); }
VolumeObject* /*borrow*/ SceneStore::getVolumeObjectMut(uint64_t id) noexcept { return getMut<VolumeObject>(id); }
ContourObject* /*borrow*/ SceneStore::getContourObjectMut(uint64_t id) noexcept { return getMut<ContourObject>(id); }

bool SceneStore::removeMeshObject(uint64_t id) noexcept {
    if (!get<MeshObject>(id)) return false;
    return removeObject(id);
}
bool SceneStore::removeMeshSliceObject(uint64_t id) noexcept {
    if (!get<MeshSliceObject>(id)) return false;
    return removeObject(id);
}
bool SceneStore::removeVolumeObject(uint64_t id) noexcept {
    if (!get<VolumeObject>(id)) return false;
    return removeObject(id);
}
bool SceneStore::removeVolumeSliceObject(uint64_t id) noexcept {
    if (!get<VolumeSliceObject>(id)) return false;
    return removeObject(id);
}
bool SceneStore::removePlaneObject(uint64_t id) noexcept {
    if (!get<PlaneObject>(id)) return false;
    return removeObject(id);
}
bool SceneStore::removeContourObject(uint64_t id) noexcept {
    if (!get<ContourObject>(id)) return false;
    return removeObject(id);
}

bool SceneStore::removeObject(uint64_t id) noexcept {
    auto it = objects_.find(id);
    if (it == objects_.end()) return false;
    uint64_t gen = it->second->generation();
    SceneKind kind = it->second->kind();
    tracker_.noteTombstone(id, gen + 1);
    auto kit = kindIndex_.find(kind);
    if (kit != kindIndex_.end()) {
        kit->second.erase(id);
        if (kit->second.empty()) kindIndex_.erase(kit);
    }
    objects_.erase(it);
    tracker_.incStoreGen();
    tracker_.recordDirty(FieldId::Items);
    return true;
}

size_t SceneStore::totalObjectCount() const noexcept {
    return objects_.size();
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
