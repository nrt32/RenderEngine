# TASKS — RenderEngine

Active backlog is **EMPTY** (V5 19/19 green, archived 2026-08-28 — `T1..T17` + `T8b` DepthConfig + `T11b` OIT fallback, 19 sessions). The completed sequential loops are archived in `COMPLETED_TASKS.md`: `T1–T16` (V1, 0.1.0) + `V2-T1–V2-T8` (V2) + `V3 T1..T23` (pure-redesign) + `V4 T1..T19` (review batch) + `V5 T1..T17+T8b/T11b` (19 sessions, extensibility & visualization-reuse) (all green, archived 2026-08-28). See SPEC.md for FRs, §9 for the V2 archive / §9.1 for the V3 roadmap, §6 for guardrails, NAMING_CONVENTIONS.md for style.

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
  `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` and
  `LOOP_BUILD_TEST_CMD` into the runner's environment. The T1 gate test
  already in the suite (COMPLETED_TASKS.md T1, gate item 4, updated for V3) makes this
  enforceable: a forgotten source must FAIL the gate loudly, never silently
  audit the wrong dirs.
- Every gate also runs the mechanical audit (`tools/audit.sh`) with
  `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` (via
  `source tools/env.sh`).
- Unit tests build with ASan+UBSan; GL-touching tests use the offscreen GL
  fixture (headless). No exceptions in v1; typed `Result` errors (SPEC §5).
- Logging via spdlog only (no printf/cout). Doxygen on all public API.

> V2 clean: `V2-T1–V2-T8` (IRenderer, multi-view, AssetRegistry, utils/, per-OS backend, draw-cache, .glsl externalization, RE_GLSL_VERSION) were green and archived to `COMPLETED_TASKS.md` 2026-08-23; `tools/logs/` (`run_all.out`, `session_*.lease`, `task_*.{log,gate.log,pass,review}`) purged. Workspace is clean for the pure-redesign iteration.

## V3 backlog — EMPTY (V4 19/19 green, archived 2026-08-28)

All 19 review follow-up tasks (T1–T19, dependency-ordered) have been completed and archived to `COMPLETED_TASKS.md` V4. Next iteration will be planned via `/loop-init`.

> **Naming:** archived backlog was `T1..T19` (foundations → REContext → pair-key → ... → View lights). Next `T1..Tn` will be assigned per new iteration.

## V5 backlog — EMPTY (V5 19/19 green, archived 2026-08-28)

All 19 sessions (T1..T17 + T8b DepthConfig + T11b OIT fallback) have been completed and archived to `COMPLETED_TASKS.md` V5. Next iteration will be planned via `/loop-init`.

> **Naming:** archived backlog was `T1..T17` + `T8b`/`T11b` (19 sessions, `T8→T8+T8b` `T11→T11+T11b` per Q1/Q2). Next `T1..Tn` will be assigned per new iteration.

## Definition of Done — V5 (archived)
- [x] All 19 task gates green; full suite green on a clean tree at T17 (19 sessions)
- [x] `suite green N>=3` where GL-touching, `audit green`, `ASan+UBSan clean`, `LICENSE` per-dir, `R9` doc-map, `R3` regression, `R4` evidence, `comment_tag_context` PASS, `install` reproduces — see `COMPLETED_TASKS.md` V5 for evidence

## V6 Active Backlog — Dumb Layers + Priority + Long-lived Samples (4 tasks)

### T6.1: Dumb layers `LAYER_0..7` + per-object `priority` (count-agnostic, no `LayerMask`)

**D** — Make `Layer` dumb and count-agnostic and add scoped `priority`. `scene/layer.hpp` `enum Layer:uint16_t{LAYER_0=0..LAYER_7=7, COUNT=8}` no semantic names (`Volume`/`Mesh` etc.), no `LayerMask` type; lower numeric draws first, `64` is doc edit only (no `1u<<layer` UB, no array sized by `COUNT`). `scene/iscene_object.hpp` add `int32_t priority{0}` alongside `Layer layer` on `ObjectBase`/`ISceneObject` (`priority()`/`setPriority()` `++generation` like `setLayer`), new `FieldId::Priority=12` `scene/field_id.hpp`. All 6 concrete objects `scene/objects/*.hpp` `layer{LAYER_0}` single default + `priority{0}`. `scene/view.hpp` + `scene/view.cpp` delete `layerMask{0xFFu}` + `layerOverrides` map + 4 setters; keep `layerGen` only for debugging if needed else remove. `scene/store.hpp` serialize wire drops `LayerMask/LayerOverrides`, `Version 1->2` migration.

**T** — suite green: `grep -c "LAYER_0" scene/layer.hpp ==1 && grep -c "LayerMask" scene/ ==0 && grep -c "layerOverrides" scene/ ==0 && grep -c "0xFFu" scene/view.hpp ==0`; `SceneStore` pixel parity 1/255 on existing gates before broker reorder.

