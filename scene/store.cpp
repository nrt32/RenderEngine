#include "scene/store.hpp"

#include <string>
#include <unordered_map>

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
    // Adding an object dirties BOTH the item inventory (views re-translate
    // their item lists) and the new object's transform state (never yet
    // translated) — recorded through the bounded one-slot-per-field log so
    // `dirtyFieldsSince` reports the genuine per-field change set.
    recordDirty_(FieldId::Transform);
    recordDirty_(FieldId::Items);
    return id;
}
uint64_t SceneStore::addMeshSliceObject(MeshSliceObject obj) {
    uint64_t id = allocId();
    obj.id = id;
    obj.generation = storeGen_ + 1;
    meshSliceObjects_.emplace(id, std::move(obj));
    ++storeGen_;
    // Adding an object dirties BOTH the item inventory (views re-translate
    // their item lists) and the new object's transform state (never yet
    // translated) — recorded through the bounded one-slot-per-field log so
    // `dirtyFieldsSince` reports the genuine per-field change set.
    recordDirty_(FieldId::Transform);
    recordDirty_(FieldId::Items);
    return id;
}
uint64_t SceneStore::addVolumeObject(VolumeObject obj) {
    uint64_t id = allocId();
    obj.id = id;
    obj.generation = storeGen_ + 1;
    volumeObjects_.emplace(id, std::move(obj));
    ++storeGen_;
    // Adding an object dirties BOTH the item inventory (views re-translate
    // their item lists) and the new object's transform state (never yet
    // translated) — recorded through the bounded one-slot-per-field log so
    // `dirtyFieldsSince` reports the genuine per-field change set.
    recordDirty_(FieldId::Transform);
    recordDirty_(FieldId::Items);
    return id;
}
uint64_t SceneStore::addVolumeSliceObject(VolumeSliceObject obj) {
    uint64_t id = allocId();
    obj.id = id;
    obj.generation = storeGen_ + 1;
    volumeSliceObjects_.emplace(id, std::move(obj));
    ++storeGen_;
    // Adding an object dirties BOTH the item inventory (views re-translate
    // their item lists) and the new object's transform state (never yet
    // translated) — recorded through the bounded one-slot-per-field log so
    // `dirtyFieldsSince` reports the genuine per-field change set.
    recordDirty_(FieldId::Transform);
    recordDirty_(FieldId::Items);
    return id;
}
uint64_t SceneStore::addPlaneObject(PlaneObject obj) {
    uint64_t id = allocId();
    obj.id = id;
    obj.generation = storeGen_ + 1;
    planeObjects_.emplace(id, std::move(obj));
    ++storeGen_;
    // Adding an object dirties BOTH the item inventory (views re-translate
    // their item lists) and the new object's transform state (never yet
    // translated) — recorded through the bounded one-slot-per-field log so
    // `dirtyFieldsSince` reports the genuine per-field change set.
    recordDirty_(FieldId::Transform);
    recordDirty_(FieldId::Items);
    return id;
}
uint64_t SceneStore::addContourObject(ContourObject obj) {
    uint64_t id = allocId();
    obj.id = id;
    obj.generation = storeGen_ + 1;
    contourObjects_.emplace(id, std::move(obj));
    ++storeGen_;
    // Adding an object dirties BOTH the item inventory (views re-translate
    // their item lists) and the new object's transform state (never yet
    // translated) — recorded through the bounded one-slot-per-field log so
    // `dirtyFieldsSince` reports the genuine per-field change set.
    recordDirty_(FieldId::Transform);
    recordDirty_(FieldId::Items);
    return id;
}

const MeshObject* /*borrow*/ SceneStore::getMeshObject(uint64_t id) const noexcept {
    auto it = meshObjects_.find(id);
    return it == meshObjects_.end() ? nullptr : &it->second;
}
const MeshSliceObject* /*borrow*/ SceneStore::getMeshSliceObject(uint64_t id) const noexcept {
    auto it = meshSliceObjects_.find(id);
    return it == meshSliceObjects_.end() ? nullptr : &it->second;
}
const VolumeObject* /*borrow*/ SceneStore::getVolumeObject(uint64_t id) const noexcept {
    auto it = volumeObjects_.find(id);
    return it == volumeObjects_.end() ? nullptr : &it->second;
}
const VolumeSliceObject* /*borrow*/ SceneStore::getVolumeSliceObject(uint64_t id) const noexcept {
    auto it = volumeSliceObjects_.find(id);
    return it == volumeSliceObjects_.end() ? nullptr : &it->second;
}
const PlaneObject* /*borrow*/ SceneStore::getPlaneObject(uint64_t id) const noexcept {
    auto it = planeObjects_.find(id);
    return it == planeObjects_.end() ? nullptr : &it->second;
}
const ContourObject* /*borrow*/ SceneStore::getContourObject(uint64_t id) const noexcept {
    auto it = contourObjects_.find(id);
    return it == contourObjects_.end() ? nullptr : &it->second;
}
MeshObject* /*borrow*/ SceneStore::getMeshObjectMut(uint64_t id) noexcept {
    auto it = meshObjects_.find(id);
    return it == meshObjects_.end() ? nullptr : &it->second;
}
VolumeObject* /*borrow*/ SceneStore::getVolumeObjectMut(uint64_t id) noexcept {
    auto it = volumeObjects_.find(id);
    return it == volumeObjects_.end() ? nullptr : &it->second;
}
ContourObject* /*borrow*/ SceneStore::getContourObjectMut(uint64_t id) noexcept {
    auto it = contourObjects_.find(id);
    return it == contourObjects_.end() ? nullptr : &it->second;
}

