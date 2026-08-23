#include "scene/store.hpp"

namespace re::scene {

// ---------------------------------------------------------------------------
// SceneStore
// ---------------------------------------------------------------------------

uint64_t SceneStore::addMeshObject(MeshObject obj) {
    uint64_t id = allocId();
    obj.id = id;
    obj.generation = storeGen_ + 1;
    meshObjects_.emplace(id, std::move(obj));
    ++storeGen_;
    return id;
}
uint64_t SceneStore::addMeshSliceObject(MeshSliceObject obj) {
    uint64_t id = allocId();
    obj.id = id;
    obj.generation = storeGen_ + 1;
    meshSliceObjects_.emplace(id, std::move(obj));
    ++storeGen_;
    return id;
}
uint64_t SceneStore::addVolumeObject(VolumeObject obj) {
    uint64_t id = allocId();
    obj.id = id;
    obj.generation = storeGen_ + 1;
    volumeObjects_.emplace(id, std::move(obj));
    ++storeGen_;
    return id;
}
uint64_t SceneStore::addVolumeSliceObject(VolumeSliceObject obj) {
    uint64_t id = allocId();
    obj.id = id;
    obj.generation = storeGen_ + 1;
    volumeSliceObjects_.emplace(id, std::move(obj));
    ++storeGen_;
    return id;
}
uint64_t SceneStore::addPlaneObject(PlaneObject obj) {
    uint64_t id = allocId();
    obj.id = id;
    obj.generation = storeGen_ + 1;
    planeObjects_.emplace(id, std::move(obj));
    ++storeGen_;
    return id;
}

const MeshObject* SceneStore::getMeshObject(uint64_t id) const noexcept {
    auto it = meshObjects_.find(id);
    return it == meshObjects_.end() ? nullptr : &it->second;
}
const MeshSliceObject* SceneStore::getMeshSliceObject(uint64_t id) const noexcept {
    auto it = meshSliceObjects_.find(id);
    return it == meshSliceObjects_.end() ? nullptr : &it->second;
}
const VolumeObject* SceneStore::getVolumeObject(uint64_t id) const noexcept {
    auto it = volumeObjects_.find(id);
    return it == volumeObjects_.end() ? nullptr : &it->second;
}
const VolumeSliceObject* SceneStore::getVolumeSliceObject(uint64_t id) const noexcept {
    auto it = volumeSliceObjects_.find(id);
    return it == volumeSliceObjects_.end() ? nullptr : &it->second;
}
const PlaneObject* SceneStore::getPlaneObject(uint64_t id) const noexcept {
    auto it = planeObjects_.find(id);
    return it == planeObjects_.end() ? nullptr : &it->second;
}
MeshObject* SceneStore::getMeshObjectMut(uint64_t id) noexcept {
    auto it = meshObjects_.find(id);
    return it == meshObjects_.end() ? nullptr : &it->second;
}
VolumeObject* SceneStore::getVolumeObjectMut(uint64_t id) noexcept {
    auto it = volumeObjects_.find(id);
    return it == volumeObjects_.end() ? nullptr : &it->second;
}

bool SceneStore::removeMeshObject(uint64_t id) noexcept {
    auto it = meshObjects_.find(id);
    if (it == meshObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    meshObjects_.erase(it);
    ++storeGen_;
    return true;
}
bool SceneStore::removeMeshSliceObject(uint64_t id) noexcept {
    auto it = meshSliceObjects_.find(id);
    if (it == meshSliceObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    meshSliceObjects_.erase(it);
    ++storeGen_;
    return true;
}
bool SceneStore::removeVolumeObject(uint64_t id) noexcept {
    auto it = volumeObjects_.find(id);
    if (it == volumeObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    volumeObjects_.erase(it);
    ++storeGen_;
    return true;
}
bool SceneStore::removeVolumeSliceObject(uint64_t id) noexcept {
    auto it = volumeSliceObjects_.find(id);
    if (it == volumeSliceObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    volumeSliceObjects_.erase(it);
    ++storeGen_;
    return true;
}
bool SceneStore::removePlaneObject(uint64_t id) noexcept {
    auto it = planeObjects_.find(id);
    if (it == planeObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    planeObjects_.erase(it);
    ++storeGen_;
    return true;
}

std::vector<FieldId> SceneStore::dirtyFieldsSince(uint64_t lastGen) const noexcept {
    if (storeGen_ == lastGen) return {};
    return {FieldId::Transform, FieldId::Material, FieldId::TransferFunction, FieldId::Items};
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
    return id;
}
const View* ViewStore::getView(uint64_t id) const noexcept {
    auto it = views_.find(id);
    return it == views_.end() ? nullptr : &it->second;
}
View* ViewStore::getViewMut(uint64_t id) noexcept {
    auto it = views_.find(id);
    return it == views_.end() ? nullptr : &it->second;
}
bool ViewStore::removeView(uint64_t id) noexcept {
    auto it = views_.find(id);
    if (it == views_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    views_.erase(it);
    ++storeGen_;
    return true;
}
std::vector<FieldId> ViewStore::dirtyFieldsSince(uint64_t lastGen) const noexcept {
    if (storeGen_ == lastGen) return {};
    return {FieldId::Rect, FieldId::Plane, FieldId::CameraView, FieldId::Items};
}

} // namespace re::scene
