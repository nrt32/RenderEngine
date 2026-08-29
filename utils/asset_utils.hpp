#pragma once

// utils/asset_utils.hpp — T1 Store IO depollution: filesystem loaders live in utils/ (SPEC §3.1, T1).
//
// SceneStore stays pure value lib `data+volume+glm` per docs/spec/modules.md:21 (no `io/` dep, no `nlohmann/json` in header — JSON stays in store.cpp only for serialize()). The 4-step `load→shared_ptr→registerMeshAsset→addMeshObject` ceremony that was previously `SceneStore::loadMesh`/`loadVolume` (atomic sugar, V5 T7) now lives in `utils/` — `utils/` owns filesystem IO (header-only, IO-only). Two header-only entry points are provided:
//
//   re::utils::loadMeshAsset(path)   → Result<SharedMesh>   (wraps io::loadObjMesh + shared_ptr)
//   re::utils::loadVolumeAsset(path) → Result<SharedVolume> (wraps io::loadNrrdVolume + shared_ptr)
//
// Callers that need a SceneStore object do the 4 steps via utils ceremony: `auto shared = utils::loadMeshAsset(path); if(shared.ok()) { auto aid = store.registerMeshAsset(*shared); auto oid = store.addMeshObject(Objects::mesh(*aid_or_shared)); }` where `Objects::mesh` is the value builder in scene/builders.hpp (stays value builder, not IO). This keeps SceneStore header lean (no io/mesh headers, no re_io linkage) and keeps the filesystem dependency in utils/ only. T1.

#include <memory>
#include <string>

#include "data/mesh.hpp"
#include "data/result.hpp"
#include "data/volume_dataset.hpp"
#include "io/mesh/obj_mesh_loader.hpp"
#include "io/volume/nrrd_volume_loader.hpp"

namespace re::utils {

/// Shared-asset aliases matching `scene::AssetRegistry<...>::SharedAsset` (shared_ptr<const T>, content-hash dedup via SceneStore registry).
using SharedMesh = std::shared_ptr<const data::Mesh>;
using SharedVolume = std::shared_ptr<const data::VolumeDataset>;

/// Load a mesh asset from `path` via `io::loadObjMesh` and wrap in a shared_ptr (IO-only, header-only).
///
/// Returns typed `data::Result<SharedMesh>` with `ErrorDomain::MeshIo` on failure (same domain/code as `io::loadObjMesh`), no partial. The caller then does `store.registerMeshAsset(shared)` + `store.addMeshObject(Objects::mesh(shared))` — the 4-step ceremony lives in `utils/` per T1, not in `scene/store.hpp` (keeps `SceneStore` pure value). Header-only so no extra cc file; `utils/` owns filesystem per T1.
inline data::Result<SharedMesh> loadMeshAsset(const std::string& path) {
    auto meshRes = io::loadObjMesh(path);
    if (meshRes.failed()) {
        return data::Result<SharedMesh>(data::error, meshRes.error());
    }
    auto shared = std::make_shared<const data::Mesh>(std::move(*meshRes));
    return data::Result<SharedMesh>(data::value, std::move(shared));
}

/// Load a volume asset from `path` via `io::loadNrrdVolume` and wrap in a shared_ptr (IO-only, header-only).
///
/// Returns typed `data::Result<SharedVolume>` with `ErrorDomain::VolumeIo` on failure (same domain/code as `io::loadNrrdVolume`), no partial. Caller then does `store.registerVolumeAsset(shared)` + `store.addVolumeObject(...)` — the 4-step ceremony lives in `utils/` per T1.
inline data::Result<SharedVolume> loadVolumeAsset(const std::string& path) {
    auto volRes = io::loadNrrdVolume(path);
    if (volRes.failed()) {
        return data::Result<SharedVolume>(data::error, volRes.error());
    }
    auto shared = std::make_shared<const data::VolumeDataset>(std::move(*volRes));
    return data::Result<SharedVolume>(data::value, std::move(shared));
}

} // namespace re::utils
