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
| 2 | T2 | V3.2a | `CompositeKey` + `TranslateContext` + `DrawContext` skeletons — must land before any cached mapper | Land skeletons `CompositeKey{Version,LayoutId,Id,Gen,Hash}`, `TranslateContext{ViewContext{viewPlane,viewMatrix,projMatrix}, optional<VolumeContext{volumeModel,dims,voxelSpacing,meshBounds}>}` ISP-segregated (Q40:B — not God `ReView*`, not flat `viewPlane+view+volumeModel` fat), `DrawContext{Viewport,ClearColor,Depth,Blend,spy}` instance per `FrameContext` replacing `core/draw.cpp` static `invalidateDrawCache()` global (SRP via instance — Q43:B). Value types, header-only, unblock `T3`/`T5`/`T6`. | T1 | §10.1, §10.4, §11.4 |
| 3 | T3 | V3.2b | `broker/` library — per-type `IMapper`/`ICachedMapper` + `Broker` + `IViewBridge` SRP-split | Heavily abstracted `broker/` `STATIC` (peer `scene`/`render`): `IMapper<AppT,ReT>{map(Ctx)}` pure vs `ICachedMapper:IMapper{mapCached,invalidate}` ISP-split, `Broker{registerMapper<T>, get<T>}` `type_index` (OCP, no `enum` switch), `IViewBridge` composing `ViewSynchronizer`+`ViewCompositor` SRP-split. One file per mapper (`camera_mapper.*`…`view_bridge.*`). App never holds `IMapper`; only `IViewBridge` (DIP, see §11). | T1, T2 | §11 |
| 4 | T4 | V3.3 | `scene::Camera` manipulable (`pan/rotate/zoom/orbit`) → view matrix to RE | Move `pan/rotate/zoom/orbit` + factories `makeOrthoForSlice`/`makePerspectiveCrosshair` into `scene::Camera` (`scene/camera.hpp`). Scene sends only `viewMatrix()`(+`proj`+`pos`) via `CameraMapper → render::Camera{view,proj,pos}`. `2D` ortho vs `3D` perspective validated by mapper (plane present → ortho). Per-field `viewGen`/`projGen`. | T1, T3 | §3.1, §12 |
| 5 | T5 | V3.4 | `View` per screen section + heterogeneous item list — delete `ViewRenderer` | `render::View` (`ReView`) per `ViewRect` owns `ViewTarget{Texture2D+Framebuffer}` (`rect.w×h`) + `Camera` + `optional<ClipPlane>` (`2D` vs `3D`) + `list<IRenderable>` (`VolumeSlice+MeshSlice` / `Volume+Mesh`). `IRenderable` type-erased `drawLayer(SceneT,Camera,DrawContext&)` — `View` never knows renderer. Each renderer gains `drawLayer(...,DrawContext&)` assuming `ReView` already `bind+viewport+clear`; single-item `render()` keeps `clear` for direct tests. Delete `ViewRenderer` + `render/types.hpp` raw-pointer `Scene` variant (replaced by `AssetId`). | T1, T2, T3 | §3.2, §11 |
| 6 | T6 | V3.5 | Persistence & layout/page lifetime — `CompositeKey` full | Full content-addressed: `CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash}` (not `id+size` dump). `ReView`/`Re*Object`/`ViewTarget` persist — `Camera::rotate` dirties only `CameraMapper`, `2D→3D` toggle same `ViewId` rebinds plane+items without `ReView` map churn, size resize recreates only `ViewTarget` inner `FBO`, layout count/set change inserts/erases `ReView`s. Hybrid `storeGen` poll + `dirtyFieldsSince()` + `markDirty()` via `IDirtyTracker`; `LayoutSpec` relative → `resolve(windowSize,dpr)` absolute `Rect`. | T2, T3, T5 | §10 |
| 7 | T7 | V3.6 | Data asset persistence — `SceneStore`-owned `AssetId` (`AssetRegistry<T>`) | Keep generational `AssetHandle` but key by stable `AssetId` from `scene::SceneStore` (`AssetId{generation,contentHash}`) not pointer `byObject_` (`render/asset_registry.hpp:137`). `data::Mesh` stays pure. `AssetRegistry<T>` template extensible, no per-kind duplicate. | T1, T6 | §7, §12 |
| 8 | T8 | V3.7 | Even hierarchy note — Phong-only stays (deferred) | **Pure redesign: no expansion.** Keep `render::IMaterial→PhongMaterial` single path + fixed headlight (PBR/`Slice`/`Contour`+`ILight` deferred as §1 non-goal). Only tightens `TransferFunction` beside `VolumeMaterial` in `VolumePresentation`. | T3, T5 | §12 |
| 9 | T9 | V3.8 | RE-minimal types — `render/re_scene/` inventory | Audit `scene→render`: `Re*` keeps only `Re`-direct values (`AssetHandle`/`ReMaterial*`/`ClipPlane`/`worldBounds`/`sliceUVW` where derived), never `app::MaterialDesc`. Produce binding inventory `docs/re_scene_inventory.md` (6 tables/23 fields). | T1, T5, T7, T8 | §12.4 |
| 10 | T10 | V3.9 | EOL skeletons — deferred stretch | `RHI`/`IJobExecutor`/serialisation skeletons **deferred** (stretch) — only `DrawContext`/`IDirtyTracker`/`Version` extension points remain. | T2, T6 | §3, §11.6, §13.8 |
| 11 | T11 | V3.8b | GPU mesh contour — `ContourRenderer` via geometry shader | Replace CPU `app/mpr_contour` (`meshPlaneContour`/`overlayContour`) with GPU `render::ContourRenderer` (`contour.geom.glsl`) — `plane∩mesh` outline on GPU via `ContourObject{AssetHandle,ClipPlane}` + `ContourMapper`, `app/mpr_contour.hpp` deleted. | T1, T5, T7 | §3, §12.4 |
| 12 | T12 | V3.4b | Plane rendering via `PlaneRenderer` — no CPU quad parsing | Audit all textured-plane displays via `render::PlaneRenderer::drawLayer` (GPU `plane.vert/frag.glsl`), no `app/` CPU `PlaneGeometry` vertex parsing outside `render/`. | T1, T3 | §3 |
| 13 | T13 | Review | Ownership discipline — no raw owning-suspect pointers | Full inventory (scene structs, renderer ctor injection, pointer-keyed caches, sample member-order coupling); handle-first policy (`unique_ptr` sole owner / generational handles for RE / `shared_ptr`+`weak_ptr` only where cross-layer lifetime is real — atomic-refcount + zombie-resource pitfalls documented); lifetime notes on every remaining borrow; audit rule. | — | §6 |
| 14 | T14 | Review | Unified asset store — volumes/images/materials alongside meshes | Closes the mesh-only gap (`broker::AssetStore`, per-renderer pointer-keyed texture caches without invalidation) with one typed multi-kind store keyed `(AssetId,generation,contentHash)`; removes per-renderer texture maps; real material dedup replaces the `material=nullptr` placeholder. | T7 | §7, §10.7 |
| 15 | T15 | Review | Comment hygiene — self-contained rationale beside every tag | Sweep of bare `SPEC §N`/`T#`/decision-log citations found in review; mechanical floor rule or explicit allowlist. | — | §6, R9 |
| 16 | T16 | Review | GPU volume-plane extraction + interactive MPR 2D views | Fragment-shader sampling of cached `Texture3D` at the view's `ClipPlane`; `plane_sample` reworked to an extracted CT plane (was: gradient quad — neither mesh nor slice); MPR slices leave the frozen CPU path (`makeSliceImage` becomes test oracle). | T5, T14 | §3, FR-app.2 |
| 17 | T17 | Review | OIT sample — opaque + transparent meshes with depth overlap | ≥2 opaque + ≥2 transparent real meshes interleaved along the view direction (replaces three transparent quads); analytic composite probes; consumes T21 depth support. | T21 | FR-render.2/3 |
| 18 | T18 | Review | Broker = only app path; complete mapper inventory | Implement missing `PlaneMapper` (+ volume layer reality), delete Noop renderables from `ViewSynchronizer`, route all samples through `IViewBridge` (today broker has zero app consumers). | T3, T16 | §3, §11 |
| 19 | T19 | Review | Persistence honesty — activate scaffolding | `dirtyFieldsSince` computed from `dirtyLog_`; tombstones enforced in `resolve`; single shared `StableKey` (divergent twins unified); fake `parallelFor` + discarded dirty results removed; id-keyed camera cache. | T6 | §10.7 |
| 20 | T20 | Review | Renderer consolidation | One pass-prologue, one shared quad, one `geometryFor`, one content-hash definition (`data/content_hash.hpp`), merged `render()`/`drawLayer` bodies, `<glad/gl.h>` out of `render/`. Zero pixel drift. | T5 | §3 |
| 21 | T21 | Review | Depth-buffer support — optional attachment + per-view flag | Color-only stays default (analytic gates); opt-in depth target for true occlusion (near-mesh-wins gate); feeds OIT-with-opaque-meshes and unblocks removing the MPR box face-ordering hack. | T5 | §3 |
| 22 | T22 | Review | Error-model hardening | Domain-tagged error codes (loader ranges collide inside the single `int code`); debug-trap on failed `Result` dereference; dead `hasValue()` removed; monadic helpers optional stretch. | — | §6 |
| 23 | T23 | Review | Sample harness resize handling | Framebuffer-size callback + optional `ISample::onResize`; all samples derive aspect from live dims (today only MPR does; others distort on resize). | — | FR-app.1 |

### 9.1.1 Deferred (recorded, intentionally not scheduled — Sr. review)

Industry-standard capabilities deliberately kept out of the current loop so
the redesign lands before breadth; recorded here so they are visible EOL
items, not accidents:

- **Render-graph pass scheduling/ordering** (Frostbite FrameGraph /
  Filament / UE RDG pattern): automatic pass ordering + transient resource
  aliasing from declared reads/writes. The `View` item list is painter's
  order today — sufficient at this scale; revisit when passes exceed a
  handful per view.
- **Async/staging uploads** (staging buffer → device-local copy): v1 is
  single-threaded, upload-at-load; fine for ≤128³ budgets.
- **Material shader-permutation system** (UE FMaterialShaderMap analog):
  Phong-only non-goal keeps one shader per technique; revisit with PBR.
- **Lights** (`ILight` hierarchy, per-view light lists): deferred with the
  material expansion (T8 note); fixed headlight until then.
- **Monadic `Result` combinators** (`map`/`and_then`): stretch within T22;
  nested-error-dance call sites are tolerable at current depth.