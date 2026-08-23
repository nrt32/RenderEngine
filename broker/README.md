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

- `camera_mapper.*` — `ICachedMapper<scene::Camera, render::Camera>` (per-field viewGen/projGen)
- `mesh_object_mapper.*` — `ICachedMapper<scene::MeshObject, render::MeshInstance>` (AssetHandle via AssetRegistry dedup)
- `asset_store.*` — generational `BrokerAssetHandle` skeleton (T3: pointer dedup, typed `StaleHandle` code 2; T7: content-hash AssetId)
- `view_synchronizer.*` — polls `SceneStore::storeGeneration()` early-out + bounded `dirtyFieldsSince` (SRP: cache/dirty)
- `view_compositor.*` — owns dispatch/present (SRP: ReView map)
- `view_bridge.*` — `IViewBridge` façade composing `ViewSynchronizer` + `ViewCompositor` (SRP via composition)

App never holds an `IMapper`; only `IViewBridge` (DIP via `AppContext` composition root).

## Build

`re_broker` `STATIC` links to `re_scene` + `re_render` + `re_core` (never raw `gl*`;
`gpu_api_ownership` holds — `broker/` outside `core/` so any `gl*(` fails audit).

## Ownership / guardrail

- No raw `gl*` in `broker/` (audit `gpu_api_ownership`).
- One `class *Mapper` per file (audit `broker_per_type`); `ViewBridge`/`ViewSynchronizer`/`ViewCompositor` are coordinators, not mappers.
- `ISP`: `IMapper` must not expose `mapCached` (`isp_mapper_forbid`).
- `app → IViewBridge` only (audit `broker_app_reach`).

