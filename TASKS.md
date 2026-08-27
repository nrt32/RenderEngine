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

### FR → T traceability (regression — no new FRs, V3 preserves V1/V2 gates)

V3 has **no new FRs** (2026-08-23 direction) — every active `T1..T19` is a review follow-up preserving the 20 FRs below via regression lock R3. `COMPLETED_TASKS.md` V1 `T1..T16` + V2 `V2-T1..V2-T8` are the original gates; the table below links each FR to its **regression T** (last T that touched that path) and its **original V1/V2 gate** for audit. **Active draft `T1..T4` (next iteration):** `FR:none new` — each preserves the FRs via `R3` suite-green regression (no weakening); explicit `Active T` column below shows where the draft re-verifies the path (e.g. `T1` layerMask preserves `FR-render.5/6` volume/plane technique order, `T2` bounded run preserves `FR-app.1` smoke). Suite green = all 20 FR constants still asserted via full-suite regression gate per R3; no T weakens an FR gate. Original V1 gates remain the binding acceptance per `COMPLETED_TASKS.md`. **R4 evidence rule (spec-review #5):** every `T` — even infra `T3` `FpsCounter` (`fps==1/delta` within `1e-3` + overlay `1/255` probe) — asserts an **explainable analytic count** (typed null vs UB, `grep -c` 0/1, spy 2→1, `640×480=152 MB` via `w*h*16*32`, `sample(0.5)==0.5±1e-6`), never `non-empty/non-black/>0`; `T3` `fps==1/delta` is the analytic evidence.

| FR | Description (tolerance) | Regression T (V3) | Active T (T1..T4 draft) | Original gate | Acceptance constant |
|---|---|---|---|---|---|
| FR-io.1 | OBJ loader — vertex/index/AABB vs golden | T17 (via T5 infra) | T1 (layer technique-priority keeps Mesh load green) | V1 T4 | bunny `v` hand-count, `AABB` exact |
| FR-io.2 | NRRD loader — dims + corner voxels | T15 (via T13) | R3 (NRRD dims ≤128³, T13 in previous iteration, preserved) | V1 T5 | dims ≤128³, corner values exact |
| FR-io.3 | Image loader (stb) — dims + corner/center | T17 (via T13 VG) | R3 (image dims, preserved) | V1 T4 | `w*h*ch` + probe pixels |
| FR-io.4 | Loaders reject malformed → typed error | T12 (VG5) + T13 (BudgetExceeded) | R3 (typed ErrorDomain::Io, T12 VG5 in previous, preserved) | V1 T4/T5 | `ErrorDomain::Io` code, `errno ERANGE` |
| FR-data.1 | Mesh face normal analytic | T17 (via T7) | T1 (layer does not alter face normal) | V1 T4 | cross-product within 1e-6 |
| FR-data.2 | Mesh AABB exact | T17 (via T7) | T1 (AABB untouched) | V1 T4 | golden `min/max` exact |
| FR-data.3 | VolumeDataset trilinear vs 8 corners | T7 (preserved via T6 helpers) | T1 (trilinear via helpers preserved) | V1 T5 | interpolant within 1e-6 |
| FR-vol.1 | TransferFunction control points → RGBA | T18 (TF clamp) | R3 (TF control points, T18 in previous, preserved) | V1 T6 | exact at points, ramp 1e-6 |
| FR-vol.2 | Ray-cast compositing front-to-back | T10 (via T8 OIT docs) | R3 (compositing preserved) | V1 T6 | alpha-blend within 1e-6 |
| FR-vol.3 | Ray/AABB step positions analytic | T10 (via T8) | R3 (ray/AABB preserved) | V1 T6 | step positions analytic |
| FR-core.1 | RAII GL objects no errors/leaks | T4/T5 + T11 sanitizers | T1 (CompositeKey+REContext via T2 harness not GL) | V1 T3 | `GL_NO_ERROR` + ASan clean |
| FR-core.2 | ShaderProgram diagnostics `ERROR: 0:7` | T12 (VG1) | R3 (shader ERROR:0:7 preserved, T12 VG1 in previous) | V1 T3 | golden substring `glibberish` line 7 |
| FR-render.1 | MeshRenderer center pixel vs analytic | T10 (RI5 hoist) | T1 (Mesh layer priority 3 vs Volume 0, 1/255) | V1 T7 | center pixel within 1/255 |
| FR-render.2 | OIT depth-sorted composite | T8 (cost table) + T17 | T1 (OIT contour layer 5 vs mesh 3 + technique priority) | V1 T10 | 1/255 at 3 probes, spy count |
| FR-render.3 | OIT auto-engage on transparent | T8 + T14 (variant collapse) | T1 (isTransparent + layer cull + mask) | V1 T7/T10 | `isTransparent` + spy |
| FR-render.4 | SliceRenderer verts on plane ε=1e-4 | T10 (RI2 eps) | T1 (MeshSlice layer 4 vs Contour 5) | V1 T11 | distance ≤ ε |
| FR-render.5 | PlaneRenderer textured quad 1/255 | T7 (via T17) | T1 (Plane layer 2, technique priority 2) | V1 T8 | corner/center within 1/255 |
| FR-render.6 | VolumeRenderer ray-cast synthetic 1/255 | T6/T7 (asset handles) | T1 (Volume layer 0, technique priority 0) | V1 T9 | center pixel analytic 1/255 |
| FR-app.1 | Samples exit 0 + no sanitizer (smoke) | T15 (GlfwRuntime) + T17 | T2 (bounded 20-frame smoke exit 0, FR-app.1) | V1 T12/T13 | exit code 0, timeout |
| FR-app.2 | MPR 2×2 grid 1280×960 / 640×480 + axis convention | T12 (via T13) | R3 (MPR 2×2 grid preserved, T1 layerMask not grid) | V1 T14 | viewport dims exact, per-axis probe |
| FR-app.3 | MPR contour 90% within 2 px + 3D view | T17 (contour GPU, via T12 overlay) + T12 (View/overlay) | T1 (contour L5 vs slice L1, 90% within 2px) | V1 T15 | 90% within 2 px, 1/255 at probe (`tests/t*_contour*`) |

---

## V3 backlog — EMPTY (V4 19/19 green, archived 2026-08-28)

All 19 review follow-up tasks (T1–T19, dependency-ordered) have been completed and archived to `COMPLETED_TASKS.md` V4. Next iteration will be planned via `/loop-init`.

> **Naming:** archived backlog was `T1..T19` (foundations → REContext → pair-key → ... → View lights). Next `T1..Tn` will be assigned per new iteration.

## V3 documentation map (T-map, R9) — draft T1..T4 (next iteration)

| Task | Spec alias | Docs updated in the same commit |
|---|---|---|
| T1 | §3.1/§10 | `docs/spec/modules.md` (`View` `layerMask` + `SceneObject` `layer`), `scene/layer.hpp`, `scene/view.hpp`, `scene/object.hpp`, `scene/composite_key.hpp`, `docs/spec/persistence.md` (`CompositeKey{layer,layerMask}` + `FieldId::Layer` + `dirtyFieldsSince`) |
| T2 | §8 | `app/sample_harness.*` (`runInteractive`/`runBounded` dual mode), `docs/samples.md` (run modes) |
| T3 | §5 | `app/fps_counter.*` + `app/sample_harness.*` (overlay `FpsCounter`), `docs/samples.md` (FPS) |
| T4 | §3.1/§11 | `app/camera_controller.*` (`CameraController` + `CameraBindings`), `app/sample_harness.*` (poll+`WantCaptureMouse` + `View::mutateCamera`), `docs/samples.md` (controls) |

> **Naming:** next backlog `T1..T4` — `COMPLETED_TASKS.md` V4 `T1..T19` archived.

---

## Next iteration draft backlog — explicit layering + standalone + FPS + camera (upcoming `T1..T4`, spec §3.1/§10/§11, Option C)

### T1: Explicit 64-layer layering — enum `Layer` + `LayerMask` + technique-priority tie-break (Option C)

**D** — Every scene object carries `Layer layer{Layer::L0_Background}` (`scene/layer.hpp` enum `Layer : uint8_t { L0_Background=0 … L63_OverlayTop=63, Count=64 }`, `using LayerMask = uint64_t; 1ULL<<layer`). `scene::View` gains `LayerMask layerMask{~0ULL}` (all on) + `setLayerMask(mask)` bumping `layerMaskGen` (+ `View::generation`). `SceneObject` family (`MeshObject` etc.) gains `Layer layer` + `setLayer()` bumping object `generation`/`layerGen` (`FieldId::Layer` + `CompositeKey` includes `layer`/`layerMaskGen`). Rendering is **layer → technique-priority**: `ViewSynchronizer` groups translated `Re*` objects by `layer` ascending, then by fixed technique priority `Volume(0) → VolumeSlice(1) → Plane(2) → Mesh(3) → MeshSlice(4) → Contour(5) → PointCloud/Grid/Axes(6)` — so `itemIds` insertion order is irrelevant at 10k objects; two `Mesh` objects on `L5` are same-layer same-technique and rely on `Z-test` (depth-on) or stable overdraw (contour). Per-view `layerMask & (1ULL<<layer)` culls invisible layers `O(visible layers)` without removing objects. Count `32→64` fits one `uint64_t`; 64 keys cover future overlays. Per-object `layer` is primary; per-view override map `View::layerOverrides` is deferred (empty by default) — sharing one `MeshObject` at two different layers would duplicate the store entry (cheap `AssetRef` share) until override proves needed. **Size note (review #4 + #9):** `FieldId::Layer` + `CompositeKey{layer,layerMask}` + `dirtyFieldsSince` update are part of this task; cache invalidation on `setLayer`/`setLayerMask` is verified via `dirtyFieldsSince` (not just rendering).

**Depends:** none (foundational scene value type; `T2` harness independent; `docs/spec/persistence.md` `CompositeKey` updated here).

**FR:** none new — deterministic layer order replaces `itemIds` insertion order; regression `FR-render.*`/`FR-app.*` still green (MPR `VolumeSlice L0 → Contour L1`).

**T** — gate: two objects on same `L5` but different techniques render in priority order independent of `itemIds` insertion order (swap `itemIds` → same image within 1/255); mask hides a layer (`layerMask &= ~(1ULL<<L1)` → contour disappears, volume alone within 1/255); `grep -c "enum class Layer" scene/` == 1.

**G** — suite green, audit green, `layerMask` hidden-layer still green. **Size waiver:** `T1` adds enum + `View`/`Object` fields + `CompositeKey`/`FieldId` + `ViewSynchronizer` grouping; 2-phase gate within same task — Phase A `scene::Layer` enum + `View`/`Object` value types, Phase B `ViewSynchronizer` priority grouping + `layerMask` cull; each phase `grep -c` analytic before next.

### T2: Standalone samples — unbounded run with headless gate preservation

**D** — `app/sample_harness` gains dual mode: default `runInteractive()` loops `while(!shouldClose())` until user closes; gate path `runBounded(maxFrames)` / `run(maxFrames)` stays for `RE_SAMPLE_MAX_FRAMES` headless CI (FR-app.1). `runSample` helper dispatches on env var presence. Samples' `main()` switches to unbounded by default.

**T** — suite green (automated): headless `RE_SAMPLE_MAX_FRAMES=20` smoke exits 0 under Xvfb (always); `grep -c "runInteractive" app/sample_harness.hpp` == 1 && `grep -c "runBounded" app/sample_harness.hpp` == 1 (both paths present); interactive path is not a gate — gate is the bounded 20-frame exit-0 + no sanitizer reports (FR-app.1).

**G** — suite green, audit green. **Depends:** none (harness dual-mode is first; `T1` layering independent — file order `T1→T2` is convenience, could be swapped; `T3`/`T4` depend on `T2`).

### T3: FPS counter — all samples via harness overlay

**D** — `app/FpsCounter` (`std::chrono::steady_clock`, 0.5s sliding window) owned by `SampleHarness`, ticked each frame before overlay; overlay adds `Text("FPS: %.1f (%.1f ms)", fps, ms)` always-on (single-site harness change covers all 6 samples).

**T** — suite green (automated): headless `RE_SAMPLE_MAX_FRAMES=20` smoke still exits 0; unit test `FpsCounter` sliding average over 0.5s window equals `1/delta` within 1e-3 (analytic, not visual); headless FBO capture of `SampleHarness` overlay region probe `expectPixel(overlayRect{10,10,120,20}, Text("FPS"))` matches golden `data/fixtures/font_atlas_golden.rgba` sample within 1/255 (proves text is rasterized); `grep -c "FpsCounter" app/` == 1 definition.

**G** — suite green, audit green. **Depends:** `T2` (harness `runInteractive`/`WantCaptureMouse` guard).

### T4: Camera interaction — pan/rotate/zoom, configurable, 3D views only

**D** — `app/CameraController` + `CameraBindings{ rotateButton=LMB, panButton=RMB, zoomButton=MMB/wheel, modifiers, rotateSpeed, panSpeed, zoomSpeed }` plain struct. Harness polls `glfwGetMouseButton/CursorPos/Scroll` each frame before `renderFrame`, forwards to controller when `!ImGui::GetIO().WantCaptureMouse`; controller calls `View::mutateCamera([&](Camera& c){ c.rotate(...); })` so `viewGen` bumps and broker re-translates only dirty fields. Wired in `mesh/slice/volume/oit/mpr-3D` (plane + MPR 2D orthographic slice views skip it).

**T** — drag `rotate` updates `viewMatrix` deterministically (±1e-6 vs analytic orbit); `WantCaptureMouse=true` guard: `drag(10px)` with `WantCaptureMouse=true` leaves `viewMatrix` unchanged within `1e-6` vs pre-drag (delta 0 ±1e-6) and `position` unchanged, while same drag with `WantCaptureMouse=false` yields analytic `orbit(10px)` within `1e-6`; gate runs `N>=3` via offscreen fixture (3× `ctest Passed`); `grep -c "CameraController" app/camera_controller.hpp` == 1; gate bounded run with no input still green. **Depends:** `T2` (harness).

**G** — suite green, audit green.

## Definition of Done — next iteration (T1..T4 draft)

- [ ] All 4 task gates green; full suite green on a clean tree at T4.
- [ ] `suite green N>=3` where GL-touching (`T1` layerMask, `T3` overlay FBO, `T4` camera viewMatrix); `audit green` with `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` (incl. `test_utils` empty until T18, harmless pre-existing) — `tools/audit.sh` PASS
- [ ] `ASan+UBSan clean` on all `re_*` libs (`re_project_sanitizers` on 9 libs, proven before first GL gate per spec-review #5) + samples exit 0 under `xvfb` (`RE_SAMPLE_MAX_FRAMES=20` bounded, `FR-app.1`)
- [ ] `LICENSE` per dataset dir (`data/meshes/LICENSE`, `data/volumes/LICENSE` enumerated via `test -f` as in `COMPLETED_TASKS.md:264` V1 T2)
- [ ] `R9` doc-map: `git diff --name-only` at T4 includes `docs/spec/modules.md` + `scene/layer.hpp` (T1), `app/sample_harness.*` (T2), `app/fps_counter.*` (T3), `app/camera_controller.*` (T4) per rows above
- [ ] `R3` regression lock: `FR-io.*`/`FR-data.*`/`FR-vol.*`/`FR-core.*`/`FR-render.*`/`FR-app.*` still green via full-suite regression (20 FR table above, no weakening)
- [ ] `R4` evidence: every T asserts explainable constant (analytic `1/255`, `1e-6`, `152 MB` via `w*h*16*32`, `1/delta` within `1e-3`, not `non-empty`/`visual`)
