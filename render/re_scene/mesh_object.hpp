#pragma once

// render/re_scene/mesh_object.hpp — ReMeshObject reference (SPEC §12.4 V3.8, T9).
//
// RE-minimal type for the mesh path: keeps only Re-direct values
// (AssetHandle/model/worldBounds/ReMaterial*), never verbatim scene desc.
// Binding inventory is docs/re_scene_inventory.md (6 tables/23 fields).
// This header is the sole render/re_scene/*.hpp landed this iteration;
// ReVolume/RePlane/ReView inventory is doc-only (deferred with T8).

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <memory>

#include "render/asset_registry.hpp"
#include "render/imaterial.hpp"

namespace re::render::re_scene {

/// Axis-aligned bounding box in world space (derived from model * local bounds).
struct Aabb {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

/// RE-minimal mesh object (reference, T9 V3.8).
///
/// Mirrors scene::MeshObject but keeps only RE-direct fields per SPEC §12.4:
/// - `mesh` is the GPU handle (never raw mesh bytes)
/// - `model` is the world model matrix (uniform-ready)
/// - `bounds` is the world-space AABB `worldBounds` (derived: `model * localBounds`)
/// - `material` is the deduped RE material handle (never verbatim desc)
struct ReMeshObject {
    AssetHandle mesh{};              ///< GPU handle (AssetRegistry) — handle
    glm::mat4 model{1.0f};           ///< world model matrix — uniform-ready
    Aabb bounds{};                   ///< world-space AABB (derived)
    /// Deduped RE material, SHARED with its owner (T13 ownership discipline:
    /// no raw borrow; null is the documented "no material" value).
    std::shared_ptr<IMaterial> material{nullptr};
};

} // namespace re::render::re_scene

// Alias for broker convenience where render::ReMeshObject is expected.
namespace re::render {
using ReMeshObject = re_scene::ReMeshObject;
}
