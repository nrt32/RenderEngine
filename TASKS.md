# TASKS — RenderEngine

Active backlog is the **pure-redesign V3 iteration** (no new FRs — OpenGL/C++/CMake and Mesh/Volume/Plane/Slice/OIT/MPR requirements are locked per user direction 2026-08-23; implementation only). The completed sequential loops are archived in `COMPLETED_TASKS.md`: `T1–T16` (V1, 0.1.0) + `V2-T1–V2-T8` (V2 multi-view/asset/platform/maintainability, 8 gates green — archived 2026-08-23, logs purged). See SPEC.md for FRs, §9 for the V2 archive / §9.1 for the V3 roadmap, §6 for guardrails, NAMING_CONVENTIONS.md for style.

## Generic rules (preamble, binding for every task)

- R2 Gate discipline: runner rebuilds + runs the FULL suite before each task on
  a clean tree. No new work while red.
- R3 Regression lock: tests from prior tasks are NEVER weakened.
- R4 Evidence rule: every test asserts an explainable constant (analytic value,
  committed golden corpus hit, invariant derived from project numbers). Never
  "non-empty / non-black / >0".
- R5 Stop-and-report: an infeasible requirement PAUSES and is reported; never
  silently reinterpreted or relaxed.
- R6 One branch (main), meaningful incremental commits, no secrets, no
  binaries outside allowed dirs.
- R7 Build hygiene: warnings-as-errors; build stays clean at every task end.
- R8 Review-before-commit: build clean → full suite green → independent review
  → review gate (findings addressed, green again) → RUNNER commit → push.
- R9 Documentation gate: docs are part of every deliverable; update exactly the
  files this task's documentation-map row lists.
- R10 Verification: after the final full-suite run, no further edits; for
  GPU/readback tests require N>=3 consecutive green runs before declaring green.
- R11 State files under `tools/logs/` are runner-owned; never touch.
- R12 Permission policy: allowlist only; never block on a prompt.
- R13 URL discipline: never cite a web page not fetched; pin URLs that matter.
- R14 Failure taxonomy: infra vs code vs state vs gate; escalate, don't retry
  forever.
- R15 Launch prerequisite (SPEC §8): the loop MUST be launched with
  `source tools/env.sh` first — it exports
  `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils tests"` and
  `LOOP_BUILD_TEST_CMD` into the runner's environment. The T1 gate test
  already in the suite (COMPLETED_TASKS.md T1, gate item 4, updated for V3) makes this
  enforceable: a forgotten source must FAIL the gate loudly, never silently
  audit the wrong dirs.
- Every gate also runs the mechanical audit (`tools/audit.sh`) with
  `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils tests"` (via
  `source tools/env.sh`).
- Unit tests build with ASan+UBSan; GL-touching tests use the offscreen GL
  fixture (headless). No exceptions in v1; typed `Result` errors (SPEC §5).
- Logging via spdlog only (no printf/cout). Doxygen on all public API.

> V2 clean: `V2-T1–V2-T8` (IRenderer, multi-view, AssetRegistry, utils/, per-OS backend, draw-cache, .glsl externalization, RE_GLSL_VERSION) were green and archived to `COMPLETED_TASKS.md` 2026-08-23; `tools/logs/` (`run_all.out`, `session_*.lease`, `task_*.{log,gate.log,pass,review}`) purged. Workspace is clean for the pure-redesign iteration.

---

## V3 backlog — pure-redesign `scene`/`broker`/View/List/Persistence overhaul (SPEC §10–§12)

