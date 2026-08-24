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
| T13 | — | `docs/spec/guardrails.md` (ownership ladder), `NAMING_CONVENTIONS.md` (§8b borrow notation), `tools/audit.rules` (`ownership_raw_ptr_*`), `docs/render.md`/`docs/spec/materials_lights.md`/`docs/spec/assets.md`/`docs/re_scene_inventory.md` (ownership-model sync), `render/re_scene/mesh_object.hpp` (shared material) |
| T14 | — | `docs/spec/assets.md` (§7 unified multi-kind store record), `docs/render.md` (asset-store section + Plane/Volume renderer texture paths + guardrails), `broker/README.md` (asset_store ↔ render store split) |
| T15 | — | `tools/audit.sh` (`comment_context` mode), `tools/audit.rules` (`comment_tag_context` bare-tag rule), `tools/audit.rules.example`, comment-hygiene sweep across all modules (self-contained rationale beside every tag) |

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

**USER-VERIFIED DEFECT (2026-08-24, binding gate item):** after the GPU migration the interactive **MPR sample shows NO contour at all** on any slice view (previously visible via the CPU overlay). The readback test passing while the live sample shows nothing means the sample wiring path differs from the test path — root-cause and fix it. Checklist to verify end-to-end in `app/mpr_sample.cpp`: (1) each slice view's scene actually carries a `ContourObject` whose `AssetHandle` resolves through the registry and whose plane matches that view's `slicePlane` (axis + voxel-center coordinate); (2) the view composition actually calls `ContourRenderer::drawLayer` for those items (not just tests driving `render()` directly); (3) draw state on the shared window/default framebuffer path — depth test off, blending state, and the geom-shader thick-line quad's screen-space expansion using the *actual* viewport size (not a cached/stale `DrawContext::viewportRect`, e.g. window FB vs FBO mismatch or HiDPI framebuffer-size vs window-size); (4) plane space (`VoxelIndex`) conversion errors surface as typed errors that the sample must not silently swallow (a skipped layer looks exactly like "no contour"); (5) run `tools/run_sample.sh mpr` interactively and confirm red outlines are visible on all three slice views before declaring green.

**G** — suite green (N>=3), audit green, `mpr_contour` CPU remnants removed, `asset_indirection` still 0 hits, **MPR sample interactively shows the red contour overlay on T/C/S views (user-verified)**.

## T12: Plane rendering via `PlaneRenderer` — no CPU quad parsing (SPEC §3, FR-render.5 — V3.4b)

