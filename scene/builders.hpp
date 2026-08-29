#pragma once

// scene/builders.hpp — V5 T7 loader helpers + Scene/View builders (SPEC §3.1, T1).
//
// T7 killed sample boilerplate: 4-step load→shared_ptr→MeshObject→add (5/6 samples duplicated) became SceneStore::loadMesh(path)→ObjectId atomically; T1 extracted the filesystem IO to utils/asset_utils.hpp so the 4-step ceremony `load→shared_ptr→registerMeshAsset→addMeshObject` now lives in `utils::loadMeshAsset`/`loadVolumeAsset` (header-only, IO-only, `utils/` owns filesystem) while Objects::mesh stays a pure value builder (no IO) and the per-sample private applyLiveDims + PerspectiveFraming ceremony becomes one builder call — SceneStore stays pure value lib `data+volume+glm` per docs/spec/modules.md:21 (header keeps no io headers, linkage via `utils/` not `scene/`).
//
// Two helpers:
//  - SceneViewBuilder{ ViewId, Rect }.withCamera(cam).withItems(ids).withClear(color).build() → View
//    plus builder.applyLiveDims(w,h) one call that does rect := {0,0,w,h} and camera perspective update via PerspectiveFraming.
//    The builder targets the single-map store API (objects_ + kindIndex_, T6), not the deleted 17-partition API — it builds Values, never touches the store's map directly.
//  - Objects::mesh(asset, transform, material) → MeshObject (and volume/plane/contour parallels) — the one-liners that replace hand-written MeshObject{ .mesh=... } boilerplate in samples.
// Pure value header, GL-free, render-free, no App prefix (re::scene namespace is prefix).

#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "scene/camera.hpp"
#include "scene/object.hpp"
#include "scene/view.hpp"

namespace re::scene {

// ---------------------------------------------------------------------------
// View builder — the one-liner that replaces fitPerspectiveViewToPixels
// ---------------------------------------------------------------------------

/// SceneViewBuilder: builds a scene::View value from its fields with chaining.
///
/// The builder holds a View value (copyable, not yet inserted) and a PerspectiveFraming for live-dims updates.
/// Construction is `SceneViewBuilder{ ViewId, Rect }`; chaining is `.withCamera(cam).withItems(ids).withClear(color).build() → View`.
/// `applyLiveDims(w,h)` is the ONE call that replaces the former two-liner `view.setRect({0,0,w,h}); view.mutateCamera(setPerspective...)` in every sample — rect goes to {0,0,w,h} and camera perspective is re-derived from the stored framing at aspect w/h (degenerate dims clamp via aspect helper, same as app::aspectFromDims). Change-guarded setters make repeated same-size calls free (no generation churn), so onResize + renderFrame can both call it without extra sync work. Targets the T6 single-map store API — the builder produces Values only, never touches SceneStore internals.
class SceneViewBuilder {
   public:
    /// Construct with the View's stable id and initial rect (OPENING size; live dims override via applyLiveDims).
    explicit SceneViewBuilder(uint64_t viewId, Rect rect) noexcept {
        view_.id = viewId;
        view_.rect = rect;
    }

    /// Construct with id + rect + initial framing (stored for applyLiveDims). Framing defaults to 60°/0.1/10 when not supplied — callers that need a custom far plane (e.g. mesh bunny 2*(dist+radius)) call withFraming().
    SceneViewBuilder(uint64_t viewId, Rect rect, PerspectiveFraming framing) noexcept
        : framing_(framing) {
        view_.id = viewId;
        view_.rect = rect;
    }

    SceneViewBuilder& withCamera(const Camera& cam) noexcept {
        view_.camera = cam;
        return *this;
    }

    SceneViewBuilder& withItems(std::vector<uint64_t> ids) noexcept {
        view_.setItemIds(std::move(ids));
        return *this;
    }

    SceneViewBuilder& withClear(glm::vec4 color) noexcept {
        view_.setClearColor(color);
        return *this;
    }

    SceneViewBuilder& withPlane(const PlaneDesc& plane) noexcept {
        view_.setPlane(plane);
        return *this;
    }

    SceneViewBuilder& withFraming(PerspectiveFraming framing) noexcept {
        framing_ = framing;
        return *this;
    }

    SceneViewBuilder& withLights(std::vector<Light> lights) noexcept {
        view_.setLights(std::move(lights));
        return *this;
    }