Pure-redesign iteration (no new FRs). Priority order **redesign-first** per `open_questions.md:30 Q30` binding: `T1 scene` → `T2 CompositeKey/TranslateContext/DrawContext` skeletons → `T3 broker` (needs T1+T2) → `T4 Camera` → `T5 View` (needs T1+T2+T3) → `T6 persistence full` (needs T5+T2) → `T7 AssetId` → `T8 Phong` → `T9 RE-minimal` (needs T1+T5+T7+T8) → `T10 stretch` (needs —). Each task is **one session, one reason to change** (SRP) and maps to `SPEC §9.1` V3.x. Accepted standard `T1..Tn` per iteration — V2 `V2-T1..V2-T8` archived, V3 now `T1..T10` (not `T9..T18`). **All 13 ★ `SPEC §13` open questions resolved binding 2026-08-23 Sr. Principal review (Q3/Q9/Q27/Q28/Q32f/Q39-Q47) — `open_questions.md:11` header; no ★ blocks V3 kickoff.**

## V3 documentation map (T-map, R9) — to be filled per task

| Task | Spec alias | Docs updated in the same commit |
|---|---|---|
| T1 | V3.1 | `docs/spec/modules.md` (`scene/`), `scene/` CMake target, `SPEC.md` at-a-glance modules |
| T2 | V3.2a | `docs/spec/persistence.md` (§10.1 composite key), `docs/spec/broker.md` (§11.4 `TranslateContext`), `docs/core.md` (`DrawContext`), `core/draw.hpp` |
| T3 | V3.2b | `docs/spec/broker.md` (§11 `IMapper`/`ICachedMapper`/`Broker`/`IViewBridge`), `broker/` README, `NAMING_CONVENTIONS.md` (Mapper/Broker/ViewBridge) |
| T4 | V3.3 | `docs/spec/modules.md` (§3.1 `Camera`), `scene/camera.hpp` (Doxygen) + `docs/render.md` (`Camera` → view matrix) |
| T5 | V3.4 | `docs/render.md` (`ReView`/`IRenderable`/`ViewTarget`), `docs/spec/modules.md` (§3.2) — deletes `ViewRenderer` |
| T6 | V3.5 | `docs/spec/persistence.md` (§10 full), `docs/spec/modules.md` (`Layout::resolve`) |
| T7 | V3.6 | `docs/spec/assets.md` (addendum) + `data/README.md` (always) |
| T8 | V3.7 | `docs/spec/materials_lights.md` (§12.2/§12.3 deferred note — Phong-only stays), `docs/render.md` (RE-minimal `Re*` note) |
| T9 | V3.8 | `docs/re_scene_inventory.md` (binding inventory — 6 tables/23 fields) + `render/re_scene/mesh_object.hpp` reference, `tools/audit.rules` (`asset_indirection` active) |
| T10 | V3.9 | `docs/spec/nfr.md` (stretch tags) + `docs/spec/modules.md` (EOL skeletons deferred — stretch, no code) |
| T11 | V3.8b | `render/contour_renderer.*` + `render/shaders/contour.geom.glsl` (`ContourRenderer` GPU), `broker/contour_mapper.*`, `docs/render.md` (contour GPU), `app/mpr_contour.hpp` deleted |
| T12 | V3.4b | `render/plane_renderer.hpp` (audit plane via `PlaneRenderer`), `docs/render.md` (plane GPU), `app/` CPU quad parsing removed |

> **Naming:** active backlog is `T1..T10` for this iteration (resets after archive — `V2-T1..V2-T8` remain in `COMPLETED_TASKS.md`). `V3.x` survives only as Spec alias in `docs/spec/roadmap.md` §9.1 and parentheses below.

---

## T1: `scene/` library — GL/RE-free app-side scene (SPEC §3, §12 — V3.1)

**D** — Extract every app-authored type into the owning `scene/` value library `re::scene` (`STATIC`): `View{rect,plane,itemIds,gen}`, `Camera{pan/rotate/zoom/orbit → viewMatrix(), projGen/viewGen}`, `PlaneDesc{normal,point,Space::World|VoxelIndex}`, `SceneObject` family (`MeshObject`, `MeshSliceObject`, `VolumeObject`, `VolumeSliceObject`, `PlaneObject`) `= {AssetRef, transform, presentation}`, plus `SceneStore`/`ViewStore` stable handles + per-field `generation`. No `App` prefix (`re::scene::MeshObject`) — namespace is prefix. `scene/` links to `data/`+`volume/`+`glm` only; `RE` keeps only translated `Re*` types (§3.1). Pure value semantics — copyable, no GL `Handle`, `core` never included.

