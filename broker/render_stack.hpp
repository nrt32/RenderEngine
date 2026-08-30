#pragma once

// broker/render_stack.hpp — RenderStack: the broker-owned set of technique
// renderers the view synchronizer composes layers with (SPEC §11 composition
// root wiring; NOT a mapper — coordinator component, exempt from
// `broker_per_type` which governs `class *Mapper` files only).
//
// Before this aggregate existed, ViewSynchronizer could not build REAL layers
// (it had no renderers to bind drawLayer closures to) and filled the gap with
// a locally-defined do-nothing IRenderable — the "silently drops volumes"
// review finding. The stack is the missing half of that fix: mappers produce
// RE values, the stack owns WHO draws them.
//
// Ownership (T13): every member is a shared_ptr co-owned with whoever else
// shares it (the AppContext composition root, tests); a stored View item
// captures the shared_ptr, so a ReView can never outlive the renderer it
// draws with. The asset registry is THE single store instance every renderer
// and mapper resolves through (one GL object per distinct content, SPEC §7).
//
// `pipeline` is null unless OIT is enabled: when present, the synchronizer
// routes transparent mesh instances into the compositor's capture stage
// instead of inline layers (FR-render.3 engagement contract), and the
// MeshRenderer is constructed WITH the pipeline so its own direct-render path
// stays consistent. No raw gl* here (renderers own GL via core/).

#include <array>
#include <memory>

#include "render/asset_registry.hpp"
#include "render/contour_renderer.hpp"
#include "render/linked_list_oit.hpp"
#include "render/mesh_renderer.hpp"
#include "render/plane_renderer.hpp"
#include "render/slice_renderer.hpp"
#include "render/volume_renderer.hpp"
#include "render/volume_slice_renderer.hpp"
#include "scene/iscene_object.hpp"

namespace re::broker {

/// Global renderer call order that governs cross-type ordering inside each
/// Layer (SPEC §3.1 V7 — dumb LAYER_0..7 with scoped priority; this is the
/// V7 dispatch order covering 9 kinds per SPEC §6 `SceneKind::Count=9` while
/// `Layer::Count` stays `8` because layers are stacking, kinds are dispatch).
/// Lower index draws first within the same Layer; priority is scoped inside
/// the same (layer, technique) bucket so a VolumeSlice priority 100 still
/// draws before a Contour priority 0 on the same LAYER_0 when the global
/// order says VolumeSlice before Contour. The order is explicit and hardcoded
/// (BGFX Sequential / UE AddPass precedent) — Volume, VolumeSlice, Plane,
/// Csg, Mesh, MeshSlice, Point, Line, Contour — `SceneKind::Count=9`
/// (`Layer::Count` stays `8` — layers are stacking, kinds are dispatch per
/// SPEC §3.1/§6; array size `9` covers `Csg` before `Mesh` composite via
/// `CsgOitStage` Puxel resolve). The synchronizer's stable_sort groups by
/// (uint16(layer) asc, orderIdx asc, priority asc, insertionIdx asc) before
/// dispatching to the render-side view. Deterministic regardless of store
/// insertion order because insertionIdx is the stable tie for same
/// layer+type+priority. No per-view LayerMask or per-object override map.
inline constexpr std::array<scene::SceneKind, 9> techniqueOrder{
    scene::SceneKind::Volume,
    scene::SceneKind::VolumeSlice,
    scene::SceneKind::Plane,
    scene::SceneKind::Csg,
    scene::SceneKind::Mesh,
    scene::SceneKind::MeshSlice,
    scene::SceneKind::Point,
    scene::SceneKind::Line,
    scene::SceneKind::Contour};

/// The per-technique renderer set one bridge composes with.
struct RenderStack {
    /// The shared GPU asset store (single instance across all renderers).
    std::shared_ptr<render::AssetRegistry> assets;
    std::shared_ptr<render::MeshRenderer> mesh;            ///< opaque + auto-OIT
    std::shared_ptr<render::SliceRenderer> meshSlice;      ///< geometry-shader clip
    std::shared_ptr<render::VolumeRenderer> volume;        ///< ray-cast
    std::shared_ptr<render::VolumeSliceRenderer> slice;    ///< GPU plane extraction
    std::shared_ptr<render::PlaneRenderer> plane;          ///< textured quads
    std::shared_ptr<render::ContourRenderer> contour;      ///< geom-shader outlines
    /// Null unless enableOIT — the linked-list transparency pipeline wired
    /// into `mesh` (capture/composite orchestrated by ViewCompositor).
    std::shared_ptr<render::LinkedListOIT> pipeline;

    /// Build a fully-wired stack over `assets`; when `enableOIT` is true the
    /// returned stack also carries the linked-list pipeline and hands it to
    /// the MeshRenderer (auto-engagement on transparent scenes, FR-render.3).
    static std::shared_ptr<RenderStack> create(
        std::shared_ptr<render::AssetRegistry> assets, bool enableOIT = false);
};

} // namespace re::broker
