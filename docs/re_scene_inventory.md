# ReScene Inventory — Binding RE-minimal Field Audit (SPEC §12.4, V7 T9)

> **Binding inventory** required before any `render/re_scene/*.hpp` lands (SPEC §12.4).
> `Re*` keeps only `Re`-direct values (`AssetHandle`/`ReMaterial*`/`ClipPlane`/
> `ReLight[]`/`worldBounds`/`sliceUVW` where derived), never verbatim
> `scene::MaterialDesc` / `data::Mesh` bytes. Guardrail `asset_indirection`
> enforces: `grep -R "data::Mesh::positions" render/re_scene/` → 0 hits.
>
> **Rationale domain** per row ∈ {`derived`|`uniform-ready`|`handle`}:
> - `handle` — indirection to GPU object or dedup cache (AssetHandle, ReMaterial*, IRHITexture/Framebuffer, IRenderable handle).
> - `uniform-ready` — already GPU-uniform shape (`mat4`, `vec4`, `Camera{view,proj,pos}`), uploaded without conversion.
> - `derived` — computed from app field + context (worldBounds, ClipPlane world, normalMatrix, sliceUVW, ReTfUniforms).

Reference headers this iteration: `render/re_scene/mesh_object.hpp` exposes
`ReMeshObject{AssetHandle, model, worldBounds, ReMaterial*}`; `render/re_scene/csg_object.hpp` exposes
`ReCsgObject{AssetHandle base, vector<AssetHandle> subs/paints, mat4 model, worldBounds}`; `render/re_scene/point_object.hpp` exposes
`RePointObject{vec3 pos, float radius, vec4 color, PointFill}`; `render/re_scene/line_object.hpp` exposes
`ReLineObject{vec3 a,b, vec4 color, float width, DashPattern}`.

---

## ReMeshObject — `render/re_scene/mesh_object.hpp` (reference, T9)

| Field | RE Type | Rationale | Derivation / Why RE-minimal |
|---|---|---|---|
| mesh | `AssetHandle` | handle | `scene::MeshObject::mesh` → `AssetRegistry::registerAsset` handle `{index,generation}` (content-hash dedup T7); never `data::Mesh::positions` copy |
| model | `glm::mat4` | uniform-ready | `scene::MeshObject::transform` directly as `uModel` uniform (no conversion) |
| bounds | `Aabb` | derived | `model * data::Mesh::bounds()` → world-space AABB `worldBounds` for culling/picking (RE needs world, not local) |
| material | `shared_ptr<IMaterial>` | handle | `scene::MeshMaterialDesc` → shared deduped material via `MaterialMapper` value-hash dedup (never verbatim desc); `PhongMaterial` today. T13: shared ownership, no raw borrow |

---

## ReVolumeObject — deferred reference (inventory only, T8 Phong-only)

| Field | RE Type | Rationale | Derivation / Why RE-minimal |
|---|---|---|---|
| volumeTex | `IRHITexture*` | handle | `data::VolumeDataset` → `IRHIContext::createTexture3D` (never `data::VolumeDataset::voxels` copy) |
| model | `glm::mat4` | uniform-ready | `scene::VolumeObject::transform` as `uModel` |
| worldBounds | `Aabb` | derived | `model * unitCube[0,1]^3` (dataset occupies [0,1]^3 model) |
| material | `const VolumeMaterial*` | handle | `scene::VolumeMaterialDesc` → `VolumeMaterial*` via `VolumeMaterialMapper` (handle, not verbatim) |
| tfUniforms | `ReTfUniforms` | derived | `volume::TransferFunction` → LUT/uniform array via `TransferFunctionMapper` (beside material per §12.5) |

---

## RePlaneObject — deferred reference (inventory only)

| Field | RE Type | Rationale | Derivation / Why RE-minimal |
|---|---|---|---|
| imageTex | `IRHITexture*` | handle | `data::Image` → `IRHIContext::createTexture2D` (never raw pixel copy) |
| model | `glm::mat4` | uniform-ready | `scene::PlaneObject::transform` as `uModel` for quad placement |
| worldBounds | `Aabb` | derived | `model * unitQuadXY` bounds → world AABB |

---

## ReCsgObject — `render/re_scene/csg_object.hpp` (V7 T9, flat Puxel CSG)

| Field | RE Type | Rationale | Derivation / Why RE-minimal |
|---|---|---|---|
| baseHandle | `AssetHandle` | handle | `scene::CsgObject::base.mesh` → `AssetRegistry::registerAsset` handle (content-hash dedup, never `data::Mesh::positions` copy) |
| subHandles | `vector<AssetHandle>` | handle | `scene::CsgObject::subtractors[].mesh` → per-subtractor handles via registry (handle vector, not verbatim bytes) |
| subTransforms | `vector<glm::mat4>` | uniform-ready | `scene::CsgObject::subtractors[].operandTransform` preserved per-operand for GPU Puxel per-operand model |
| paintHandles | `vector<AssetHandle>` | handle | `scene::CsgObject::paints[].oper.mesh` → per-paint handles (handle vector) |
| paintTransforms | `vector<glm::mat4>` | uniform-ready | `scene::CsgObject::paints[].oper.operandTransform` per-paint transform |
| paintBlends | `vector<float>` | derived | `scene::CsgObject::paints[].blend` override 0..1 (derived recolor mix factor, uniform-ready scalar) |
| paintInteriorFlags | `vector<bool>` | derived | `scene::CsgObject::paints[].paintInterior` true→volume interior, false→surface strip (derived flag for csg_resolve) |
| model | `glm::mat4` | uniform-ready | `scene::CsgObject::transform * base.operandTransform` as `uModel` (uniform-ready) |
| worldBounds | `Aabb` | derived | `model * data::Mesh::bounds(base)` → world-space AABB `worldBounds` (derived, RE needs world) |