**FR:** regression — preserves `FR-io.*`/`FR-data.*`/`FR-vol.*`/`FR-core.*`/`FR-render.*`/`FR-app.*` (no new FR, V1+V2 gates remain green; pure value lib, no GL).

**T** — suite green + audit green: `re::scene` target builds; `scene/Camera` pan/rotate/zoom/orbit produce analytic `viewMatrix` (lookAt) within 1e-6; `SceneStore` add/remove preserves `generation` bump; no `render/` include in `scene/` headers (`disposition_scene`).

**G** — suite green, audit green, `scene/` target in `CMakeLists.txt`, `SPEC.md` module list updated.

## T2: `CompositeKey` + `TranslateContext` + `DrawContext` skeletons (SPEC §10.1, §10.4, §11.4 — V3.2a)

**D** — Land the cross-cutting skeletons **before any cached mapper**: `CompositeKey{Version,LayoutId,Id,Gen,Hash}` type (hash of stable bytes, not pointer), `TranslateContext{ViewContext{viewPlane,viewMatrix,projMatrix}, optional<VolumeContext{volumeModel,dims,voxelSpacing,meshBounds}>}` ISP-segregated (Q40:B — not flat `viewPlane+view+volumeModel` fat, not God `ReView*`), and `DrawContext{Viewport,ClearColor,Depth,Blend,spy}` instance per `FrameContext` replacing `core/draw.cpp` static `invalidateDrawCache()` global (SRP via instance — Q43:B). All three are value types, header-only, no behavior change yet — they unblock `T3`/`T5`/`T6`.

**FR:** none new — skeleton unblocks `FR-render.*`/`FR-app.2` via `TranslateContext`/`DrawContext`; regression `FR-core.1` (offscreen context still GL 4.6 core).

**T** — suite green + audit green: `CompositeKey` equality/hash stable; `TranslateContext` with null `viewPlane` valid for 3D (LSP — `hasPlane()`); `DrawContext` per-frame `setViewport(cached)` spy shows exactly 1 `glViewport` for duplicate call (replaces global `invalidateDrawCache()` — N>=1).

**G** — suite green, audit green, skeletons in `scene/`/`core/` only, no `render/` edits.

## T3: `broker/` library — per-type `IMapper`/`ICachedMapper` + `Broker` + `IViewBridge` SRP-split (SPEC §11 — V3.2b)

**D** — Heavily abstracted `broker/` `STATIC` (peer to `scene/`/`render/`): `IMapper<AppT,ReT>{map(Ctx)}` pure vs `ICachedMapper:IMapper{mapCached,invalidate}` ISP-split, `Broker{registerMapper<T>(unique_ptr<IMapper<T>>), get<T>()}` keyed by `std::type_index` (OCP — no `enum` switch), `IViewBridge{sync,renderAll,presentAll}` façade composing `ViewSynchronizer` (cache/dirty) + `ViewCompositor` (dispatch/present) SRP-split. One file per mapper (`camera_mapper.*`, … , `view_bridge.*`). App never holds `IMapper`; only `IViewBridge` (DIP).

**FR:** regression — `FR-render.*`/`FR-app.*` still green via `Broker` forwarding (no new pixels); `FR-core.1` (no `gl*` in `broker/`).

**T** — suite green + audit green: `Broker` empty→`register(MeshObjectMapper)`→`get<MeshObjectMapper>()` returns same `type_index` id, second `get` same address, `V2` renderers still green via forwarding (center pixel within 1/255); same `data::Mesh` pointer twice via `Broker` still dedups to one GL object when later `AssetId` path lands; `disposition_scene`/`disposition_render` + `broker_per_type` (exactly one `class *Mapper` per file, `ViewBridge` is coordinator not mapper) + `gpu_api_ownership` (no `gl*` in `broker/`); stale `generation+1` lookup via future `AssetStore` returns typed `Error::StaleHandle` code 2 (never crash).