**G** — audit green, no `LayerMask`, no semantic names, `priority` present.

### T6.2: Global `techniqueOrder` + drop mask/override sort + scoped priority + stable insertion

**D** — `broker/render_stack.hpp` add global `std::array<SceneKind,6> techniqueOrder{Volume,VolumeSlice,Plane,Mesh,MeshSlice,Contour}` explicit hardcoded renderer call order that governs cross-type order inside each `Layer` (BGFX `Sequential` / UE `AddPass` precedent). `broker/view_synchronizer.hpp` `ViewCache` drop `layerGen` tied to mask, keep `layerOrderHash`. `broker/view_synchronizer.cpp` delete `techniquePriorityFor()` + `mask cull 1u<<eff` + `itOv override` + `curGen ^= mask/overrides` `101-106` and `:293`. New order: `struct OrderEntry{oid,Layer,int orderIdx,int priority,size_t insertionIdx}` `orderIdx=indexIn(techniqueOrder,kind)` `priority=obj->priority()` `stable_sort by (uint16(layer) asc, orderIdx asc, priority asc, insertionIdx asc)` scoped priority inside same `layer+type` bucket so `VolumeSlice prio100` still before `Contour prio0` on same `LAYER_0`. `orderHash ^= layer+orderIdx+priority+oid`. `mapItemToLayer` unchanged.

**T** — gate: new T8-layer semantics preserved without mask/override: `same LAYER_0 different type via techniqueOrder 1/255 swap invariant` (`VolumeSlice before Contour`), `LAYER_0 vs LAYER_1 overlay on top 1/255`, `same LAYER_0 same type prio0 vs prio1 scoped priority 1/255` + `scoped cross-type VolumeSlice prio100 still before Contour prio0`, `stable insertion same layer+type+prio`.

**G** — suite green N>=3 `llvmpipe`, audit green, sample smoke still.

### T6.3: Fix failing samples (bounded `300` frame harness)

**D** — Diagnose via `source tools/env.sh && eval "$LOOP_BUILD_TEST_CMD"` + headless sample runs `RE_SAMPLE_MAX_FRAMES=20 tools/build.sh && timeout ... build/app/re_sample_*`. Fix root cause (likely layer migration `LAYER_0` default or missing `techniqueOrder` entry for `Plane`/`Contour` or uninitialized `priority`). Keep harness bounded `kDefaultFrames=300` `app/sample_harness.hpp:175` `run(sampleMaxFrames)` discipline. No long-lived change here.

**T** — suite green, `tools/run_sample.sh mesh|plane|volume|slice|oit|mpr` headless `RE_SAMPLE_MAX_FRAMES=20` exit 0, bounded default `300` not interactive hang.

**G** — audit green, samples run.

### T6.4: Long-lived samples (non-testing, opt-in `runInteractive` + camera `pan/rotate/zoom`)

**D** — Create `app/*_interactive.cpp` or `--interactive` flag set that bypasses `sampleMaxFrames` and uses `SampleHarness::runInteractive()` `app/sample_harness.cpp:111` `until shouldClose()` with `pan+rotate+zoom` camera interaction via `scene::CameraController` `scene/camera_controller.hpp` + `app::GlfwCameraInteractor` `app/glfw_camera_interactor.hpp` (left drag rotate `dx*0.5deg`, right drag pan `dx*0.01`, scroll/middle drag zoom `exp(-dy*0.02)`). Each long-lived target mirrors its bounded peer (mesh/plane/volume/slice/oit/mpr) but `renderFrame` calls `interactor.update(view)` before `syncRenderPresent`, respects `WantCaptureMouse`, skips orthographic slice views per `V5 T9` guard, and mutates via `View::mutateCamera` so `viewGen`/`generation` bump lets `broker` re-translate only dirty camera fields `1e-6` analytic. Guard `tools/test.sh` and `ctest` never use them (exclude from `CMake RE_BUILD_SAMPLES` long-lived targets via `EXCLUDE_FROM_ALL` or separate `re_samples_long` target). Docs `docs/samples.md` note `long-lived not for testing`.

**T** — gate: new targets build, `build/app/re_sample_*_long --help` or `runInteractive` path exists + `GlfwCameraInteractor` `update()` wired (left rotate, right pan, scroll zoom within `1e-6`), `ctest` suite count unchanged (long sample not run), short bounded samples still `1/255` `300` frames.

**G** — suite green, audit green, long samples excluded from default build/test.

## V7 Active Backlog — Facade Depollution + Renderer Coherence (9 tasks)

> Collated 2026-08-28 from 5 parallel XHigh sessions (muse-spark-1.2-contributor). Rule-of-thumb: `Mesh Load` in `Store.hpp` is facade pollution — belongs to `utils/`; `Engine` duplication is same archetype. Validated exemplar Ex1 `MeshRenderer::render` vs `drawLayer` dual API meaningless when meshes spread over multiple Layers per View per cycle (systemic across all 6 renderers, two OIT policies, layer-blind `IRenderable`). Ex2 View-level blind `depthTest` dangerous for heterogeneous `View` (`VolumeSlice+Mesh` on same `LAYER_0`, MPR hides by 4 separate Views). Ordering: depollute `Store`→`Engine` before collapsing `render` API, before re-scoping depth, before unifying layers/techniqueOrder.

