# ReScene Inventory — Binding RE-minimal Field Audit (SPEC §12.4, T9 V3.8)

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

Reference header this iteration: `render/re_scene/mesh_object.hpp` exposes
`ReMeshObject{AssetHandle, model, worldBounds, ReMaterial*}` only.

---

## ReMeshObject — `render/re_scene/mesh_object.hpp` (reference, this iteration)

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

- **6 tables / 23 fields** — each field rationale ∈ {`derived`,`uniform-ready`,`handle`}.
- RE keeps only derived/uniform/handle values; never verbatim `scene::MaterialDesc` / `data::Mesh::positions`.
- `ReMeshObject` reference header is the only `render/re_scene/*.hpp` landed this iteration; `ReVolume/RePlane/ReView/ReScene/AssetHandle` are inventory-only (headers deferred with T8).
- Guardrail `asset_indirection` (`forbid_grep data::Mesh::positions|data::VolumeDataset::voxels`) active since T9.