**G** — suite green, audit green, `broker/` target `STATIC`, `tools/env.sh` `AUDIT_SOURCE_DIRS` already includes `broker`.

## T4: `app::Camera` manipulable (`pan/rotate/zoom/orbit`) → view matrix to RE (SPEC §3.1 — V3.3)

**D** — Move `pan/rotate/zoom/orbit` + factories `makeOrthoForSlice` / `makePerspectiveCrosshair` into `scene::Camera` (`scene/camera.hpp`). Scene sends only `viewMatrix()` (+`projMatrix()`, `pos`) via `CameraMapper → render::Camera{view,proj,pos}`. `2D` ortho vs `3D` perspective validated by mapper (plane present → ortho). Per-field `viewGen`/`projGen` (see `T2` `DrawContext` split — camera orbit dirties only `viewGen`).

**FR:** regression — `FR-app.2`/`FR-app.3` camera interplay; `Camera` `viewMatrix` analytic within 1e-6 preserves existing viewport/slice constants.

**T** — suite green + audit green: orbit 90° yields analytic `viewMatrix` within 1e-6; `2D` plane+camera combo produces ortho `proj` deterministic, `3D` produces perspective `proj`; no `render/` type leaking into `scene/`.

**G** — suite green, audit green.

## T5: `View` per screen section + heterogeneous item list — delete `ViewRenderer` (SPEC §3.2, §11 — V3.4)

**D** — `render::View` (`ReView`) per screen section owns one `ViewTarget{Texture2D+Framebuffer}` per `ViewRect` (`rect.w×h`) + `Camera` + `optional<ClipPlane>` (`2D` vs `3D`) + `list<IRenderable>` (`VolumeSlice+MeshSlice` for `2D`, `Volume+Mesh` for `3D`). Each `IRenderable` is type-erased `drawLayer(SceneT,Camera,DrawContext&)` — `View` never knows renderer. Each renderer gains `drawLayer(..., DrawContext&)` assuming `ReView` already `bind+viewport+clear`; single-item `render()` keeps `clear` for direct tests. Delete `ViewRenderer` + `render/types.hpp` `Scene` raw-pointer variant (replaced by `AssetId` handles from `T7`). Evolves in two incremental commits inside the session: (1) `ReView`/`ViewTarget`/`IRenderable` skeleton + `core::blit` wiring, green on `gl*` ownership; (2) migrate `Mesh/Plane/Volume/SliceRenderer` to `drawLayer` + delete `ViewRenderer` — mid-gate `ctest` after (1).

**FR:** `FR-render.*` + `FR-app.2` (2-view blit 1280×480, `ViewRect` constants, center pixel within 1/255, N>=3).

**T** — suite green (N>=3 for blit): 2-view `1280×480` window — `View` A `(0,0,640,480)` / B `(640,0,640,480)` each FBO center `320,240` matches scene color within 1/255; window pixels `320,240` / `960,240` match after `core::blit` (same gate as `V2-T2`, now via `ReView`/`ViewTarget`).

**G** — suite green (N>=3), audit green, `ViewRenderer` deleted, `utils::PixelReader` path unchanged.

## T6: Persistence & layout/page lifetime — `CompositeKey` full (SPEC §10 — V3.5)

**D** — Full content-addressed persistence: `CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash}` (not `id+size` dump). `ReView`/`Re*Object`/`ViewTarget` persist across `sync()` — `Camera::rotate` dirties only `CameraMapper` (per-field `viewGen`), `2D→3D` toggle same `ViewId` (`plane some→nullopt`, `itemIds` swap) rebinds `plane+items` without `ReView` map churn, size resize recreates only `ViewTarget` inner `FBO`, layout count/set change inserts/erases `ReView`s. Hybrid `storeGen` poll early-out + bounded `dirtyFieldsSince()` scan + `markDirty()` push opt-in via `IDirtyTracker` (collaborator from `T2`). `LayoutSpec{row,col,span,weight}` relative → `Layout::resolve(windowSize,dpr)` absolute `Rect` (see `T2` `TranslateContext`).