### T7.1: Store IO depollution — `loadMesh/loadVolume` out of `Store.hpp`

**D** — Delete `scene/store.hpp:218` `loadMesh/loadVolume` (2 decls) + `scene/store.cpp:1-10` `#include "io/mesh/obj_mesh_loader.hpp"`/`io/volume/nrrd_volume_loader.hpp`/`nlohmann/json.hpp` (header stays lean) + `scene/CMakeLists.txt:13` `PRIVATE re_io` edge. Extract to `utils/asset_utils.hpp` `Result<SharedMesh> loadMeshAsset(path)` + `Result<SharedVolume> loadVolumeAsset(path)` (IO-only, header-only, `utils/` owns filesystem). Keep 4-step `load→shared_ptr→registerMeshAsset→addMeshObject` ceremony in `utils/`; `scene/store.hpp:215` doc retains `4 steps →1 call` note but points to `utils/`. `scene/builders.hpp` `Objects::mesh` stays value builder, not IO. `SceneStore` stays pure value lib `data+volume+glm` per `docs/spec/modules.md:21`.

**T** — suite green: `grep -c "loadMesh\|loadVolume" scene/store.hpp ==0 && grep -c "obj_mesh_loader" scene/ ==0 && grep -c "nrrd_volume_loader" scene/ ==0 && grep -c "re_io" scene/CMakeLists.txt ==0`; `SceneStore` pixel parity 1/255 on existing mesh/volume gates via `utils::loadMeshAsset`.

**G** — audit green, `disposition_scene` `scene|#include.*render/` still green, `scene` no `io` dep, no `json` include.

### T7.2: Engine IO depollution — `Engine::addMesh/addVolume` out of facade

**D** — Remove `include/render_engine/engine.hpp:32-33` `#include "io/mesh/obj_mesh_loader.hpp"`/`io/volume/nrrd_volume_loader.hpp` + inline `87-165` `addMesh(path,transform,mat)`/`addVolume(path,transform,tf)` IO path (3+4 overloads). Keep only store-typed API `addMesh(AssetRef<Mesh>,transform,mat)→ObjectId` + `addVolume(AssetRef<Volume>,…)` delegating to `ctx_.store().add*Object`. Call sites `app/mesh_sample.cpp:32` + samples migrate to `utils::loadMeshAsset`→`registerMeshAsset`→`addMesh`. Delete `33` `io` transitive include from `re_engine` INTERFACE `CMakeLists.txt:210`.

**T** — suite green: `grep -c "loadObjMesh\|loadNrrdVolume" include/ ==0 && grep -c "obj_mesh_loader" include/ ==0 && grep -c "io/mesh" include/render_engine/ ==0`; `Engine` facade tests via `utils::loadMeshAsset` still 1/255.

**G** — audit green, `re_engine` no `re_io` linkage, `acl_app_render` still green.

### T7.3: Collapse `render()` dual API — single `drawLayer` path (Ex1 systemic)

**D** — Delete public `render(scene,camera,target)` from all 6 renderers: `render/mesh_renderer.hpp:96` `MeshRenderer::render`, `render/slice_renderer.hpp:96` `SliceRenderer::render`, `render/volume_renderer.hpp:148` `VolumeRenderer::render`, `render/volume_slice_renderer.hpp:149` `VolumeSliceRenderer::render`, `render/plane_renderer.hpp:167` `PlaneRenderer::render`, `render/contour_renderer.hpp:100` `ContourRenderer::render`. Keep private `drawInstances`/`clipInstances` as `drawLayer` impl; `render/mesh_renderer.cpp:166-241` inline OIT `anyTransparent ? begin/drawOpaque/drawTransparent/end` deleted (now single `drawInstances` blend-off, OIT only via `broker/view_compositor.cpp:94` `captureTransparents` out-of-band). Direct tests port via minimal `render::View` + `REContext::current().beginPass` + `View::addItem(scene,renderer)`. Remove `drawOpaque/drawTransparent` private pair. `render/i_renderable.hpp:45` retains but add `Layer` param in T7.5.

**T** — suite green N>=3 `llvmpipe`: `grep -c "data::Result<void> render(" render/*_renderer.hpp ==0` ; `mesh/volume/plane/slice/oit` direct-oracle parity via `View` path 1/255; no silent `>0` asserts.

**G** — audit green, no `render()` in `render/`, single OIT policy only via compositor.

### T7.4: Scope depth per-layer/technique not per-View (Ex2)

