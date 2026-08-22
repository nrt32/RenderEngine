# SPEC §9 — V2 future scope (roadmap)

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §9" mean this file. The numbered backlog
> mirrors `TASKS.md` (active backlog) — tasks 1–8 are the **completed** V2 loop;
> tasks 9+ are the V3 `scene/`/`broker`/View/List/Material/Light/Persistence
> overhaul (SPEC §10-§12). Section §9 stays as the archive of the eight shipped
> V2 items.

## 9. V2 future scope (roadmap) — eight completed items (archived)

Decisions recorded for V2 during post-loop review. None are V1 requirements;
each is independently implementable behind the existing guardrails. Items that
share a dependency (e.g. the multi-view workstream) land together.

**Priority order (approved): product-first.** The multi-view workstream
(V2.3 → V2.4 → V2.5) delivers the product value first, then the low-risk
refactors (V2.1/V2.2/V2.10), then maintainability (V2.6/V2.7).

| P | # | Item | Scope / rationale | Depends on |
|---|---|---|---|---|
| 1 | V2.3 | Narrow `IRenderer` interface + shared `render/types.hpp` | Move `Camera`/`RenderTarget` out of `mesh_renderer.hpp` into a shared header; a pure abstract `render` contract implemented by `Mesh/Plane/Volume/SliceRenderer`. Lands as the dispatch mechanism of the multi-view workstream (industry pattern: per-technique renderers, interfaces only where polymorphism helps). | — (foundation) |
| 2 | V2.4 | Multi-view rendering (Model B: per-view FBO + engine blit) | Front-end shares per-view window-section handles + abstract scene objects; RE dispatches objects to the correct renderer, renders into each view's own `core::Framebuffer`, then blits each FBO into its window rect via a new `core::blit`. No app-side viewport blending. Drives SceneView/MPRView composition. | V2.3, `core::blit` |
| 3 | V2.5 | Asset system: generational `AssetHandle` registry | `render::AssetRegistry` (`register()` → copyable `AssetHandle{index,generation}`); one GPU object per individual CPU object globally (fixes current per-renderer duplication: the same `data::Mesh` uploads once even across `MeshRenderer`+`SliceRenderer`). Handles are the currency views exchange. | V2.3, V2.4 |
| 4 | V2.1 | Move `offscreen_context` + `read_pixels` to a new `utils/` module | Test-support + windowing (GLFW/EGL) are not core rendering; keep the raw-GL anchors (`core::loadCoreGl`, `core::readRgba8`) under `core/` so `gpu_api_ownership` / `no_production_readback` stay intact; `PixelReader` class in `utils/`. **Requires adding `utils` to `AUDIT_SOURCE_DIRS`.** | — |
| 5 | V2.2 | Platform-extensible context-backend factory | `utils::OffscreenContext` selects the no-display backend per-OS: EGL-surfaceless/Mesa on Linux, ANGLE-EGL or WGL on Windows, CGL on macOS (generalizes the Mesa-specific `EGL_PLATFORM_SURFACELESS_MESA` path). | V2.1 |
| 6 | V2.10 | Internal dirty-flag draw-state cache in `core/draw.cpp` | Keep the free-function `core::Draw` API and audit anchors; cache `setViewport`/`setClearColor`/`enable*`/`disable*` values and skip redundant `gl*` calls. Motivator: OIT mid-frame toggles. No API/audit change. | — |
| 7 | V2.6 | Shader externalization to `.glsl` files | Replace inline `constexpr char[]` GLSL in render/ with `.glsl` files loaded by `core::ShaderProgram` (adds syntax highlighting/editor navigation). Keep the t3 malformed-shader golden substring reproducible via a fixture file. | — (relocation only) |
| 8 | V2.7 | GLSL profile macro (`RE_GLSL_VERSION`) | Decouple the shader language level from the llvmpipe ceiling: 450 = portable floor (tests/CI), 460 = hardware floor. Single `#version` concern now that shaders live in files. | V2.6 |

## 9.1 V3 roadmap — pure-redesign `scene`/`broker`/View/List/Persistence (renamed `V3.x` → `T9..T18`)

Pure-redesign V3 (no new FRs — R1–R8 redesign scope per 2026-08-23 direction). The `V3.x` alias survives only for Spec traceability; the **accepted standard is `Tn: Title / D / T / G`**, global sequential `T9..T18` after `T1–T16` (V1) + `V2-T1..V2-T8` (V2). `TASKS.md` holds the binding headings — this table mirrors them.