**FR:** `FR-render.*`/`FR-app.2` persistence — same pixels after `rotate`/`toggle`/`resize` (see `T` for identity checks).

**T** — suite green (N>=3): `Camera::rotate(1°)` keeps `&ReView` identity (`EXPECT_EQ(&after, &before)`), `viewMatrix` delta is analytic `rotateY(1°)` within 1e-6; `2D→3D` toggle (`plane some→nullopt`, `itemIds` swap) keeps `&ReView` identity, no map churn (`Broker::mapCached` hit, `AssetRegistry` not touched); `glfwSetWindowSize(1280,960 → 800,600)` keeps `&ReView` identity, only `ViewTarget` inner `Framebuffer` id changed (size hash includes physical pixels `framebufferSize` + `contentScale`); `LayoutSpec::resolve` relative `Layout` → absolute `Rect` within 1 px; hybrid `storeGen` poll + `dirtyFieldsSince()` bounded scan + `markDirty()` push opt-in all exercised.

**G** — suite green (N>=3), audit green, `no_dump_sync` (`recreateAll`/`dumpAll` forbid) green.

## T7: Data asset persistence — `SceneStore`-owned `AssetId` (SPEC §7, §12 — V3.6)

**D** — Pure-redesign asset identity: keep current `render::AssetRegistry::Slot{MeshGeometry}` generational `AssetHandle` but key by stable `AssetId` from `scene::SceneStore` (not `byObject_` pointer `render/asset_registry.hpp:137`). `SceneStore` owns `AssetId{generation,contentHash}` per `data::Mesh`/`VolumeDataset`/`Image` (hash of stable bytes, not pointer). `data::Mesh` stays pure — no `AssetId` field (preserves `data` RE-agnostic for physics/UI — see redesign Q&A). Typed store is extensible via `AssetRegistry<T>` template, no per-kind duplicate.

**FR:** `FR-data.*`/`FR-io.*` asset identity — same `data::Mesh` dedup preserves `FR-io.1` vertex counts and `FR-data.1` normals.

**T** — suite green + audit green: same `data::Mesh` added twice via `SceneStore` dedups to one `AssetId` + one `AssetHandle`; second `SceneStore` entry with identical bytes but distinct `Mesh` pointer dedups to same `AssetId` (content-hash path); stale `AssetId{gen+1}` → typed error, not crash.

**G** — suite green, audit green, no `data::Mesh` copy into `render/re_scene/` (`asset_indirection`).

## T8: Even hierarchy note — Phong-only stays (SPEC §12 — V3.7 deferred)

**D** — **Pure redesign: no hierarchy expansion this iteration.** Keep `render::IMaterial→PhongMaterial` single path (FR non-goal `SPEC §1` — PBR deferred) and no `ILight` (fixed headlight `max(dot(n,(0,0,1)),0)` in `MeshRenderer` stays). Note even `IMaterial`/`ILight` hierarchies as deferred (§12.2 `IColor/IVolume/ILineMaterial` + `PBR`/`SliceMaterial`/`ContourMaterial`, §12.3 `Directional/Point/Spot`) — headers not added this iteration; `MaterialDesc`/`LightDesc` remain `app`-local free structs for `MPR` sample. This task only tightens the `TransferFunction` vs `VolumeMaterial` boundary (TF stays beside `VolumeMaterial` in `VolumePresentation` — already decided §12.5).

**FR:** `FR-render.*` Phong-only — `isTransparent ⇔ baseColor.a<1` preserves `FR-render.2/3` transparency gates.

**T** — suite green + audit green: `PhongMaterial isTransparent ⇔ baseColor.a<1` unchanged; `VolumeRenderer` still takes `TransferFunction*` separately (no regression).