**D** — Remove view-level depth ownership: `scene/view.hpp:59-61` delete `bool depthTest` + `DepthConfig depthConfig{false}` duplicate + `depthTestGen` + `setDepthTest/setDepthConfig` + `scene/depth_config.hpp` value object moved to `broker/render_stack.hpp` per-technique `DepthMode` or per-`Layer` policy. `render/view.hpp:87-88` delete `depthTest_` + `setDepthTest` + `render/view_target.hpp:41` `DepthMode` per-`ViewTarget` replaced by per-layer pass `DepthMode` map in `RenderStack`. `core/re_context.cpp:166-185` `beginPass(...,depthTest)` now per-layer enable/disable. Delete `include/render_engine/engine.hpp:354-355` `applyMeshDepthDefault{DepthConfig{true}}` global blind enable. `scene/view.hpp:51-53` `depthTestGen` removed from `ViewCache` `broker/view_synchronizer.hpp:128`.

**T** — suite green: `grep -c "depthTest" scene/view.hpp ==0 && grep -c "DepthConfig" scene/ ==0 && grep -c "applyMeshDepthDefault" include/ ==0`; mixed `LAYER_0 VolumeSlice prio0 + Mesh prio0` depth-correct 1/255 (slice no depth, mesh depth, contour overlay on top), same heterogenous `VolumeSlice prio100` vs `Contour prio0` still before via techniqueOrder.

**G** — audit green, `engine_depth_default` rule `DepthConfig\{true` deleted or moved to `broker/render_stack` check, no view-level depth.

### T7.5: Finish dumb layers `LAYER_0..7` + global `techniqueOrder` + scoped priority (subsume V6 T6.1/T6.2)

**D** — Depends on T7.4 depth removal. `scene/layer.hpp` `enum Layer:uint16_t{LAYER_0=0..LAYER_7=7, COUNT=8}` no semantic names (`Volume`/`Mesh` etc.), no `LayerMask` type; lower numeric draws first, `64` doc-only (no `1u<<layer` UB, no array sized by `COUNT`). `scene/iscene_object.hpp` add `int32_t priority{0}` alongside `Layer layer` on `ObjectBase`/`ISceneObject` (`priority()`/`setPriority()` `++generation` like `setLayer`), new `FieldId::Priority=12` `scene/field_id.hpp`. All 6 concrete objects `scene/objects/*.hpp` `layer{LAYER_0}` single default + `priority{0}`. `scene/view.hpp` + `scene/view.cpp` delete `layerMask{0xFFu}` + `layerOverrides` map + 4 setters; keep `layerGen` only for debugging if needed else remove. `broker/render_stack.hpp` add global `std::array<SceneKind,6> techniqueOrder{Volume,VolumeSlice,Plane,Mesh,MeshSlice,Contour}` (BGFX `Sequential` precedent). `broker/view_synchronizer.hpp` `ViewCache` drop `layerGen` tied to mask, keep `layerOrderHash`. `broker/view_synchronizer.cpp` delete `techniquePriorityFor()` + `mask cull 1u<<eff` + `itOv override` + `curGen ^= mask/overrides` `101-106` and `:293`. New order: `struct OrderEntry{oid,Layer,int orderIdx,int priority,size_t insertionIdx}` `orderIdx=indexIn(techniqueOrder,kind)` `priority=obj->priority()` `stable_sort by (uint16(layer) asc, orderIdx asc, priority asc, insertionIdx asc)` scoped priority inside same `layer+type` bucket so `VolumeSlice prio100` still before `Contour prio0` on same `LAYER_0`. `orderHash ^= layer+orderIdx+priority+oid`. `scene/store.hpp` serialize wire drops `LayerMask/LayerOverrides`, `Version 1->2` migration.

**T** — suite green: `grep -c "LAYER_0" scene/layer.hpp ==1 && grep -c "LayerMask" scene/ ==0 && grep -c "layerOverrides" scene/ ==0 && grep -c "0xFFu" scene/view.hpp ==0`; `SceneStore` pixel parity 1/255 + `same LAYER_0 different type via techniqueOrder 1/255 swap invariant` (`VolumeSlice before Contour`), `LAYER_0 vs LAYER_1 overlay on top 1/255`, `same LAYER_0 same type prio0 vs prio1 scoped priority 1/255` + `scoped cross-type VolumeSlice prio100 still before Contour prio0`, `stable insertion same layer+type+prio`.

**G** — audit green, no `LayerMask`, no semantic names, `priority` present, `engine_depth` independent.

### T7.6: Unify asset identity — delete triple store (`byObject_`/`legacyHandleCache`)

**D** — Single identity `data/content_hash.hpp:31` `hashStableBytes` (SHA-256 truncated 64 prod) — keep `scene/store.hpp:284-286` `AssetRegistry<T>` as canonical. Delete `broker/asset_store.hpp` mesh-only store + `render/asset_registry.hpp:590-597` `byObject_` dual-key shim + `render/volume_renderer.hpp:204` `legacyHandleCache` + `render/volume_slice_renderer.hpp:194` + `render/plane_renderer.hpp:211` pointer caches. Keep only `byHash_` content-hash dedup; `register*Asset` → `resolve*` via `AssetHandle{index,generation,hash}` (also `VolumeTextureHandle`/`ImageTextureHandle` unify to `AssetHandle`). Update `broker/*_object_mapper.cpp` to use `scene::SceneStore` registry only.