---

## RePointObject — `render/re_scene/point_object.hpp` (V7 T9, impostor billboard)

| Field | RE Type | Rationale | Derivation / Why RE-minimal |
|---|---|---|---|
| pos | `glm::vec3` | derived | `scene::PointObject::position` transformed by `transform * vec4(pos,1)` → world-space `pos` (derived) |
| radius | `float` | uniform-ready | `scene::PointObject::radius` (world when worldUnits true, else px) as uniform-ready scalar |
| worldUnits | `bool` | uniform-ready | `scene::PointObject::worldUnits` toggle for `radiusScreen` projection-delta scaling (uniform-ready) |
| color | `glm::vec4` | uniform-ready | `scene::PointObject::color` straight RGBA (alpha<1 → premul OIT, uniform-ready) |
| fill | `PointFill` | uniform-ready | `scene::PointObject::fill` {Solid,Hollow,GridDashed} as uniform-ready enum for impostor interior |
| fillParam | `float` | uniform-ready | `scene::PointObject::fillParam` grid density / hollow thickness (uniform-ready) |

---

## ReLineObject — `render/re_scene/line_object.hpp` (V7 T9, SSBO view-quad strip)

| Field | RE Type | Rationale | Derivation / Why RE-minimal |
|---|---|---|---|
| a | `glm::vec3` | derived | `scene::LineObject::segments[].a` transformed by `transform * vec4(a,1)` → world-space endpoint `a` (derived) |
| b | `glm::vec3` | derived | `scene::LineObject::segments[].b` transformed → world-space `b` (derived) |
| color | `glm::vec4` | uniform-ready | `scene::LineObject::color` stroke color (premul alpha for OIT, uniform-ready) |
| width | `float` | uniform-ready | `scene::LineObject::width` (world when worldUnits true, else px) uniform-ready |
| dash | `DashPattern` | uniform-ready | `scene::LineObject::dash` {dashLength,gapLength,offset} as uniform-ready scalars for Rougier `mod(s)` (covers `dashLength/gapLength/dashOffset/dashed` derived flag) |
| worldUnits | `bool` | uniform-ready | `scene::LineObject::worldUnits` toggle for width projection-delta scaling (uniform-ready) |
| cap | `LineCap` | uniform-ready | `scene::LineObject::cap` {Round,Square} end shape via `LineCap` uniform-ready (view-quad cap disc vs square) |
| join | `LineJoin` | uniform-ready | `scene::LineObject::join` {Miter,Bevel} node shape uniform-ready (miter extension vs bevel cut) |
| miterLimit | `float` | uniform-ready | `scene::LineObject::miterLimit` `4→bevel` threshold uniform-ready for acute-angle fallback |

---

## ReView — `render/view.hpp` (`ReView`) per screen section (T5)

| Field | RE Type | Rationale | Derivation / Why RE-minimal |
|---|---|---|---|
| target | `ViewTarget` | handle | `ViewTarget{IRHITexture+IRHIFramebuffer}` sized `rect.w×h` (handle to GPU FBO, delegates lifecycle) |
| camera | `Camera{view,proj,pos}` | uniform-ready | `scene::Camera::viewMatrix()/projMatrix()/eye()` via `CameraMapper` → uniforms `uView/uProj/uEye` |
| clipPlane | `optional<ClipPlane>` | derived | `scene::PlaneDesc` + `TranslateContext{ViewContext,VolumeContext}` → world `ClipPlane` via `PlaneMapper` (nullopt = 3D) |
| lights | `vector<ReLight>` | derived | `scene::View::lights vector<LightDesc>` → `ReLight` variant via `LightMapper` visitor (world-space pos/dir) |
| items | `vector<IRenderable>` | handle | type-erased `ITypeErasedDraw` handles owning `Re*Object+model+material` (View never knows renderer) |

---

## ReScene — composition root (broker `ViewBridge` + `SceneStore`)

| Field | RE Type | Rationale | Derivation / Why RE-minimal |
|---|---|---|---|
| views | `map<CompositeKey, ReView>` | handle | `LayoutId→ViewId` → `CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash}` → `ReView` lifetime map in `ViewCompositor` |
| assets | `AssetRegistry` | handle | global `AssetStore<T>` handles (`AssetHandle`) shared across views (never per-view duplicate upload) |
| version | `uint32_t` | derived | persistence schema `Version` from `CompositeKey::Version` (SHA-256 of field inventory, EOL cache-key versioning) |

---

## AssetHandle — `render/asset_registry.hpp` (generational handle)

| Field | Type | Rationale | Derivation / Why RE-minimal |
|---|---|---|---|
| index | `uint32_t` | handle | slot index in `AssetRegistry::slots_` |
| generation | `uint32_t` | handle | generational validation (stale `gen+1` → typed error code 2) |
| contentHash | `uint64_t` | derived | `hashStableBytes` (SHA-256 truncate) of mesh stable bytes, not pointer (T7 dedup) |

---

## Summary

- **9 tables / 47 fields** — each field rationale ∈ {`derived`,`uniform-ready`,`handle`}.
- RE keeps only derived/uniform/handle values; never verbatim `scene::MaterialDesc` / `data::Mesh::positions`.
- `ReMeshObject` / `ReCsgObject` / `RePointObject` / `ReLineObject` headers live in `render/re_scene/*.hpp` (V7 T9); `ReVolume/RePlane` remain inventory-only (deferred); `ReView/ReScene/AssetHandle` complete the audit.
- Guardrail `asset_indirection` (`forbid_grep data::Mesh::positions|data::VolumeDataset::voxels`) active since T9; `grep -R "data::Mesh::positions" render/re_scene/` → 0 hits.