**G** — suite green, audit green, no new `render/material/` files this iteration.

## T9: RE-minimal types — `render/re_scene/` inventory (SPEC §12.4 — V3.8)

**D** — Audit every field crossing `scene→render`: `Re*` keeps only `Re`-direct values (`AssetHandle`/`ReMaterial*`/`ClipPlane`/`ReLight[]`/`worldBounds`/`sliceUVW` where derived), never verbatim `app::MaterialDesc`. Produce the binding inventory `docs/re_scene_inventory.md` before any `render/re_scene/*.hpp` lands (per-field question-mark table §12.4). This iteration only **documents** the inventory and moves `ReMeshObject{AssetHandle,model,bounds,ReMaterial*}` to `render/re_scene/` as reference — no `Volume/Contour` expansion (deferred with `T8`).

**FR:** `FR-render.*` RE-minimal — inventory documents `FR-render.*` derived fields; no pixel change.

**T** — suite green + audit green: `docs/re_scene_inventory.md` exists with 6 tables (ReMeshObject, ReVolumeObject, RePlaneObject, ReView, ReScene, AssetHandle) / 23 fields, each row rationale ∈ {`derived`|`uniform-ready`|`handle`}; `grep -R "data::Mesh::positions" render/re_scene/` → 0 hits (`asset_indirection`); `ReMeshObject` reference header `render/re_scene/mesh_object.hpp` exposes `AssetHandle`+`model`+`bounds`+`ReMaterial*` only, never verbatim `app::MaterialDesc`.

**G** — suite green, audit green, `docs/re_scene_inventory.md` committed.

## T10: EOL skeletons — deferred stretch (SPEC §3, §11.6, §13.8 — V3.9)

**D** — **(stretch) — deferred.** `RHI` (`core/rhi/IRHIContext`), `IJobExecutor`, serialisation `Version` migration are **not** landed this iteration (EOL hardening, not redesign). Only the extension points stay: `DrawContext` instance (T2) + `IDirtyTracker` interface + `CompositeKey::Version` field (no `Vulkan` impl, no thread pool, no file format). This task is a placeholder `(stretch)` that stays **red** until stretch is activated — not required for looping.

**FR:** none — stretch (`(stretch)`), `FR-*` deferred; `core|` anchor stays.

**T** — (stretch) suite green + audit green when activated: `core/rhi/` still absent, `core/` remains sole `gl*` owner `audit.rules:gpu_api_ownership` (`core|\bgl[A-Z]`), `require_only` not yet enforced; `IJobExecutor` inline fallback remains synchronous.

**G** — (stretch) audit green, `rhi_ownership` / `IJobExecutor` not yet enforced.

## T11: GPU mesh contour — `ContourRenderer` via geometry shader (SPEC §3, FR-app.3 — V3.8b)

**D** — **GPU contour, not CPU `mpr_contour`.** Replace `app/mpr_contour.{hpp,cpp}` CPU `meshPlaneContour` (triangle-plane edge test on `data::Mesh::positions`) + `overlayContour` CPU rasterization with GPU `render::ContourRenderer` (or `SliceRenderer` contour mode) that computes the `plane∩mesh` outline **on GPU** via a geometry shader (clip pattern `slice_clip.geom.glsl` → emit line strip). New `ContourObject{AssetHandle, ClipPlane}` via `ContourMapper` + `ContourRenderer::drawLayer(ContourObject, Camera, DrawContext&)` through `broker/` (`ContourMapper : IMapper<scene::ContourObject, render::ContourObject>`). `Re*` keeps `AssetHandle` only (RE-minimal). `app/mpr_contour.hpp` deleted — MPR contour overlay now comes from `ReView`'s `ContourObject` rendered by `ViewCompositor`, not from CPU `overlayContour` image copy. `SliceRenderer` cross-section capture (`captureCrossSection` + `TransformFeedback` for `FR-render.4`) stays for sliced-mesh fill; contour is its outline-only peer.