**T** — suite green: `grep -c "byObject_" render/ ==0 && grep -c "legacyHandleCache" render/ ==0 && grep -c "AssetStore" broker/ ==0`; dedup still 1/255 (identical bytes share one `Texture3D` across 2 `VolumeRenderer` instances).

**G** — audit green, `asset_indirection` `data::Mesh::positions` still `render/re_scene` check passes, no pointer-key maps, no `shared()` global mutable (or guarded).

### T7.7: Split `View` god + `ViewSynchronizer` god

**D** — `scene/view.hpp:39` keep `View{rect,plane,itemIds,camera}` only (4 fields + per-field gens). Move `clearColor,DepthConfig,lights,layerMask/overrides` to `scene/view_ext.hpp` composition `ViewState` or `broker` policy object; delete `mutateCamera` lambda leak `98-106` in favor of `setCamera`. Split `broker/view_synchronizer.cpp:86-340` `sync` 250-line god into `ViewHasher` (curGen), `LayerOrderer` (stable_sort `OrderEntry`), `ItemTranslator` (mapItemToLayer). Keep `ViewCompositor` OIT capture unchanged.

**T** — suite green: `wc -l scene/view.hpp <70 && wc -l broker/view_synchronizer.cpp <220`; `View` still 1/255 on existing gates.

**G** — audit green, `View` SRP, no `mutateCamera` lambda.

### T7.8: Harden GL ownership — `core/rhi/gl/` RHI anchor

**D** — Uncomment `tools/audit.rules:50` `rhi_ownership forbid_outside core/rhi/gl|\bgl[A-Z]` ; move `core/re_context.cpp:28` `#include <glad/gl.h>` + raw `gl*` bodies under `core/rhi/gl/` `IRHIContext{ITexture,IBuffer,IFramebuffer,IShader,blit,capabilities}` (Qt QRhi/O3DE/Adept per `docs/spec/modules.md:34`). `core/re_context.hpp` includes `core/rhi/irhi_context.hpp` not `<glad>`. Update `docs/spec/guardrails.md:13` transitional note to landed, keep `gpu_api_ownership` as `core/rhi/gl|` hard. Keep `render_no_glad` `forbid_inside render|#include.*glad` still green.

**T** — suite green: `grep -c "rhi_ownership" tools/audit.rules ==1 active && grep -c "gl[A-Z]" core/rhi/gl/re_context.cpp >=1 && grep -c "glViewport\|glClearColor" core/*.cpp ==0` outside `core/rhi/gl/`.

**G** — audit green, `rhi_ownership` enforced, `render/` no `glad`.

### T7.9: Evidence & audit placeholder activation

**D** — Replace `tools/audit.rules:163-165` `__never_matches_weak_assert_review_gated__`/`__never_matches_evidence_rule__`/`__never_matches_regression_lock__` placeholders (review-gated) with mechanical `require_grep 1/255|1e-6` per-task `grep -c` counts (floor) as already used in `TASKS.md` DoD. Uncomment `tools/audit.rules:88-89` `layer_count/layer_mask` (now `COUNT=8` + no `LayerMask`) or delete them (dumb count-agnostic). Enforce `find_package` fallback pin: delete `CMakeLists.txt:58` `find_package(glfw3/glm/spdlog/json QUIET)` before `FetchContent` or pin system version `== 3.4/1.0.1/1.14.1/3.11.3`. Enforce `1/255` per `render/*` gate, not bare `>0`.

**T** — suite green: `grep -c "__never_matches" tools/audit.rules ==0 && grep -c "1/255\|1e-6" tests/*.cpp >= 8`; `audit green`.

**G** — audit green, no placeholder `__never_matches`, `deps_pinned` still `GIT_TAG` verified.

## V8 Active Backlog — Correctness & Hardening (6 tasks, blocked on V7 green)

> Collated 2026-08-28 from 10 parallel XHigh sessions. V7 covers facade depollution + renderer coherence (Ex1/Ex2). V8 covers remaining `CRITICAL`/`HIGH` correctness, ownership, and build hardenings that V7 leaves — NRRD/OBJ/Image loader overflow & budgets, canonical content-hash & spy pollution, EGL/REContext isolation, generation/dirty contract, broker/ownership regex hardening, build/export defaults. All V8 tasks depend on V7 green (especially T7.6 asset unification + T7.8 RHI anchor). Execution order inside V8 is linear T8.1→T8.6.

### T8.1: Loader hardening — NRRD/OBJ/Image overflow & budgets (CRITICAL C13, HIGH H02/H03/H04)