| P | Task | Spec alias | Item | Scope / rationale | Depends on | Spec |
|---|---|---|---|---|---|---|
| 1 | T1 | V3.1 | `scene/` value library — GL/RE-free app-side scene description | Extract every app-authored type into owning `scene/` value lib `re::scene`: `View`, `Camera` (`pan/rotate/zoom/orbit` → `viewMatrix()`), `PlaneDesc`, `SceneObject` family (`MeshObject`, `MeshSliceObject`, `VolumeObject`, `VolumeSliceObject`, `PlaneObject`) `{asset ref, transform, presentation}`, plus `SceneStore`/`ViewStore` stable handles + per-field `generation`. No `App` prefix — namespace is prefix. `scene/` links to `data/`+`volume/`+`glm` only; `RE` keeps only translated `Re*` types (SPEC §3.1). Pure value semantics — copyable, no `Handle`, `core` never included. <br>**Pure redesign** — no new material/light beyond Phong fixed headlight. | — | §3, §12 |
| 2 | T2 | V3.2a | `CompositeKey` + `TranslateContext` + `DrawContext` skeletons — must land before any cached mapper | Land skeletons `CompositeKey{Version,LayoutId,Id,Gen,Hash}`, `TranslateContext{viewPlane,view,volumeModel,dims,meshBounds}` (ISP — not God `ReView*`), `DrawContext{Viewport,ClearColor,Depth,Blend,spy}` instance replacing `core/draw.cpp` static cache (SRP per `FrameContext`). Value types, header-only, unblock `T3`/`T5`/`T6`. | T1 | §10.1, §10.4, §11.4 |
| 3 | T3 | V3.2b | `broker/` library — per-type `IMapper`/`ICachedMapper` + `Broker` + `IViewBridge` SRP-split | Heavily abstracted `broker/` `STATIC` (peer `scene`/`render`): `IMapper<AppT,ReT>{map(Ctx)}` pure vs `ICachedMapper:IMapper{mapCached,invalidate}` ISP-split, `Broker{registerMapper<T>, get<T>}` `type_index` (OCP, no `enum` switch), `IViewBridge` composing `ViewSynchronizer`+`ViewCompositor` SRP-split. One file per mapper (`camera_mapper.*`…`view_bridge.*`). App never holds `IMapper`; only `IViewBridge` (DIP, see §11). | T1, T2 | §11 |
| 4 | T4 | V3.3 | `scene::Camera` manipulable (`pan/rotate/zoom/orbit`) → view matrix to RE | Move `pan/rotate/zoom/orbit` + factories `makeOrthoForSlice`/`makePerspectiveCrosshair` into `scene::Camera` (`scene/camera.hpp`). Scene sends only `viewMatrix()`(+`proj`+`pos`) via `CameraMapper → render::Camera{view,proj,pos}`. `2D` ortho vs `3D` perspective validated by mapper (plane present → ortho). Per-field `viewGen`/`projGen`. | T1, T3 | §3.1, §12 |
| 5 | T5 | V3.4 | `View` per screen section + heterogeneous item list — delete `ViewRenderer` | `render::View` (`ReView`) per `ViewRect` owns `ViewTarget{Texture2D+Framebuffer}` (`rect.w×h`) + `Camera` + `optional<ClipPlane>` (`2D` vs `3D`) + `list<IRenderable>` (`VolumeSlice+MeshSlice` / `Volume+Mesh`). `IRenderable` type-erased `drawLayer(SceneT,Camera,DrawContext&)` — `View` never knows renderer. Each renderer gains `drawLayer(...,DrawContext&)` assuming `ReView` already `bind+viewport+clear`; single-item `render()` keeps `clear` for direct tests. Delete `ViewRenderer` + `render/types.hpp` raw-pointer `Scene` variant (replaced by `AssetId`). | T1, T3 | §3.2, §11 |
| 6 | T6 | V3.5 | Persistence & layout/page lifetime — `CompositeKey` full | Full content-addressed: `CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash}` (not `id+size` dump). `ReView`/`Re*Object`/`ViewTarget` persist — `Camera::rotate` dirties only `CameraMapper`, `2D→3D` toggle same `ViewId` rebinds plane+items without `ReView` map churn, size resize recreates only `ViewTarget` inner `FBO`, layout count/set change inserts/erases `ReView`s. Hybrid `storeGen` poll + `dirtyFieldsSince()` + `markDirty()` via `IDirtyTracker`; `LayoutSpec` relative → `resolve(windowSize,dpr)` absolute `Rect`. | T3, T5 | §10 |
| 7 | T7 | V3.6 | Data asset persistence — `SceneStore`-owned `AssetId` (`AssetRegistry<T>`) | Keep generational `AssetHandle` but key by stable `AssetId` from `scene::SceneStore` (`AssetId{generation,contentHash}`) not pointer `byObject_` (`render/asset_registry.hpp:137`). `data::Mesh` stays pure. `AssetRegistry<T>` template extensible, no per-kind duplicate. | T1, T6 | §7, §12 |
| 8 | T8 | V3.7 | Even hierarchy note — Phong-only stays (deferred) | **Pure redesign: no expansion.** Keep `render::IMaterial→PhongMaterial` single path + fixed headlight (PBR/`Slice`/`Contour`+`ILight` deferred as §1 non-goal). Only tightens `TransferFunction` beside `VolumeMaterial` in `VolumePresentation`. | T3, T5 | §12 |
| 9 | T9 | V3.8 | RE-minimal types — `render/re_scene/` inventory | Audit `scene→render`: `Re*` keeps only `Re`-direct values (`AssetHandle`/`ReMaterial*`/`ClipPlane`/`worldBounds`/`sliceUVW` where derived), never `app::MaterialDesc`. Produce binding inventory `docs/re_scene_inventory.md`. | — | §12.4 |
| 10 | T10 | V3.9 | EOL skeletons — deferred stretch | `RHI`/`IJobExecutor`/serialisation skeletons **deferred** (stretch) — only `DrawContext`/`IDirtyTracker`/`Version` extension points remain. | T2, T6 | §3, §11.6, §13.8 |