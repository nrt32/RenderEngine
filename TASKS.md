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