**D** — `io/volume/nrrd_volume_loader.cpp:362-433` add `uint64_t` checked `voxelCount` (`__builtin_mul_overflow` or `checked_mul`), range-check `sizesValue[i] <= UINT32_MAX` before `static_cast<uint32_t>`, reject `size==0` with `VolumeIo::BudgetExceeded` (code 8). `data/volume_dataset.hpp:44` + `volume_dataset.cpp:12` assert `voxels.size()==sx*sy*sz`, `sampleTrilinear:32` clamp guards `NaN` + `size==0` wrap. `io/mesh/obj_mesh_loader.cpp:21` remove `kMaxFaceVertices=64` cap — unbounded fan triangulation, accept negative relative indices (`-1` → last vertex) per OBJ spec, add `kMaxFileBytes` probe (like NRRD `512^3*8+64KiB`) before `positions.emplace_back`. `io/image/image_loader.cpp:36` add `file_size` pre-probe + `BudgetExceeded` (mirror NRRD), overflow check `size_t(width)*height` before `stbi_load`. `data/mesh.hpp:40` `fromTriangles` asserts `idx%3==0 && idx < positions.size()`. `volume/transfer_function.hpp:43` assert `points` non-empty sorted strictly increasing else typed error (prevent divide-by-0 `inf/NaN`).

**T** — suite green `N>=3`: hostile `sizes: 4294967296 1 1` truncated NRRD → `BudgetExceeded` not crash, `sizeX==0` wrap caught, 100-vert n-gon OBJ fan-triangulates, negative indices resolve, 2GB `golden_image.png` → `BudgetExceeded` before OOM, `VolumeDataset::sampleTrilinear(NaN)` clamps not UB, `TransferFunction` duplicate points → typed error. `FR-io.1/2/4` + `FR-data.1/2/3` + `BudgetExceeded` gates still 1/255 / 1e-6.

**G** — audit green, `no_secrets`/`no_legacy_api` still green, no `re_io` leak to `scene/`.

### T8.2: Canonical content-hash & spy depollution (CRITICAL C04/C11, HIGH H01/H02)

**D** — `data/content_hash.hpp:48-152` make `hashStableBytes` canonical: `float`/`uint32_t`/`int32_t` → little-endian bytes via `memcpy` + `htole32` (not `reinterpret_cast` host bytes), FNV-1a → `SHA-256` truncated 64 (or keep FNV but canonical LE), NaN payload canonicalized (`-NaN` → `NaN`), document `SPEC §10.1` hierarchical `Version:LayoutId:Type:Hash` determinism across runs. Move `contentHashSpy atomic<uint64_t>` out of `data/` (`data/content_hash.hpp:32`) into `test_utils/content_hash_spy.hpp` (data stays pure math, headless-testable without global mutable). `render/asset_registry.cpp:229` + `broker/mesh_object_mapper.cpp:17` ensure `SceneStore::meshAssets_` `AssetId` handle minted at `loadMeshAsset` (via `utils/asset_utils.hpp` from T7.1) is reused — `mapCached` hit must not re-hash full vertex buffer on every `setTransform` (bump only `generation`). `broker/material_mapper.cpp:18` fix `phongValueHash` XOR weak → FNV continuation same as `render/asset_registry.cpp:44` `materialContentHash`, consistent field set (`baseColor+specular+shininess+ambient+diffuse`, `doubleSided` dropped with typed warning).

**T** — suite green: same mesh bytes on LE vs BE mock hash equal, `contentHashCallCount()==0` during 60-frame steady-state `setTransform` orbit (no per-frame re-hash), `material` identical bytes dedup to `materialSlotCount()==1` across two renderers, `grep -c "contentHashSpy" data/ ==0 && grep -c "contentHashSpy" test_utils/ ==1`.

**G** — audit green, `asset_indirection` still 0 hits, `data/` no `spdlog`/`atomic` pollution.

### T8.3: Offscreen/EGL ownership & REContext per-context isolation (CRITICAL C05/C06, HIGH H02/H07)

**D** — `utils/offscreen_context.hpp:141-143` replace `void* eglDisplay_/eglContext_` + raw `GLFWwindow* window_` with RAII: `struct EglHandle{ EGLDisplay dpy; EGLContext ctx; EGLSurface surf; }` typed, `unique_ptr<GLFWwindow, GlfwDeleter>` or explicit `release()` with move-nulling. `utils/offscreen_context.cpp:313` fix `reinterpret_cast<void*>(&eglGetProcAddress)` UB → `reinterpret_cast<GlLoadProc>(eglGetProcAddress)`, guard `#include <EGL/egl.h>` with `#ifdef RE_HAS_EGL` (`utils/CMakeLists.txt:33` already probes `RE_EGL_LIBRARY`). `core/re_context.cpp:53` `thread_local REContext t_fallback` → per-`EGLContext` map (`unordered_map<EGLContext, REContextState>` mutex-guarded like `g_windowMap:49`) or at minimum `invalidate()` on EGL `release:129` so second EGL context on same thread starts cold (no `viewport/clearColor` bleed). `utils/offscreen_context.cpp:199` save/restore `glfwWindowHint` globals (pollution to `core::Window`), after `loadCoreGl` verify `GL_MAJOR==4 && MINOR==6 && CORE_PROFILE` else typed error (SPEC §2 OpenGL 4.6 core). `core/re_context.cpp:191` document `REContext::current()` `thread_local` vs single-threaded contract (`nfr.md:24`) — keep mutex only for map, not `t_current`.