**FR:** `FR-app.3` — same ≥90% within 2 px of analytic box curve, now verified via GPU `ContourRenderer` readback (`core::readRgba8` via `utils::PixelReader`, `core/rhi/gl` only) within 1/255, `N>=3`.

**T** — suite green (N>=3, contour): golden box `plane∩mesh` contour via `ContourRenderer` matches analytic rectangle boundary (same 8 triangle-segments, 4 edges) within 1/255; `app/mpr_contour.cpp` gone (`grep "meshPlaneContour|overlayContour" app/ → 0 hits`); `render::ContourRenderer` geometry shader `contour.geom.glsl` compiled (`RE_GLSL_VERSION` 450); `SliceRenderer` slice tests still green (no regression).

**G** — suite green (N>=3), audit green, `mpr_contour` CPU remnants removed, `asset_indirection` still 0 hits.

## T12: Plane rendering via `PlaneRenderer` — no CPU quad parsing (SPEC §3, FR-render.5 — V3.4b)

**D** — **Audit plane path uses `PlaneRenderer`, not CPU-parsed quad.** All textured-plane displays (`plane_sample`, MPR `PlaneObject` via `View`'s `IRenderable` list) go through `render::PlaneRenderer::drawLayer(PlaneScene, Camera, DrawContext&)` (GPU `.glsl` `plane.vert/frag.glsl`, `core::blit` present). No `app/` CPU parsing of `PlaneGeometry` corners/UVs into vertex buffers outside `PlaneRenderer` — `PlaneGeometry::unitQuadXY()` stays in `render/`, `app` sends only `PlaneDesc{AssetRef, transform, presentation}` via `PlaneMapper`. `data::Image → Texture2D` upload stays in `PlaneRenderer::textureFor` (GPU); `imageToRgba8` CPU row-flip is internal to renderer, not app-parsed quad. Remove any `app/` CPU quad vertex generation that bypasses `PlaneRenderer`.

**FR:** `FR-render.5` — textured quad corner/center pixel matches texture sample within 1/255 via `PlaneRenderer` (same gate as `V2` plane, now via `ReView`/`Broker`).

**T** — suite green (N>=3, plane): `PlaneRenderer` quad center pixel matches source gradient within 1/255 via `utils::PixelReader`; `grep -R "PlaneGeometry" app/ --include="*.cpp" --include="*.hpp" | grep -v "mpr_contour"` → only `PlaneDesc`/`PlaneObject` (no quad vertex parsing); `PlaneRenderer` still owns `planeProgram_` + `quadGeometry` (`RE_GLSL_VERSION` 450); MPR `PlaneObject` via `PlaneMapper` still green.

**G** — suite green (N>=3), audit green, no CPU quad parsing outside `render/`.

## Definition of Done (end-of-loop evidence, finalized at V2-T8 — archived)

- [x] All 8 V2 task gates green (see `COMPLETED_TASKS.md` V2 `V2-T1..V2-T8`); full suite green on a clean tree at the last V2 task.
- [x] GPU/readback tests (V2-T2, V2-T6) verified with **N>=3 consecutive green runs** (archived — `tools/logs/` purged 2026-08-23).
- [x] Mechanical audit green (`tools/audit.sh`) with `AUDIT_SOURCE_DIRS="io data volume core render app utils tests"` plus `scene`/`broker`/`utils` as landed.
- [x] ASan+UBSan clean on all test binaries (no leaks, no UB).
- [x] Documentation map complete: `docs/render.md`, `docs/core.md`, `AGENTS.md`, `docs/spec/env.md`, `tools/env.sh`, `TASKS.md` preamble — exactly as listed per task.
- [x] Sample smoke set (mesh/plane/volume/slice/oit/mpr) still green.

## Definition of Done (end-of-loop evidence, finalized at T12 — pure-redesign; T10 stretch)

V2 DoD above **plus** (checked at `T12` — `T10` is stretch/deferred):

- [ ] `scene/` value library `re::scene` `STATIC` exists with `camera.hpp`/`view.hpp`/`object.hpp` + `SceneStore` (`AssetId` per `T7`) — no `App` prefix, `scene/` links to `data/`+`volume/`+`glm` only (§3.1, `disposition_scene`).
- [ ] `broker/` `STATIC` exists with `IMapper`/`ICachedMapper` ISP-split + `Broker` `type_index` registry (OCP, no `enum` switch) + `IViewBridge` composing `ViewSynchronizer`+`ViewCompositor` (SRP) — one file per mapper, no god `Translator` (§11.2.1, `broker_per_type`).
- [ ] `DrawContext` instance replaces `core/draw.cpp` global cache — `DrawContext{Viewport,ClearColor,Depth,Blend}` per `FrameContext`, spy per context (SRP, test determinism — see `T2`).
- [ ] `render::View` (`ReView`) per screen section + heterogeneous `list<IRenderable>` (`VolumeSlice+MeshSlice` for `2D`, `Volume+Mesh` for `3D`) — `ViewRenderer` deleted; `ReView` holds `ViewTarget{Texture2D+Framebuffer}` + `vector<IRenderable>` (`drawLayer`) (see `T5`, §3.2).
- [ ] `scene::Camera` (`pan/rotate/zoom/orbit`) sends only `viewMatrix()`(+`proj`+`pos`) via `CameraMapper` to `render::Camera` with per-field `viewGen`/`projGen` (see `T4`, §3.1).
- [ ] Persistence by `CompositeKey{Version,LayoutId,Id,Gen,Hash}` — `Camera::rotate` dirties only `CameraMapper`, `2D→3D` toggle same `ViewId` keeps `ReView` identity, size resize recreates only `ViewTarget` inner `FBO` (see `T6`, §10.3/10.4).
- [ ] `SceneStore`-owned `AssetId{generation,contentHash}` via `AssetRegistry<T>` template — same `data::Mesh` deduped to one `AssetId`+one `Handle`, content-hash alias, `data::Mesh` stays pure (see `T7`, §7 addendum).
- [ ] RE-minimal `render/re_scene/` inventory `docs/re_scene_inventory.md` exists — every `Re*` field rationale `derived|uniform-ready|handle`, no verbatim `app::MaterialDesc` copy (`asset_indirection`).
- [ ] `T8` note: `IMaterial→Phong` single path kept (PBR/`Slice`/`Contour` + `ILight` deferred — Phong-only non-goal §1) — `TransferFunction` stays beside `VolumeMaterial` (see `T8`, §12.5).
- [ ] `render::ContourRenderer` GPU contour (`contour.geom.glsl`) via `ContourMapper` — `app/mpr_contour` CPU `meshPlaneContour`/`overlayContour` gone, `FR-app.3` 2 px band still green via GPU readback (see `T11`, §3, geometry shader)
- [ ] `render::PlaneRenderer` owns all textured-plane draws — no `app/` CPU `PlaneGeometry` quad parsing outside `render/` (see `T12`, §3, `FR-render.5`)
- [ ] Full audit green including `disposition_scene`, `disposition_render`, `broker_per_type`, `isp_mapper_forbid`, `no_dump_sync`, `asset_indirection`, `broker_app_reach` (plus `scene`/`broker`/`utils` in `AUDIT_SOURCE_DIRS` `io data volume scene core broker render app utils tests`). `rhi_ownership`/`require_only` remain stretch (`T10` deferred — `core|` anchor stays; `core/rhi/gl|` lands with V3.2a skeleton).
- [ ] `ASan+UBSan` clean on all test binaries (no leaks, no UB, `GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6`).
- [ ] `GPU/readback` gates (`T5` 1280×480 blit, `T6` persistence) verified `N>=3` consecutive green (`tools/logs/*.gate.log` records).
- [ ] `5+1` sample smoke (`mesh/plane/volume/slice/oit/mpr`) still green on `GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6` (portable 450 floor).