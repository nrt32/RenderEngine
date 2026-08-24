# broker/ — app → RE mediation (SPEC §11, V3.2b T3)

Heavily abstracted per-type `scene → render` mediation library `re_broker` (`STATIC`,
peer to `scene`/`render`/`core`). Owns no GL and no app rendering logic — only
`IMapper<AppT,ReT>` per-type translation plus `generation/contentHash` caching
(SPEC §10) and persistence. Only library that may include **both** `scene/` and
`render/` (`app` never includes `render/` directly, `render` never includes `scene/`).

## Interfaces (ISP-split)

- `IMapper<AppT,ReT>{map(Ctx)}` pure translation (`i_mapper.hpp`).
- `ICachedMapper<AppT,ReT> : IMapper{mapCached, invalidate}` cached (ISP — stateless
  `PlaneMapper` implements only `IMapper`, stateful `CameraMapper`/`MeshObjectMapper`
  implement `ICachedMapper`).
- `i_cached_mapper.hpp` is an alias include of `i_mapper.hpp` for DIP stability.

## Registry (OCP)

- `Broker{registerMapper<AppT,ReT>(unique_ptr<IMapper<AppT,ReT>>), get<AppT,ReT>()}`
  keyed by `std::type_index(typeid(AppT))` (OCP — no `enum` switch), plus concrete
  `registerMapper<MapperT>` / `get<MapperT>` overload keyed by `MapperT` type.
  Adding a new `AppT` (e.g. `PointCloudObject`) needs one new `*Mapper` file + one
  `registerMapper` call, zero edits to existing mappers or `ViewMapper`.

## Per-type inventory (one file per mapper — `broker_per_type`)

- `camera_mapper.*` — `ICachedMapper<scene::Camera, render::Camera>` (per-field viewGen/projGen; `mapCached` memoizes ONE slot per owning-view id — key is a CompositeKey over viewId + both generations + an FNV-1a fingerprint of the camera's stable parameter bytes — so two cameras never thrash each other; hit/miss counters are test evidence)
- `material_mapper.*` — `IMapper<scene::MeshMaterialDesc, shared_ptr<render::IMaterial>>` (Phong values → canonical store-resident materials; value-dedup via `AssetRegistry::registerMaterial`, the SPEC §12 hand-off that replaced the old `material = nullptr` placeholder)
- `mesh_object_mapper.*` — `ICachedMapper<scene::MeshObject, render::MeshInstance>` (AssetHandle via AssetRegistry dedup; composes MaterialMapper for its presentation field)
- `mesh_slice_object_mapper.*` — `ICachedMapper<scene::MeshSliceObject, render::SliceScene>` (contextual: the clip plane comes from the VIEW by value, converted through the one PlaneMapper rule; cache keyed id+generation+plane)
- `volume_object_mapper.*` — `ICachedMapper<scene::VolumeObject, render::VolumeInstance>` (a REAL ray-cast layer; the fix for the "sync silently drops volumes" review finding)
- `volume_slice_object_mapper.*` — `ICachedMapper<scene::VolumeSliceObject, render::VolumeSliceInstance>` (GPU plane extraction; converts the view's VoxelIndex plane against the OBJECT's own display-frame transform via `indexPlacementFromModel`)
- `plane_mapper.*` — `IMapper<scene::PlaneDesc, render::ClipPlane>`: THE single voxel-index → world conversion (`world = indexPlacement · ((p + ½) · spacing)`; normal passes through — it is declared world-space). Typed errors when VoxelIndex is requested without a usable volume role. `indexPlacementFromModel` recovers the index-space placement from a canonical unit-cube model.
- `plane_object_mapper.*` — `IMapper<scene::PlaneObject, render::PlaneInstance>` (textured quads; named `PlaneMapper` before T20 renamed it to match this inventory and free the name for the PlaneDesc rule above)
- `contour_mapper.*` — `IMapper<scene::ContourObject, render::ContourObject>` (AssetHandle injection; World-space object planes pass through, VoxelIndex is a typed error there because a contour's plane is post-model by contract)
- `asset_store.*` — generational `BrokerAssetHandle` skeleton (T3: pointer dedup, typed `StaleHandle` code 2; T7: content-hash AssetId). CPU-side identity layer only — no GL, no core/. The GPU-side multi-kind store (mesh/volume/image/material, ref-counted, content-hash-deduped) is `render::AssetRegistry` since T14 (SPEC §7); broker mappers resolve handles through it.
- `render_stack.*` — the technique-renderer set (Mesh/Slice/Volume/VolumeSlice/Plane/Contour renderers over ONE AssetRegistry, optional LinkedListOIT). Not a mapper: the layers the synchronizer builds bind their drawLayer closures to these renderers.
- `app_context.*` — the DIP composition root `{SceneStore, Broker(full default inventory), RenderStack, ViewBridge}`; the ONLY constructor call an app needs.
- `slice_display.*` — slice-display camera factories returning `scene::Camera` values (`makeSliceCamera`, `make3dCamera`) plus `toRenderCamera` (the CameraMapper translation without cache/validation); formerly `app/mpr_camera.*`, moved here because they build RE-side types and app must not name them.
- `view_synchronizer.*` — polls `SceneStore::storeGeneration()` early-out + computed `dirtyFieldsSince` (SRP: cache/dirty); item ids translate into REAL renderer-bound layers — an unresolvable id is typed error code 11, never a silently skipped item
- `view_compositor.*` — owns dispatch/present (SRP: ReView map); runs the OIT capture/composite stage after each view pass when transparent instances are pending
- `view_bridge.*` — `IViewBridge` façade composing `ViewSynchronizer` + `ViewCompositor` (SRP via composition)
- `stable_key.hpp` — THE single ReView identity key `{version, layoutId, viewId}` shared by synchronizer caches and the compositor's ReView map (one definition — divergent twins were a persistence-honesty review finding)

App never holds an `IMapper`; only `IViewBridge` (DIP via the `AppContext`
composition root). Since T20 this ACL is mechanically enforced:
audit rule `acl_app_render` fails any `#include <render/…>` under `app/`
(headers included) — every capability sample drives rendering exclusively
through `AppContext` + `IViewBridge{sync, renderAll, presentAll}`.

## Build

`re_broker` `STATIC` links to `re_scene` + `re_render` + `re_core` (never raw `gl*`;
`gpu_api_ownership` holds — `broker/` outside `core/` so any `gl*(` fails audit).

## Ownership / guardrail

- No raw `gl*` in `broker/` (audit `gpu_api_ownership`).
- One `class *Mapper` per file (audit `broker_per_type`); `ViewBridge`/`ViewSynchronizer`/`ViewCompositor`/`RenderStack` are coordinators, not mappers.
- `ISP`: `IMapper` must not expose `mapCached` (`isp_mapper_forbid`).
- `app → IViewBridge` only (audit `broker_app_reach`); `app ↛ render/` includes enforced by `acl_app_render`.