**T** — suite green `N>=3`: two sequential `OffscreenContext` on same thread → second `viewport==0` cold not stale, `glfwWindowHint` pollution test (`OffscreenContext` then `Window::create` still `VISIBLE TRUE`), missing `libEGL.so` configure still passes (VG9), `EGL` context reports 4.6 core else error. `FR-core.1` still GL 4.6 core.

**G** — audit green, `gpu_api_ownership` `core|` still green, `utils/` no raw `gl*`.

### T8.4: Generation/dirty contract hardening (CRITICAL C01/C04, HIGH H01/H06/H08)

**D** — Fix `ViewSynchronizer::sync` `broker/view_synchronizer.cpp:86-340`: (1) `120-126` silent success without `ViewCompositor` → return typed error code 10 not success, `lastStoreGen` not advanced. (2) `103-106` nondeterministic `curGen` hash over `unordered_map layerOverrides` → sort keys before xor or `std::map`. (3) `39-49` `techniquePriorityFor` switch closed → delete after `T7.5` `RenderStack::techniqueOrder` lands, `mapItemToLayer:342` six-way `if` → dispatch via `Broker::getByKind` registry (OCP). (4) `cache_[key.id]` `broker/camera_mapper.cpp:66` `layoutId=0` alias → carry `layoutId` from `StableKey` (`broker/stable_key.hpp:24`). (5) `86-113` `O(views*items)` `curGen` recompute every `sync()` even when `storeGeneration` unchanged → early-out on `storeGeneration==lastSceneStoreGen_` before hashing (hybrid poll `SPEC §10.4`). `scene/view.hpp:59-61` + `scene/iscene_object.hpp:138` + `scene/detail/generation_tracker.hpp:55` fix `tombstoneGen_` unbounded growth (prune on serialize or `pruneOlderThan` with version bump), `ViewStore::addView:295` missing `lightsGen/layerGen` seed → initialize from `generation`, `mutateCamera:97` lambda leak → already deleted in `T7.7` but ensure `SceneStore::bump(FieldId)` single entry point documented.

**T** — suite green `N>=3`: `sync` without compositor → typed error, two layouts same `viewId` different `layoutId` → distinct `ReCamera` (no alias), `camera orbit` only → `CameraMapper` miss not full rebuild, `tombstoneGen_.size()` bounded `O(#FieldIds)` after 10k add/remove cycles, `viewGen`/`projGen` analytic within 1e-6. `no_dump_sync` still green.

**G** — audit green, `broker_per_type` still 1-file-per-mapper, no `Noop`.

### T8.5: Broker/ownership & audit regex hardening (HIGH H03/H04/H05, MEDIUM M02/M04)

**D** — Harden mechanical floors that are currently false-negative: `tools/audit.rules:41` `deps_pinned` single-line `[^)]*` → `require_grep -Pzo "FetchContent_Declare[^)]*GIT_TAG"` or keep `utils/CMakeLists.txt:52` anchor as interim but add `audit.sh` multiline check; `42` `deps_pinned_no_branch` denylist `master|main|...|branch` → add `stable|next|latest|trunk|dev|vNext|1.x|refs/heads|origin/`; `49` `gpu_api_ownership` `forbid_outside core|\bgl[A-Z]` → also forbid `GL_*` constants + add `utils` `egl*` allowlist comment; `99` `broker_per_type` `class.*Mapper.*\n.*class.*Mapper` → `-Pzo` or `grep -c "class.*Mapper" broker/*_mapper.hpp ==1` built-in; `100` `isp_mapper_forbid` single-line `[^}]*mapCached` → cross-file `grep -l "mapCached" broker/*_mapper.*` + header `ICachedMapper` allowlist; `181` `no_per_target_sanitize` `add_compile_options.*-fsanitize` → `forbid_grep -fsanitize.*(target_compile_options|target_link_options|add_link_options)`; `153-155` `ownership_raw_ptr_*` `Type* name` → also `Type * name` spaced star + `void*`/`char*`/`auto*` + function pointers (`grep -E "(const\s+)?[A-Za-z_][A-Za-z0-9_:]*\s*\*\s*[a-z_]"`); update `utils/offscreen_context.hpp:125` `GLFWwindow* handle()` + `core/window.hpp:64` `GLFWwindow* handle()` to `/*borrow*/` or exclude via allowlist with `@note lifetime:`; `broker/broker.hpp:297` `sceneKindAliases_` raw borrow → `weak_ptr` observer or documented borrow with lifetime tag + fix `empty()/size()` inconsistency.