bool SceneStore::removeMeshObject(uint64_t id) noexcept {
    auto it = meshObjects_.find(id);
    if (it == meshObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    meshObjects_.erase(it);
    ++storeGen_;
    // Erasure dirties the item inventory of every view holding this id; the
    // id's tombstone generation is retained so resolve(id) can report a
    // typed stale error instead of an indistinguishable "unknown".
    recordDirty_(FieldId::Items);
    return true;
}
bool SceneStore::removeMeshSliceObject(uint64_t id) noexcept {
    auto it = meshSliceObjects_.find(id);
    if (it == meshSliceObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    meshSliceObjects_.erase(it);
    ++storeGen_;
    // Erasure dirties the item inventory of every view holding this id; the
    // id's tombstone generation is retained so resolve(id) can report a
    // typed stale error instead of an indistinguishable "unknown".
    recordDirty_(FieldId::Items);
    return true;
}
bool SceneStore::removeVolumeObject(uint64_t id) noexcept {
    auto it = volumeObjects_.find(id);
    if (it == volumeObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    volumeObjects_.erase(it);
    ++storeGen_;
    // Erasure dirties the item inventory of every view holding this id; the
    // id's tombstone generation is retained so resolve(id) can report a
    // typed stale error instead of an indistinguishable "unknown".
    recordDirty_(FieldId::Items);
    return true;
}
bool SceneStore::removeVolumeSliceObject(uint64_t id) noexcept {
    auto it = volumeSliceObjects_.find(id);
    if (it == volumeSliceObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    volumeSliceObjects_.erase(it);
    ++storeGen_;
    // Erasure dirties the item inventory of every view holding this id; the
    // id's tombstone generation is retained so resolve(id) can report a
    // typed stale error instead of an indistinguishable "unknown".
    recordDirty_(FieldId::Items);
    return true;
}
bool SceneStore::removePlaneObject(uint64_t id) noexcept {
    auto it = planeObjects_.find(id);
    if (it == planeObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    planeObjects_.erase(it);
    ++storeGen_;
    // Erasure dirties the item inventory of every view holding this id; the
    // id's tombstone generation is retained so resolve(id) can report a
    // typed stale error instead of an indistinguishable "unknown".
    recordDirty_(FieldId::Items);
    return true;
}
bool SceneStore::removeContourObject(uint64_t id) noexcept {
    auto it = contourObjects_.find(id);
    if (it == contourObjects_.end()) return false;
    tombstoneGen_[id] = it->second.generation + 1;
    contourObjects_.erase(it);
    ++storeGen_;
    // Erasure dirties the item inventory of every view holding this id; the
    // id's tombstone generation is retained so resolve(id) can report a
    // typed stale error instead of an indistinguishable "unknown".
    recordDirty_(FieldId::Items);
    return true;
}

void SceneStore::recordDirty_(FieldId field) noexcept {
    // Bounded drain: at most one slot per FieldId — a re-mutation RAISES the
    // recorded generation in place instead of appending, so the log's size is
    // capped by the FieldId count no matter how many frames mutate the store.
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
    // The log holds one slot per ever-dirtied field with its LATEST mutation
    // generation, so scanning it yields exactly the distinct set of fields
    // genuinely mutated after `lastGen` — first-mutation order, no hardcoded
    // superset. Cost is O(#FieldIds) per call (bounded drain structure).
    for (const auto& entry : dirtyLog_) {
        if (entry.first > lastGen) out.push_back(entry.second);
    }
    return out;
}

data::Result<void> SceneStore::resolve(uint64_t id) const noexcept {
    if (meshObjects_.count(id) != 0u || meshSliceObjects_.count(id) != 0u ||
        volumeObjects_.count(id) != 0u || volumeSliceObjects_.count(id) != 0u ||
        planeObjects_.count(id) != 0u || contourObjects_.count(id) != 0u) {
        return data::Result<void>(data::value);
    }
    if (tombstoneGen_.count(id) != 0u) {
        // The tombstone generation written on erase is finally READ here: a
        // handle minted before an erase resolves to a typed stale error
        // (code 2), never to an "unknown id" guess and never to a crash.
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
    // A new view starts with all four render-relevant fields genuinely
    // untranslated, so each enters the bounded log exactly once.
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
    // Erasure dirties the item inventory of every view holding this id; the
    // id's tombstone generation is retained so resolve(id) can report a
    // typed stale error instead of an indistinguishable "unknown".
    recordDirty_(FieldId::Items);
    return true;
}

void ViewStore::recordDirty_(FieldId field) noexcept {
    // Bounded drain (same one-slot-per-field structure as SceneStore): the
    // log size is capped by the FieldId count regardless of frame count.
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
    // Computed, never hardcoded: exactly the distinct fields whose latest
    // mutation generation is greater than `lastGen` (e.g. a lone camera bump
    // reports {CameraView}, not a fixed four-field list).
    for (const auto& entry : dirtyLog_) {
        if (entry.first > lastGen) out.push_back(entry.second);
    }
    return out;
}

data::Result<void> ViewStore::resolve(uint64_t id) const noexcept {
    if (views_.count(id) != 0u) return data::Result<void>(data::value);
    if (tombstoneGen_.count(id) != 0u) {
        // Tombstone generations are retained on erase precisely so a stale
        // view handle resolves to this typed error instead of "unknown".
        return data::makeError<void>(
            2, "ViewStore::resolve: stale view handle — id " +
                   std::to_string(id) + " was erased (tombstone present)");
    }
    return data::makeError<void>(
        1, "ViewStore::resolve: unknown view id " + std::to_string(id));
}

} // namespace re::scene