**D** — **Audit plane path uses `PlaneRenderer`, not CPU-parsed quad.** All textured-plane displays (`plane_sample`, MPR `PlaneObject` via `View`'s `IRenderable` list) go through `render::PlaneRenderer::drawLayer(PlaneScene, Camera, DrawContext&)` (GPU `.glsl` `plane.vert/frag.glsl`, `core::blit` present). No `app/` CPU parsing of `PlaneGeometry` corners/UVs into vertex buffers outside `PlaneRenderer` — `PlaneGeometry::unitQuadXY()` stays in `render/`, `app` sends only `PlaneDesc{AssetRef, transform, presentation}` via `PlaneMapper`. `data::Image → Texture2D` upload stays in `PlaneRenderer::textureFor` (GPU); `imageToRgba8` CPU row-flip is internal to renderer, not app-parsed quad. Remove any `app/` CPU quad vertex generation that bypasses `PlaneRenderer`.

**FR:** `FR-render.5` — textured quad corner/center pixel matches texture sample within 1/255 via `PlaneRenderer` (same gate as `V2` plane, now via `ReView`/`Broker`).

**T** — suite green (N>=3, plane): `PlaneRenderer` quad center pixel matches source gradient within 1/255 via `utils::PixelReader`; `grep -R "PlaneGeometry" app/ --include="*.cpp" --include="*.hpp" | grep -v "mpr_contour"` → only `PlaneDesc`/`PlaneObject` (no quad vertex parsing); `PlaneRenderer` still owns `planeProgram_` + `quadGeometry` (`RE_GLSL_VERSION` 450); MPR `PlaneObject` via `PlaneMapper` still green.

**G** — suite green (N>=3), audit green, no CPU quad parsing outside `render/`.

## T13: Ownership discipline — eliminate raw owning-suspect pointers (user mandate)

**D** — **No raw pointers where ownership/lifetime matters; use `unique_ptr` (sole owner), `shared_ptr` (shared owner), `std::weak_ptr` (observer), or a generational handle instead.** Non-owning *borrow* pointers with provable scope-bounded lifetimes are allowed only where the borrow is structurally guaranteed (e.g., a renderer borrowing its own `optional<>` member for the duration of one call). Inventory from the architecture review (2026-08-23 session, all verified file:line):
  - `render/`: `PlaneInstance{const PlaneGeometry*, const data::Image*}` (`plane_renderer.hpp:79-80`), `VolumeInstance{const data::VolumeDataset*, const volume::TransferFunction*}` (`volume_renderer.hpp:60-61`), `MeshInstance::material const IMaterial*` (`mesh_renderer.hpp:45`), `MeshRenderer/SliceRenderer(AssetRegistry* registry_, ITransparencyPipeline* transparency_)` ctor injection (`mesh_renderer.hpp:120-121`, `slice_renderer.hpp:142`), `RenderTarget::framebuffer core::Framebuffer*` (`types.hpp:54`), `Scene = variant<const MeshScene*, …>` (`types.hpp:76-77`), cache map keys `unordered_map<const data::Image*/VolumeDataset*, Texture>` (`plane_renderer.hpp:146`, `volume_renderer.hpp:139`).
  - `scene/`: `MeshObject/MeshSliceObject::mesh const data::Mesh*`, `VolumeObject/VolumeSliceObject::volume const data::VolumeDataset*`, `PlaneObject::image const data::Image*` (`object.hpp:27,46,64,84,104`), `scene::AssetRegistry<T>` stores caller-owned `const T* object` slots + `byObject_` pointer map (`asset_registry.hpp:66-73,155,163`) — registry does NOT own assets. (The future `ContourObject` from T11 will add one more borrowed `const data::Mesh*` — include it in this sweep when T11 lands.)
  - `broker/`: `Broker::get()` returns raw `IMapper*` aliases over owned `unique_ptr`s (acceptable if documented as non-owning view; make it explicit), `IDirtyTracker::store_` raw + `const_cast` smell (`idirty_tracker.hpp:79-101`), `ViewSynchronizer`/`ViewCompositor` mutual raw back-pointers (`view_synchronizer.hpp:92-93`, `view_compositor.hpp:80`).
  - Construction-order coupling hazard: every sample declares `registry_` before `renderer_{&registry_}` with an explicit comment (`mesh_sample.cpp:115-117`, `oit_sample.cpp:142-144`, `mpr_sample.cpp:308-310`) — reordering members silently breaks init; replace injection-of-raw with shared ownership or two-phase init that validates.
  Policy decision to record first: renderers/GPU resources should follow the **handle-based model** (generational handles into pools — matches `AssetHandle`) rather than `shared_ptr` everywhere; atomic refcount churn per frame and post-teardown zombie resources are documented industry pitfalls. `shared_ptr` reserved for genuinely shared ownership across layers (e.g., assets owned by `SceneStore`, borrowed by broker+RE via `weak_ptr`/handle); `unique_ptr` for sole owners (`Broker` mappers, `ReView` items — already correct).

**T** — audit-enforceable floor: extend `tools/audit.rules` with an ownership rule (e.g., forbid public API/members of pattern `(const )?[A-Z][A-Za-z_]*\*\s*[a-z]` in scene//broker//app/ headers outside an allowlist of documented borrows); suite green; every remaining raw pointer carries a Doxygen `@note lifetime:` tag naming its owner; reorder-hazard sample member groups converted to explicit two-phase init or shared ownership.

**G** — suite green, audit green (new rule enforced), zero undocumented raw pointers in `scene/ broker/ app/` public APIs.

## T14: Unified asset store — volumes/images/materials alongside meshes (answers "why mesh-only?")

**D** — **Answer recorded:** the asset store caches only meshes for **historical, not principled, reasons**. The V2 asset registry was built specifically to fix the `MeshRenderer`+`SliceRenderer` double-upload of the same `data::Mesh`; when `VolumeRenderer` landed it brought its own private `textureFor()` cache inside the renderer instance (`volume_renderer.hpp:139` — `unordered_map<const data::VolumeDataset*, core::Texture3D>` keyed by raw CPU pointer), and it was never migrated into the registry. Consequences today: (1) two `VolumeRenderer` instances double-upload the same dataset — exactly the problem the registry solved for meshes; (2) identical-content datasets are not deduped (pointer-keyed, unlike the mesh path's content hash); (3) no invalidation — freeing/mutating a dataset dangles the cache key and serves stale GPU data; (4) `broker::AssetStore` (`asset_store.hpp:66,79,86-87`) repeats the same mesh-only shape even though `scene::computeContentHash` already has unused `VolumeDataset`/`Image` overloads (`scene/asset_id.hpp:125,148`). Fix: generalize `scene::AssetRegistry<T>` / `broker::AssetStore` to a typed multi-kind store covering `data::Mesh → MeshGeometry`, `data::VolumeDataset → core::Texture3D`, `data::Image → core::Texture2D`, keyed by `(AssetId, generation, contentHash)` with reference counting and invalidation; move `VolumeRenderer::textureFor`/`PlaneRenderer::textureFor` onto the shared store; delete per-renderer pointer-keyed maps.

**FR:** `FR-render.6` unchanged (ray-cast center pixel); new invariant — registering the same `data::VolumeDataset` through two `VolumeRenderer` instances yields one GPU `Texture3D`.

**T** — gate asserts (explainable): same dataset registered twice (via two renderer instances) → one `Texture3D` GL id; identical-content distinct-pointer datasets dedup by content hash; stale handle after unregister → typed error, no crash; plane/volume/MPR samples still green (readback N>=3).

**G** — suite green (N>=3), audit green; per-renderer texture caches removed (`grep -n "textures_" render/*_renderer.*` only in the shared store).

## T15: Comment hygiene — self-contained context next to every spec/task tag

**D** — **Every comment must be understandable without opening SPEC.md or TASKS.md.** Spec/task IDs may still be cited, but the comment itself must carry the full relevant information (what the constraint/design is, why). Audit found pervasive counterexamples: `types.hpp:10` ("replaced by AssetId handles in T7"), `asset_registry.hpp:17` ("shim removed V4" — V4 undefined anywhere), `view_target.hpp:3` (`SPEC §3.2 V3.4 T5` bare), `mesh_renderer.cpp:220-222` ("the gate's opaque mesh" — which gate?), `asset_registry.cpp:19-20` (cites rule name `disposition_render` without stating the constraint), `scene/store.hpp:81` + `store.cpp:140-141` ("for T1…T1 gate expects exactly 4 fields"), `scene/composite_key.hpp:7` (dated web citation, no substance), `scene/translate_context.hpp:71` ("Q40:B 2026-08-23" decision-log shorthand), plus inconsistent formats (`SPEC S3/S5` vs `SPEC §3/§9`). Counter-exemplars to copy: `broker/asset_store.hpp:3-13`, `volume/ray_caster.cpp:14-18`. Fix sweep across all modules: rewrite each tag-only comment so the design rationale stands alone, then keep the ID as trailing provenance.

**FR:** none (docs-only; R9 documentation-map row below).

**T** — mechanical floor added to audit: forbid bare patterns like `"(T[0-9]+)"`, `"V[0-9]+\.[0-9a-z]+"`, `"SPEC S?[0-9]"` occurring without ≥120 chars of explanatory prose in the same comment block (heuristic grep + review gate); spot-check list above all rewritten; Doxygen on changed comments.

**G** — suite green, audit green (new comment-context rule enforced or waived with explicit allowlist).

## T16: Plane capability — GPU volume-plane extraction replaces textured-quad-only story

**D** — **Premise correction + redesign:** the review found `app/plane_sample.cpp` feeds `PlaneRenderer` a **procedural gradient image on a unit quad** (`makeGradientImage()` `:51-63`, `PlaneInstance{&geometry_, &image_, I}` `:68-71`) — neither a mesh nor a volume slice (the user-reported "used a mesh as input" is factually not what it does; there is no `io/` call in the file at all). But the underlying design concern is correct: in this engine a "plane" semantically means **a slice extracted from volume data**, yet nothing demonstrates extraction — even MPR computes slices on the CPU (`app/mpr_slice.cpp:36-88` `makeSliceImage` loops voxels through the TF) and merely *displays* them with `PlaneRenderer`. Redesign: (1) add GPU volume-plane extraction — a `VolumeSliceRenderer` (or `VolumeRenderer` slice mode) that samples the cached `core::Texture3D` at the view's `ClipPlane` in the fragment shader (texcoord mapping `(idx+0.5)/dim` as in `volume_renderer.cpp` ray-cast), so a plane through a volume is produced entirely on GPU; (2) rework `plane_sample` to load `sample_ct.nrrd` and show a GPU-extracted oblique/axial plane (not a gradient quad, not a mesh); (3) rewire MPR 2D views onto the GPU extraction path so slice scrolling becomes interactive (kills the frozen mid-volume CPU images); `PlaneRenderer` remains the display primitive for image-backed quads (MPR contour overlay until T11 lands).

**FR:** extends `FR-render.5`/`FR-app.2` — extracted-plane pixel equals `tf.sample(dataset.sampleTrilinear(...))` analytic value within 1/255 at probe points; MPR axis convention (T=const Z, C=const Y, S=const X) preserved.

**T** — gate asserts: synthetic 2×2×2 volume, plane z=mid → FBO pixels match CPU oracle within 1/255 (N>=3); `plane_sample` loads a volume (grep `loadNrrdVolume app/plane_sample.cpp` ≥1 hit) and renders an extracted plane; interactive slice-index change updates output without CPU re-loop (frame-time assertion optional; correctness via readback after state change).

**G** — suite green (N>=3), audit green; MPR 2D views GPU-driven; `makeSliceImage` retained only as test oracle.

## T17: OIT sample — opaque + transparent meshes with depth overlap (no quads)

**D** — Current `app/oit_sample.cpp` uses **three coplanar-normal transparent quads** (one shared golden quad mesh, alphas 0.55, stacked z=+0.5/0/−0.5, camera at (0,0,3)) — no opaque geometry at all, and quads only. Replace with a scene of **actual meshes**: ≥2 opaque meshes (e.g., golden box + bunny.obj or teapot.obj from `data/meshes/`) interleaved with ≥2 transparent meshes (e.g., two nested glass-like boxes/spheres built procedurally or from `data/meshes/`), arranged so that along the view direction each transparent mesh overlaps both opaque meshes and each other (e.g., opaque objects at different depths inside/between two enclosing transparent shells). This exercises the real OIT contract: opaque pass renders first, transparent fragments capture into the per-pixel linked list, depth-sorted composite blends over the opaque result — including the case the current sample never tests (opaque behind/in-front-of transparency). **Depends on T21**: correct opaque self-occlusion needs a depth buffer on the render target (today v1 FBOs are color-only — architecture-review finding "no depth attachment anywhere"; T21 adds the optional depth attachment + per-view depth-test flag this sample consumes).

**FR:** `FR-render.2/3` — center-region pixels match the analytic depth-ordered premultiplied blend of [opaque occluder]→[near transparent]→[far transparent] chain within 1/255; opaque pixels under no transparency stay alpha 255; adding the transparent meshes flips the pipeline on (spy count == number of transparent meshes).

**T** — gate asserts (explainable): known arrangement with closed-form expected composite at ≥3 probe pixels (fully-opaque region, near-transparent-over-opaque region, far-transparent-over-near-over-opaque region); no quad primitives (`grep -c "unitQuadXY\|makeQuadMesh" app/oit_sample.cpp` == 0); pipeline spy engaged exactly for the transparent set; suite green N>=3.

**G** — suite green (N>=3), audit green, OIT sample updated instructions text matching the new scene. **Depends on:** T21.

## T18: Broker becomes the only app path — complete mapper inventory (volume/plane/contour)

**D** — Two related review findings fixed together: (1) `ViewSynchronizer::sync` **silently drops volumes** — a matched `VolumeObject` inserts a locally-defined `Noop : render::IRenderable` (`view_synchronizer.cpp:202-218`) and references a `PlaneMapper` that does not exist anywhere in `broker/`; (2) the broker façade has **zero app consumers** — every sample includes `render/` directly (`mpr_sample.cpp:54-64`, `volume_sample.cpp:32`, …), violating the documented ACL (`broker/README.md:7`: app never includes render/). Work: implement the missing mappers (`PlaneMapper : IMapper<scene::PlaneDesc, render::ClipPlane>` with `Space::VoxelIndex→World` conversion via dataset dims/model, plus `VolumeSliceObjectMapper` producing a real GPU slice draw once T16's extraction exists — until then `VolumeObjectMapper` must produce a working `VolumeRenderer` layer, never a Noop); route ALL samples through `IViewBridge` (`sync/renderAll/presentAll`) and delete direct `render/` includes from `app/*.cpp`; material hand-off stays stubbed only until T14 lands the material slot (`MeshObjectMapper.cpp:21` placeholder gets its real store then).

**FR:** no pixel change for mesh path (regression lock); volume path gains parity with direct-`VolumeRenderer` output within 1/255 (readback probe).

**T** — gate asserts: `grep -R "#include \"render/" app/ --include=*.cpp` → 0 hits; sync of a scene containing `MeshObject+VolumeObject+PlaneObject` produces ≥2 non-Noop layers whose FBO readback matches the direct-renderer oracles within 1/255 (N>=3); `grep -c "Noop" broker/` == 0; PlaneMapper unit test converts voxel-index plane z=35 → world point z=35.5 exactly.

**G** — suite green (N>=3), audit green (`broker_app_reach` now enforceable against live samples), ACL rule un-commented/enforced if it was review-only.

## T19: Persistence honesty — activate write-only scaffolding, unify StableKey

**D** — Review found the persistence mechanism is asserted by tests but not actually driving behavior: `SceneStore/ViewStore::dirtyFieldsSince` return hardcoded field sets ignoring the `dirtyLog_` they maintain (`store.cpp:138-144`, `:204-208`); `tombstoneGen_` is written on erase but never read (no stale-handle detection enforcement, `store.cpp:89`); `view_synchronizer.cpp:49-51` contains a fake `executor_->parallelFor` exercise whose results are discarded alongside `auto sceneDirty = …; (void)sceneDirty;` (:100-103); and identity is defined twice with divergent shapes — `StableKey{version,layoutId,viewId}` in `view_compositor.hpp:34-49` vs a version-less twin in `view_synchronizer.hpp:75-88`. Work: make `dirtyFieldsSince` compute from `dirtyLog_` (bounded drain per the documented contract); enforce tombstones in `resolve()` (stale id after erase → typed error); delete the fake parallel section and consumed-or-removed `(void)` discards; extract ONE `StableKey` into a shared broker header used by synchronizer+compositor; give `CameraMapper` an id-keyed multi-entry cache (current single-slot thrashes with >1 camera, `camera_mapper.hpp:42-45` vs `invalidate(id)` ignoring id at `:59`).

**FR:** none behavioral beyond correctness — existing persistence gates (T6) must stay green while their inputs become genuinely computed.

**T** — gate asserts: mutate only camera → `dirtyFieldsSince` returns `{Camera}` exactly (not the hardcoded 4-field set); erase object then `resolve(oldId)` → typed error (code 2 stale); `grep -c "parallelFor" broker/` == 0; single `StableKey` definition (`grep -rc "struct StableKey" broker/` == 1); two cameras alternating pans each get cache hits (spy/gen counters prove no cross-camera thrash).

**G** — suite green, audit green, `no_dump_sync` still enforced.

## T20: Renderer consolidation — one prologue, one quad, one hash, no glad leak

**D** — Deduplicate the copy-paste across the four technique renderers (review §"Duplication debt"): (1) bind-target+viewport+clear+disable-depth/blend prologue repeated verbatim 4× (`mesh_renderer.cpp:149-160`, `plane_renderer.cpp:241-252`, `slice_renderer.cpp:113-128`, `volume_renderer.cpp:185-201`) → extract one internal pass-setup helper (or fold into `core::DrawContext::beginPass`); (2) each `drawLayer` duplicates its own `render()` body minus prologue (`mesh_renderer.cpp:203-236` vs `:69-95`; plane basis-matrix math duplicated nearly line-for-line at `plane_renderer.cpp:284-297` vs `:351-360`; volume uniform block `:212-237` vs `:283-300`; slice clip loop `:130-156` vs `:175-211`) → merge via private shared implementation taking a "clear" flag; (3) full-screen/unit quad VAO built 3× with identical index pattern (`plane_renderer.cpp:119-176`, `volume_renderer.cpp:73-117`, `linked_list_oit.cpp:105-144`, NDC verts defined twice `:32-37`/`:40-45`) → one shared internal quad provider; (4) `geometryFor(handle)` identical in `mesh_renderer.cpp:42-52` and `slice_renderer.cpp:86-96` → shared helper over `AssetRegistry`; (5) `meshContentHash` deliberately duplicates `scene::computeContentHash` (`asset_registry.cpp:19-20`) → move the byte-hash into a GL-free header both layers include (e.g., `data/content_hash.hpp`) so there is one definition; (6) `slice_renderer.cpp:6` includes `<glad/gl.h>` and uses `GL_TRIANGLES` (`:288`) despite "render/ is GL-call-free" headers → route through a core/ constant/wrapper.

**FR:** regression lock R3 — zero pixel change; all renderer gates unchanged within 1/255.

**T** — gate asserts: suite green unchanged (all t7–t15 renderer gates byte-identical tolerance); mechanical greps: prologue pattern count ≤1 occurrence site, `kScreenQuadVerts` definitions ≤1, `#include <glad/gl.h>` under render/ == 0 hits.

**G** — suite green (N>=3 for readback gates), audit green.

## T21: Depth-buffer support — optional depth attachment + per-view depth state

**D** — Architecture-review finding: v1 framebuffers are color-only everywhere (`view_target.hpp`, `RenderTarget` docs), forcing painter's-order workarounds (MPR box face-ordering comment in `makeBoxMesh`) and blocking correct OIT-with-opaque-meshes (T17). Add an optional depth attachment: `core::Texture2D` depth format (or renderbuffer via a small core/ wrapper) attachable by `ViewTarget` when constructed with `DepthMode::Enabled`; `render::View` gains a per-view `depthTest` flag driving `enable/disableDepthTest` in the pass prologue (T20 helper); default stays color-only so every existing analytic gate is untouched; OIT capture/composite explicitly disable depth as today. Document that color-only remains the deterministic-gate default (llvmpipe-safe) and enabled-depth views assert completeness with the depth attachment.

**FR:** new acceptance — with depth enabled, two overlapping opaque meshes at different z render the nearer mesh's color at the overlap pixel (analytic arrangement), whereas color-only renders the later-drawn one; existing gates unchanged.

**T** — gate asserts (N>=3): depth-on target completeness check passes; near-mesh-wins overlap probe within 1/255; depth-off default leaves all prior FBO gates green; `LinkedListOIT` end-to-end still green with depth-on targets.

**G** — suite green (N>=3), audit green. **Feeds:** T17 (required), MPR 3D view cleanup (follow-up allowed to drop face-ordering hack).

## T22: Error-model hardening — namespaced codes, safe Result accessors

**D** — Review finding B8 + ergonomics: loader error enums collide numerically inside the single `int code` carried by `data::Error` (ImageLoadError 1..3, MeshLoadError 1..6, VolumeLoadError 1..8 — `image_loader.hpp:24-28`, `obj_mesh_loader.hpp:28-35`, `nrrd_volume_loader.hpp:42-51`), so disambiguation requires string parsing; `Result::operator*` is documented UB on failure with no debug assert (`result.hpp:83-86`); `operator->` silently returns nullptr; dead API `hasValue()` can never differ from `ok()`. Work: extend `Error` with a domain tag (small enum: Io/Core/Render/Broker/Scene or carry the source enum value + domain) so codes are interpreted per-domain; add debug-only assertion (assert/abort macro, not exceptions) in `operator*` failure path; remove `hasValue()`; optional stretch: monadic `map`/`and_then` helpers to collapse the nested `if (failed()) return` dances (e.g., `mpr_sample.cpp:255-258`). All existing numeric assertions in tests keep working (codes unchanged within their domain).

**FR:** `FR-io.4`/`FR-core.2` preserved — typed errors remain; disambiguation now structural.

**T** — gate asserts: same numeric code from image vs volume loaders is distinguishable via the domain tag; `operator*` on failed Result aborts in debug build (death-test style where supported) and documents behavior; `grep -c "hasValue" data/result.hpp` == 0; suite green.

**G** — suite green, audit green.

## T23: Sample harness resize handling — framebuffer callback + aspect recompute

**D** — Review finding: there is no GLFW framebuffer-size event path — the harness re-reads `window_.width()/height()` lazily each frame (`sample_harness.cpp:82-83`) but every sample except MPR hardcodes aspect from compile-time `kWindowWidth/kWindowHeight` constants (e.g., `plane_sample.cpp:77-80`, `oit_sample.cpp:108-111`), so window resize distorts geometry. Work: register a framebuffer-size callback on `core::Window` (stored size + dirty flag surfaced to the harness), pass current pixel dims into `ISample::onResize(width,height)` (new optional ISample hook, default no-op), and update all samples to derive camera aspect from the live dims each frame (MPR already does for its grid — reuse that pattern). Keep the headless gate path unchanged (fixed-size offscreen runs never fire the callback).

**FR:** `FR-app.1` unchanged (smoke exit-clean); manual verification note added to sample instructions.

**T** — gate asserts: harness test simulates a resize (call the hook directly) and asserts the sample's next-frame projection matrix matches `glm::perspective(fov, newAspect, …)` within 1e-6; MPR grid re-resolves from new dims (existing T14 grid math); smoke set still exits 0.

**G** — suite green, audit green.

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

## Definition of Done — review follow-ups (T13–T23, user-mandated + architecture review)

- [ ] `T13`: no undocumented raw owning-suspect pointers in `scene/ broker/ app/` public APIs; every remaining borrow carries a lifetime note; audit ownership rule green; construction-order hazards removed from all samples.
- [ ] `T14`: unified typed asset store covers mesh + volume + image (+ material slot with real dedup replacing the `material=nullptr` placeholder); per-renderer pointer-keyed texture caches deleted; same-dataset-two-renderers gate proves one GPU texture.
- [ ] `T15`: comment sweep landed — every SPEC/task tag is accompanied by self-contained rationale (incl. the false "deduped RE material handle" claim until T14 makes it true); bare-tag pattern rule green or explicitly allowlisted.
- [ ] `T16`: GPU volume-plane extraction shipped; `plane_sample` demonstrates an extracted volume plane (not a gradient quad); MPR 2D views interactive on the GPU path with CPU oracle retained for tests.
- [ ] `T17` (needs `T21`): OIT sample = ≥2 opaque + ≥2 transparent real meshes with view-direction overlap, depth-buffer-backed target, analytic composite probes green N>=3.
- [ ] `T18`: all samples route through `IViewBridge`; volume/plane layers are real (no Noop); `PlaneMapper` exists with voxel→world conversion test.
- [ ] `T19`: dirty tracking computed from `dirtyLog_`; tombstoned ids resolve to typed errors; single `StableKey`; no fake parallel code; multi-camera mapper cache.
- [ ] `T20`: renderer prologue/quad/hash/geometryFor deduplicated; zero pixel drift on all renderer gates; `<glad/gl.h>` gone from render/.
- [ ] `T21`: optional depth attachment + per-view depth flag; near-mesh-wins overlap gate; color-only default untouched.
- [ ] `T22`: domain-tagged error codes; debug-trap on failed `Result` dereference; dead accessors removed.
- [ ] `T23`: resize callback + live-aspect samples; simulated-resize projection gate.