**T** — suite green: audit still green after regex tighten (no new false-positives), `rg "Type * name" scene/` now caught, `rg "legacyHandleCache|byObject_" render/` already 0 after `T7.6`, `deps_pinned` fails loudly if root `CMakeLists.txt` loses `GIT_TAG` even with anchor file present.

**G** — audit green, `comment_tag_context` still PASS, no `__never_matches` re-introduced.

### T8.6: Build/export & audit defaults hardening (CRITICAL C14/C15, HIGH H06/H09)

**D** — `tools/audit.sh:62` `SOURCE_DIRS="${AUDIT_SOURCE_DIRS:-...}"` default masks missing `source tools/env.sh` → change to `if [ -z "${AUDIT_SOURCE_DIRS:-}" ]; then echo "ERROR: source tools/env.sh first" >&2; exit 2; fi` (R15 fail-loud, `AGENTS.md`). `CMakeLists.txt:58` `find_package(glfw3/glm/spdlog/json QUIET)` before `FetchContent` bypass → either delete `find_package` or add version equality `find_package(glfw3 3.4 EXACT REQUIRED)` + audit `deps_pinned_no_branch` version check. `examples/CMakeLists.txt:104` delete `LINKER:--unresolved-symbols=ignore-all` (reveal broken `find_dependency` export), fix `app/CMakeLists.txt:28` `re_imgui PUBLIC glfw` leak → `PRIVATE` or `$<BUILD_INTERFACE:glfw>`, `core/CMakeLists.txt:45` already `PRIVATE glad` stays. `tools/env.sh:10-15` `LSAN_OPTIONS` cwd brittle `tools/lsan.supp` relative → `ROOT="$(cd "$(dirname "$BASH_SOURCE")"/.. && pwd)"` absolute. `utils/CMakeLists.txt:42` `AUDIT_SOURCE_DIRS` grey-zone doc → already in `tools/env.sh:6` `utils test_utils` present from start (keep). `examples/` intentionally NOT in `AUDIT_SOURCE_DIRS` per `README.md:33` but `no_secrets` still scans via built-in — document waiver.

**T** — suite green: `AUDIT_SOURCE_DIRS="" tools/audit.sh` exits 2 not 0, `examples/minimal` build without `--unresolved-symbols` still links (or fails loudly revealing missing `find_dependency`), `LSAN_OPTIONS` correct when sourced from `build/` subdir, `re_app` consumer no longer inherits `-Werror` via `INTERFACE re_project_warnings` (`app/CMakeLists.txt:54` `PUBLIC` → `PRIVATE` or `$<BUILD_INTERFACE>`).

**G** — suite green, audit green, `install(TARGETS re_core ...)` `RenderEngineConfig.cmake` `find_dependency` still green via `examples` probe.

> **Note V6:** `V6 T6.1/T6.2` (dumb layers + techniqueOrder) subsumed by `V7 T7.5` — kept for archive traceability but gates in `T7.5` are binding. `V6 T6.3/T6.4` (bounded harness + long-lived) remain independent and execute after `V7 T7.5` green.

## Definition of Done — V7/V8 (updated)

- [ ] All `V7` 9 gates + `V8` 6 gates green; full suite green `N>=3` on `llvmpipe` at final task
- [ ] `audit green` with tightened `deps_pinned`/`rhi_ownership`/`broker_per_type`/`no_per_target_sanitize`/`ownership_raw_ptr_*` floors (no `__never_matches` placeholders)
- [ ] `Store` + `Engine` IO depolluted (`grep -c "loadObjMesh" scene/ include/ ==0`, `re_io` not linked to `re_scene`/`re_engine`)
- [ ] Single `drawLayer` path (`grep -c "render(" render/*_renderer.hpp ==0`), single OIT via `ViewCompositor::captureTransparents`
- [ ] Depth per-layer/technique (`grep -c "depthTest" scene/view.hpp ==0`), heterogeneous `VolumeSlice+Mesh` depth-correct 1/255
- [ ] Dumb `LAYER_0..7` + `techniqueOrder` + scoped `priority` + `stable_sort` (`grep -c "LayerMask" scene/ ==0`)
- [ ] Unified content-hash dedup (`grep -c "byObject_\|legacyHandleCache" render/ ==0`, `grep -c "AssetStore" broker/ ==0`)
- [ ] View/Synchronizer split (`wc -l scene/view.hpp <70`, `wc -l broker/view_synchronizer.cpp <220`)
- [ ] `core/rhi/gl/` RHI anchor (`rhi_ownership` active, `render/` no `glad`)
- [ ] Loader hardening + canonical hash + EGL isolation + generation contract + build defaults (V8 `T8.1-T8.6` `1/255|1e-6|BudgetExceeded|152 MB` evidence)