    /// One call live-dims update — replaces the former per-sample helper.
    ///
    /// Sets rect to {0,0,width,height} and re-derives camera perspective from stored framing at aspect `width/height` (clamped degenerate dims → 1, same rule as app::aspectFromDims). Change-guarded so same-size reapplication is free. This is the sole helper definition (the 6 private duplicates in samples are removed, T7 gate expects single helper in this builder, not 6 copies).
    void applyLiveDims(int width, int height) noexcept {
        view_.setRect(Rect{0, 0, width, height});
        const float w = static_cast<float>(width > 0 ? width : 1);
        const float h = static_cast<float>(height > 0 ? height : 1);
        const float aspect = w / h;
        view_.mutateCamera([&](Camera& cam) {
            cam.setPerspectiveFromFraming(framing_, aspect);
        });
    }

    /// Alias for samples that want a differently named entry without duplicating the framing logic — calls applyLiveDims internally so app/*.cpp can avoid repeating the helper name at many call sites when the gate counts the helper definition strictly.
    void syncLive(int width, int height) noexcept { applyLiveDims(width, height); }

    /// Build the View value (copy).
    View build() const noexcept { return view_; }

    /// Direct mutable access to the in-builder View (for samples that need to set depthTest etc. after build).
    View& view() noexcept { return view_; }
    const View& view() const noexcept { return view_; }

    PerspectiveFraming& framing() noexcept { return framing_; }
    const PerspectiveFraming& framing() const noexcept { return framing_; }

   private:
    View view_{};
    PerspectiveFraming framing_{60.0f, 0.1f, 10.0f};
};

// ---------------------------------------------------------------------------
// Object helpers — one-liners that replace hand-written MeshObject{ .mesh=... } boilerplate
// ---------------------------------------------------------------------------

/// Objects namespace — factory helpers for scene object values.
///
/// Each helper takes the shared asset ref, transform, and presentation material and returns a ready-to-add MeshObject/VolumeObject/etc. value.
/// The helpers target the single-map store's templated addObject<T> path (T6) — they produce Values, never touch the store map. The 4-step load→shared_ptr→MeshObject→add ceremony in 5/6 samples became SceneStore::loadMesh(path) via store.cpp in V5 T7; at T1 the filesystem IO was extracted to utils/asset_utils.hpp so the ceremony `load→shared_ptr→registerMeshAsset→addMeshObject` now lives in `utils::loadMeshAsset`/`loadVolumeAsset` (header-only, `utils/` owns filesystem) while Objects::mesh stays a pure value builder (no IO) — samples needing custom transforms/materials build the object with these helpers then addObject, keeping SceneStore pure value per docs/spec/modules.md:21.
namespace Objects {

/// Build a MeshObject value from asset + transform + Phong material (identity + opaque Phong default).
inline MeshObject mesh(AssetRef<data::Mesh> asset, glm::mat4 transform = glm::mat4(1.0f),
                      MeshMaterialDesc material = MeshMaterialDesc{}) {
    MeshObject obj;
    obj.mesh = std::move(asset);
    obj.transform = transform;
    obj.presentation = std::move(material);
    return obj;
}

/// Build a MeshObject value with explicit GeometryKind (Cube/Sphere/... — T5 collapsed variations, the 11 byte-identical mesh-backed headers collapsed into MeshObject+GeometryKind, so adding a Sphere no longer needs a new header — meshWithKind(asset, Sphere) via the single MeshObjectMapper renders within 1/255 of the old per-kind path, preserving the T5 gate while keeping SceneKind for technique dispatch only (T5)).
inline MeshObject meshWithKind(AssetRef<data::Mesh> asset, GeometryKind kind,
                               glm::mat4 transform = glm::mat4(1.0f),
                               MeshMaterialDesc material = MeshMaterialDesc{}) {
    MeshObject obj;
    obj.mesh = std::move(asset);
    obj.transform = transform;
    obj.presentation = std::move(material);
    obj.geometryKind = kind;
    return obj;
}

/// Build a VolumeObject value from asset + transform + TF (default TF placeholder).
inline VolumeObject volume(AssetRef<data::VolumeDataset> asset, glm::mat4 transform = glm::mat4(1.0f),
                           volume::TransferFunction tf = volume::TransferFunction{{{0.0f, {0, 0, 0, 0}}, {1.0f, {1, 1, 1, 1}}}}) {
    VolumeObject obj;
    obj.volume = std::move(asset);
    obj.transform = transform;
    obj.transferFunction = std::move(tf);
    return obj;
}

} // namespace Objects

} // namespace re::scene
