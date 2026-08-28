# TASKS — RenderEngine

Active backlog is the **V5 extensibility & visualization-reuse iteration** (Sr. Architect review 2026-08-28 — 17 tasks, `T1..T17` below; pure implementation, no new FRs — OpenGL/C++/CMake and Mesh/Volume/Plane/Slice/OIT/MPR requirements remain locked per user direction 2026-08-23; implementation only). The completed sequential loops are archived in `COMPLETED_TASKS.md`: `T1–T16` (V1, 0.1.0) + `V2-T1–V2-T8` (V2) + `V3 T1..T23` (pure-redesign) + `V4 T1..T19` (review batch) (all green, archived 2026-08-28). See SPEC.md for FRs, §9 for the V2 archive / §9.1 for the V3 roadmap, §6 for guardrails, NAMING_CONVENTIONS.md for style.

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

V5 has **no new FRs** (2026-08-28 direction — same as V3/V4) — every active `T1..T17` is an extensibility/boilerplate follow-up preserving the 21 FRs below via regression lock R3 + **explicit active V5 T re-verification** (every row has ≥1 V5 `T` in `Active T` column — not `R3` alone). `COMPLETED_TASKS.md` V1 `T1..T16` + V2 `V2-T1..V2-T8` are the original gates; `V3/V4` are the pure-redesign/review archives; the table below links each FR to its **regression T** (last T that touched that path) and its **original V1/V2 gate** for audit. **Active V5 `T1..T17`:** `FR:none new` — each preserves the FRs via `R3` suite-green regression (no weakening) **and** an explicit active T that re-verifies the path (e.g. `T8` 8-layer mask preserves `FR-render.5/6` volume/plane technique order, `T3` bounded `renderViews` preserves `FR-app.1` smoke, `T15` light facade preserves `FR-render.1` Phong headlight when `lights` empty, `T11` `256³ BudgetExceeded` + `128³ byte-identical` preserves `FR-io.2` NRRD dims). Suite green = all 21 FR constants still asserted via full-suite regression gate per R3; no T weakens an FR gate. Original V1 gates remain the binding acceptance per `COMPLETED_TASKS.md`. **R4 evidence rule (spec-review #5):** every `T` — even infra `T12` `FpsCounter` (`fps==1/delta` within `1e-3` + overlay `1/255` probe) — asserts an **explainable analytic count** (typed null vs UB, `grep -c` 0/1, spy 2→1, `640×480=152 MB` via `w*h*16*32`, `sample(0.5)==0.5±1e-6`), never `non-empty/non-black/>0`; `T12` `fps==1/delta` is the analytic evidence (note: after T3/T4 swap, harness is `T3`, offscreen is `T4` — `T12` is FPS).

| FR | Description (tolerance) | Regression T (V4) | Active T (V5 T1..T17) | Original gate | Acceptance constant |
|---|---|---|---|---|---|
| FR-io.1 | OBJ loader — vertex/index/AABB vs golden | T17 (via T5 infra) | T7 (loadMesh facade) + T1 (layer technique-priority keeps Mesh load green) | V1 T4 | bunny `v` hand-count, `AABB` exact |
| FR-io.2 | NRRD loader — dims + corner voxels | T15 (via T13) | T11 (`256³` tiled via `core::Caps` within 1/255 + `128³` byte-identical streaming, No cap) + T7 (loadVolume) | V1 T5 | sample `128³` dims ≤128³ exact; `256³` tiled 1/255 (No cap streaming via `core::Caps`, `BudgetExceeded` only probe-fail) |
| FR-io.3 | Image loader (stb) — dims + corner/center | T17 (via T13 VG) | T7 (loader facade) + T4 (offscreen parity vs `Window` path) | V1 T4 | `w*h*ch` + probe pixels |
| FR-io.4 | Loaders reject malformed → typed error | T12 (VG5) + T13 (BudgetExceeded) | T10 (`Result::andThen` preserves `ErrorDomain::Io` code) + T11 (`BudgetExceeded` code) | V1 T4/T5 | `ErrorDomain::Io` code, `errno ERANGE` |
| FR-data.1 | Mesh face normal analytic | T17 (via T7) | T5 (`MeshObject{GeometryKind}` collapse preserves cross-product within 1e-6; gate `T5` pixel parity 1/255 + analytic unit `EXPECT_NEAR(cross, expected, 1e-6)`) + T1 (layer does not alter face normal) | V1 T4 | cross-product within 1e-6 |
| FR-data.2 | Mesh AABB exact | T17 (via T7) | T5 (collapse preserves AABB `min/max` exact golden; gate `T5` `AABB` exact + `T6` single-map) + T1 (AABB untouched) | V1 T4 | golden `min/max` exact |
| FR-data.3 | VolumeDataset trilinear vs 8 corners | T7 (preserved via T6 helpers) | T6 (single-map store preserves trilinear interpolant within 1e-6; gate `T6` `sample(0.5)==0.5±1e-6`) + T7 (builder) | V1 T5 | interpolant within 1e-6 |
| FR-vol.1 | TransferFunction control points → RGBA | T18 (TF clamp) | T11 (TF preserved via volume fallback path; gate `T11` weighted-blend uses TF ramp 1e-6) + T7 (builder uses TF) | V1 T6 | exact at points, ramp 1e-6 |
| FR-vol.2 | Ray-cast compositing front-to-back | T10 (via T8 OIT docs) | T11b (weighted-blended fallback preserves compositing math within 1e-6) + T8 (technique priority) | V1 T6 | alpha-blend within 1e-6 |
| FR-vol.3 | Ray/AABB step positions analytic | T10 (via T8) | T11 (No cap tiled via `core::Caps` `maxTexture3DSize` probe) + T8 | V1 T6 | step positions analytic |
| FR-core.1 | RAII GL objects no errors/leaks | T4/T5 + T11 sanitizers | T12 (`core/re_context.hpp` alias + FPS standalone preserves `GL_NO_ERROR` via `REContext` spy, ASan clean) + T4 (offscreen vs window parity proves no leak) + T2 (PRIVATE glad firewall) | V1 T3 | `GL_NO_ERROR` + ASan clean |
| FR-core.2 | ShaderProgram diagnostics `ERROR: 0:7` | T12 (VG1) | T12 (`core/re_context.hpp` alias cleanup preserves `ERROR:0:7`) + T10 (`Result` domain) | V1 T3 | golden substring `glibberish` line 7 |
| FR-render.1 | MeshRenderer center pixel vs analytic | T10 (RI5 hoist) | T1 (Mesh layer priority 4 vs Volume 1, 1/255) + T15 (empty `lights` preserves headlight) | V1 T7 | center pixel within 1/255 |
| FR-render.2 | OIT depth-sorted composite | T8 (cost table) + T17 | T1 (OIT contour layer 6 vs mesh 4 + technique priority) + T11 (fallback parity) | V1 T10 | 1/255 at 3 probes, spy count |
| FR-render.3 | OIT auto-engage on transparent | T8 + T14 (variant collapse) | T1 (isTransparent + layer cull + mask) + T8 (mask hides layer) | V1 T7/T10 | `isTransparent` + spy |
| FR-render.4 | SliceRenderer verts on plane ε=1e-4 | T10 (RI2 eps) | T1 (MeshSlice layer 5 vs Contour 6) + T8 (priority orthogonal) | V1 T11 | distance ≤ ε |
| FR-render.5 | PlaneRenderer textured quad 1/255 | T7 (via T17) | T1 (Plane layer 3, technique priority 2) + T4 (offscreen vs window) | V1 T8 | corner/center within 1/255 |
| FR-render.6 | VolumeRenderer ray-cast synthetic 1/255 | T6/T7 (asset handles) | T1 (Volume layer 1, technique priority 1) + T4 (offscreen) | V1 T9 | center pixel analytic 1/255 |
| FR-app.1 | Samples exit 0 + no sanitizer (smoke) | T15 (GlfwRuntime) + T17 | T3 (bounded `renderViews` + `run(maxFrames)`) + T4 (offscreen smoke) | V1 T12/T13 | exit code 0, timeout |
| FR-app.2 | MPR 2×2 grid 1280×960 / 640×480 + axis convention | T12 (via T13) | T3 (`renderViews` preserves MPR viewport dims exact) + T4 (offscreen parity) | V1 T14 | viewport dims exact, per-axis probe |
| FR-app.3 | MPR contour 90% within 2 px + 3D view | T17 (contour GPU, via T12 overlay) + T12 (View/overlay) | T1 (contour L6 vs slice L2, 90% within 2px) + T8 (layer override) | V1 T15 | 90% within 2 px, 1/255 at probe (`tests/t*_contour*`) |

---

## V3 backlog — EMPTY (V4 19/19 green, archived 2026-08-28)

All 19 review follow-up tasks (T1–T19, dependency-ordered) have been completed and archived to `COMPLETED_TASKS.md` V4. Next iteration will be planned via `/loop-init`.

> **Naming:** archived backlog was `T1..T19` (foundations → REContext → pair-key → ... → View lights). Next `T1..Tn` will be assigned per new iteration.

## V5 documentation map (T-map, R9) — V5 T1..T17 + T8b, T11b (19 sessions, T8→T8+T8b DepthConfig split per Q1, T11→T11+T11b Caps split per Q2)

| Task | Spec alias | Docs updated in the same commit |
|---|---|---|
| T1 | §3 | `include/render_engine/engine.hpp`, `docs/engine.md` (facade), `README.md` (minimal) |
| T2 | §8 | `CMakeLists.txt`, `cmake/RenderEngineConfig.cmake.in`, `core/CMakeLists.txt` (PRIVATE glad) |
| T3 | §3/§11 | `app/frame_loop.hpp`, `app/imgui_overlay.hpp`, `app/sample_harness.*` (decoupled), `docs/samples.md` |
| T4 | §3/§8 | `core/offscreen.hpp`, `render/offscreen.hpp`, `docs/render.md` (offscreen) |
| T5 | §3 | `scene/objects/*.hpp` collapsed, `scene/iscene_object.hpp`, `broker/*mapper.*` |
| T6 | §10 | `scene/store.hpp`, `scene/store.cpp` (single-map + `kindIndex_`) |
| T7 | §3.1 | `scene/builders.hpp`, `scene/store.hpp` (loaders), `app/*_sample.cpp` (trimmed) |
| T8 | §10/§3.1 | `scene/layer.hpp`, `scene/view.hpp` (uint32_t LayerMask `1u<<`), `scene/object.hpp`, `broker/view_synchronizer.*` (8 layers + override, technique priority orthogonal, ε=1e-4, 90% within 2px) |
| T8b | §3.1/§10 | `scene/depth_config.hpp`, `scene/view.hpp`, `render/view.*`, `render/view_target.*`, `core/re_context.*` (DepthConfig value object + DepthMode) |
| T9 | §3.1 | `scene/camera_controller.hpp`, `app/glfw_camera_interactor.hpp`, `docs/samples.md` (controls) |
| T10 | §5 | `data/result.hpp`, `docs/spec/nfr.md` (Result ergonomics, `andThen`/`orElse`) |
| T11 | §7 | `io/volume/nrrd_volume_loader.cpp`, `render/volume_renderer.cpp`, `core/caps.hpp`, `core/caps.cpp` (No cap streaming tiled via `core::Caps` `maxTexture3DSize`) |
| T11b | §7/§12 | `render/linked_list_oit.cpp` (weighted fallback via `core::Caps` `ssboAtomics`, `w*h*16*32` 152 MB) |
| T12 | §5/§3 | `utils/fps_counter.hpp`, `core/re_context.hpp` (`draw.hpp` alias), `core/CMakeLists.txt` |
| T13 | §3/§8 | `examples/minimal.cpp` (==22, 1/255 smoke), `README.md`, `docs/engine.md`, `docs/spec/persistence.md` (serialize) |
| T14 | §6 | `tools/audit.rules`, `tools/audit.sh` (drift guards `≤80`→`==42`), `docs/spec/guardrails.md` |
| T15 | §12.3/§3 | `scene/light.hpp`, `include/render_engine/engine.hpp` (Engine lights Directional minimal), `docs/engine.md` (lights), `broker/light_mapper.*` (single Directional, Point/Spot stretch) |
| T16 | §11 | `broker/cached_mapper_base.hpp`, `broker/*_object_mapper.*` (dedup), `docs/spec/broker.md` (mapper cache) |
| T17 | §3/§6 | `docs/engine.md` (depth default via DepthConfig), `docs/spec/guardrails.md` (naming), `tools/audit.rules` (depth + naming drift guards) |

> **Naming:** next backlog `T1..T17` V5 + `T8b`/`T11b` splits → 19 sessions (`T1..T17` base + `T8b` DepthConfig + `T11b` OIT fallback, per Q1/Q2) — `COMPLETED_TASKS.md` V4 `T1..T19` archived; V5 active.

---

## V5 backlog — extensibility & visualization reuse (Sr. Architect review 2026-08-28 — 19 sessions, 17→19 via T8→T8+T8b DepthConfig per Q1 + T11→T11+T11b Caps per Q2)

> **Origin:** Sr. Architect review of the codebase as a reusable visualization library (GLFW accepted,
> boilerplate hostility, redundant classes, abstraction gaps). The four draft `T1..T4` (64-layer
> Option C / unbounded harness / harness-owned FPS / `app/` camera) are **superseded and refined here**
> per the review: 64 layers → 8 + per-view override now, unbounded harness default inverted, FPS made
> standalone, camera math extracted to `scene/`. All 17 tasks are **pure implementation, no new FRs**
> (regression lock R3); the 20 FR constants remain asserted via the full-suite gate per §"FR → T traceability".
> Archived draft `T1..T4` logic is preserved as `T8` (layering), `T3` (harness decoupling → `renderViews`/`FrameLoop`, supersedes draft T2), `T12` (FPS), `T9` (camera).
> Follow-up gaps `G1–G6` (light minimal, mapper cache dedup, layer/technique orthogonality, depth default, sample split, naming sweep) are folded as `T15..T17` below — same R3/R4 regime, no new FRs.

### T1: Engine facade — `viz::Engine` one-liner for visualization consumers (P0)

**D** — Publish `include/render_engine/engine.hpp` (`viz::Engine` or `re::viz::Engine`) that hides `SceneStore`/`Broker`/`AppContext`/`TranslateContext`/`CompositeKey`/`GenerationTracker` for the 80% case. Facade owns an `AppContext` + `SceneStore` internally and exposes: `Result<ObjectId> addMesh(path, mat4, Material)`, `addVolume(path/tf, mat4)`, `setView({rect,camera,ids})`, `Result<void> render(Framebuffer&)` / `render(windowFb)`, plus `appContext()`/`store()` accessors for advanced users (broker path stays). Single-site helper `Engine::createView` covers `fitPerspectiveViewToPixels` + `Rect` + `Camera` ceremony. No `CompositeKey` in public header.

**FR:** none new — facade forwards to existing `AppContext::bridge().sync/renderAll/presentAll` (broker path unchanged) so all `FR-render.*`/`FR-app.*` stay via R3.

**T** — gate: `Engine e; auto id = e.addMesh("data/meshes/bunny.obj", I, mat); e.setView({{0,0,800,600}, cam, {id}}); e.render(fb)` center pixel within 1/255 of direct `AppContext` path (`Engine` vs direct `AppContext` oracle, `N>=3` via offscreen fixture, analytic color not `>0`); `grep -c "class Engine" include/render_engine/` == 1; sample `mesh_sample` can be reduced to 20 lines via facade (kept as comment/example, not required).

**G** — suite green (`N>=3` for `Engine` vs direct pixel gate), audit green, `Engine` header in `include/` + `docs/engine.md` facade docs.

### T2: CMake install/export — make RE a real library (P0)

**D** — `CMakeLists.txt` + `cmake/RenderEngineConfig.cmake.in`: `install(TARGETS re_core re_scene re_broker re_render re_app EXPORT RenderEngineTargets)`, `write_basic_package_version_file` (semver 0.1), `install(EXPORT ...)`, `install(DIRECTORY include/ ...)`, `install(DIRECTORY scene/ TYPE INCLUDE FILES_MATCHING *.hpp)` only where needed. Privatize `re_core`'s `glad`/`glfw` linkage (`core/CMakeLists.txt:27` `PUBLIC` → `PRIVATE` with explicit downstream `target_link_libraries(render PRIVATE glad)` where `REContext` body needs it) so header firewall T5 is not leaked via `INTERFACE`. `FetchContent` deps become `find_package` fallbacks (not forced `FetchContent` in consumer). `re_project_sanitizers` `INTERFACE` not installed on Release.

**T** — gate: `cmake -S . -B /tmp/re_build && cmake --build /tmp/re_build -j && cmake --install /tmp/re_build --prefix /tmp/re_inst && test -f /tmp/re_inst/lib/cmake/RenderEngine/RenderEngineConfig.cmake` (path existence) plus `grep -c "add_compile_options.*-fsanitize" ==0` still and `grep -R "#include.*glad" core/*.hpp ==0` (firewall) **and analytic pins:** `grep -c "RenderEngineTargets" /tmp/re_inst/lib/cmake/RenderEngine/RenderEngineTargets.cmake ==1` && `grep -c "write_basic_package_version_file.*0.1" CMakeLists.txt ==1` (version `0.1` per `T2:D`) — `find_package` smoke moved to `T13` (no forward ref).

**G** — suite green, audit green, `cmake --install` reproduces.

### T3: Harness decoupling — `Window` + `ImGuiOverlay` + `FrameLoop` + bounded-run discipline (P0, supersedes draft T2 — note: draft was P2, now P0 because `T4` offscreen depends on it)

**D** — Split `app/sample_harness.*` (`SampleHarness::run(maxFrames)` `app/sample_harness.cpp:67`) into: `app/frame_loop.hpp` (`FrameLoop{ poll(), render(), present() }` free function `Result<void> renderViews(span<View>, SceneStore&, Framebuffer&)` callable without `Window`), `app/imgui_overlay.hpp` (optional overlay, not owned by loop), `core/window.hpp` stays. Keep **bounded `run(maxFrames)` as the sole public contract**; add `runInteractive()` as opt-in helper only — `runSample` dispatches via `sampleMaxFrames(kDefaultFrames)` (`app/sample_harness.hpp:191`) when `RE_SAMPLE_MAX_FRAMES` is set (CI bounded) and **defaults to bounded `kDefaultFrames` (e.g., 20) when the var is unset** — interactive `until shouldClose()` only via explicit `runInteractive()` opt-in. Samples' `main()` keeps bounded dispatch; harness never hangs CI when env var is forgotten (bounded default, not interactive). Remove `SampleHarness::initImGui` hard-coupling (`sample_harness.cpp:29` → overlay). This is the **prerequisite for T4 offscreen** — `renderViews` is the window-free render helper that `T4:renderOffscreen` will reuse.

**T** — gate: new `renderViews(views, store, fb)` renders without `Window` — center pixel within 1/255 of `SampleHarness` path (`N>=3` via offscreen fixture, analytic not `>0`); `RE_SAMPLE_MAX_FRAMES=20` smoke still exits 0 under Xvfb, and run without env var defaults to bounded `kDefaultFrames` (no hang); `grep -c "ImGui_ImplGlfw_InitForOpenGL" app/sample_harness.cpp ==0` (overlay owns it); **MPR `FR-app.2` preserved (via `renderViews` layout path):** window `1280×960` + four `640×480` viewports at `(0,0)/(640,0)/(0,480)/(640,480)` exact (within 1 px) + axis convention `T=Z, C=Y, S=X` per-view pixel probe (analytic, not visual).

**G** — suite green (`N>=3` for parity gate), audit green.

### T4: Headless/offscreen public API — server-side visualization (P0, depends on T3)

**D** — Promote `utils::OffscreenContext` (`utils/offscreen_context.hpp`) + `core::loadCoreGl` + `REContext::current().readRgba8` to public `core/offscreen.hpp` + `render/offscreen.hpp` API: `Result<Image> renderOffscreen(uint32_t w, uint32_t h, span<View> views, SceneStore& store)` (creates hidden context, owns `GlfwRuntime` ref, creates `View` FBOs, calls `T3:renderViews` + `ViewSynchronizer`+`ViewCompositor` without a `Window`, reads back via `REContext`). No `core/window.hpp` include in this path. `Window` remains for interactive samples; offscreen path is window-free. Depends on `T3:frame_loop` — fresh session for `T4` has proven `renderViews` to reuse.

**T** — gate: `renderOffscreen(640,480, {view{bunny}}, store)` center pixel within 1/255 of `Window`-path `View::render` oracle for same scene (offscreen vs window parity, `N>=3`); `grep -R "window\.hpp" render/offscreen.* ==0`; **MPR `FR-app.2` offscreen parity:** same `1280×960` / `640×480` viewport dims exact + axis probe via offscreen path (`N>=3`).

**G** — suite green (`N>=3`), audit green, no raw `glReadPixels` outside `core/re_context.cpp` (`grep -R glReadPixels -- core/ ==1, -- render/ ==0, -- utils/ ==0`).

### T5: Collapse mesh-backed object types — `MeshObject` + `GeometryKind` (P0)

**D** — 11 byte-identical headers (`scene/objects/cube_object.hpp` vs `sphere_object.hpp` diff 8 lines; pattern `AssetRef<Mesh> mesh; mat4 transform; MeshMaterialDesc presentation;` `scene/objects/*.hpp:36-40`) collapse to one `MeshObject` carrying `GeometryKind {Mesh, Cube, Sphere, Cylinder, Torus, Cone, Arrow, Grid, Axes, Capsule, PointCloud, Teapot}` or `ProceduralMesh` factory, plus the core six technique kinds (`Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour`). Keep `SceneKind` for technique dispatch only (6 values). `SceneFactory` + `REGISTER_SCENE_OBJECT` stays for truly new techniques (e.g., `StreamlineObject`), not for data-driven mesh variations. `SceneStore` 17 partitions → 6 partitions (next task `T6` tightens further to single-map — see T6). Migrate `broker/*ObjectMapper` for merged kinds to one mapper. **Sizing note (spec-review #12):** `T5` reduces 17→6 partitions but does not yet single-map; `T6` completes single-map — the intermediate 6-partition state is gated (see T5/G) so no churn vacates T5's gate. Sequential rewrite is intentional SRP: `T5` owns `GeometryKind` collapse, `T6` owns `store` template.

**T** — gate: adding `Sphere` no longer needs a new header — `MeshObject{ .geometryKind=Sphere }` via single `MeshObjectMapper` renders within 1/255 of old `SphereObject` path (pixel parity, `N>=3`); `grep -c "class SphereObject" scene/` == 0 after collapse (analytic count 0, not `>0`); `Broker::registeredTypes()` still contains 6 technique kinds.

**G** — suite green (`N>=3` for parity), audit green.

### T6: Store consolidation — single-map + `kindIndex_` (P0, depends on T5)

**D** — Replace remaining 6 hand-written `unordered_map<uint64_t, unique_ptr<ISceneObject>> meshObjects_ …` (`scene/store.hpp:331-351` after `T5`'s 17→6) + remaining `add*/remove*/get*` families with one `unordered_map<Id, unique_ptr<ISceneObject>> objects_` + `unordered_map<SceneKind, unordered_set<Id>> kindIndex_` (already existing `scene/store.hpp:357` secondary index). Provide templated `addObject<T>(T) -> Id`, `get<T>(Id)`, `remove(Id)` plus `objectsOfKind(SceneKind)` `O(kind)` via index (already `scene/store.hpp:126`). Remove 12 `*Count()` hand copies (`scene/store.hpp:186-202`) — keep `count(SceneKind)` + `totalObjectCount()`. One SRP template, not hand-copied families. **Builds on `T5`'s 6-partition gate** — `T5` gate's `Broker::registeredTypes()==6` remains true; `T6` gate checks single-map (`grep -c "meshObjects_\|sphereObjects_" ==0`).

**T** — gate: `Sphere` via `objectsOfKind(Mesh)` vs old partition parity — `count(Mesh)` unchanged; `grep -c "meshObjects_\|sphereObjects_" scene/store.hpp ==0` (analytic 0); `FR-data.*` store add/remove generation bump still analytic within 1e-6; suite green.

**G** — suite green, audit green; `grep -c "meshObjects_" scene/store.hpp ==0` still holds after `T7` (store single-map preserved — `T7` additive `loadMesh` only).

### T7: Loader facade + Scene/View builders — kill sample boilerplate (P1, 25–35 line ceremony, depends on T5+T6)

**D** — `scene/store.hpp`: `Result<ObjectId> loadMesh(path)` / `loadVolume(path)` that does `io::load*` + `register*Asset` + `add*Object` atomically (today `load → shared_ptr → MeshObject → add` 4 steps, 5/6 samples duplicate). Add `scene/builders.hpp`: `SceneViewBuilder{ ViewId, Rect }.withCamera(cam).withItems(ids).withClear(color).build() -> View` and `Objects::mesh(asset, mat4, mat) -> MeshObject` helpers; `PerspectiveFraming` (`app/sample_harness.hpp:83`) removed in favor of `Camera::perspectiveFromFraming(framing, aspect)` on `scene/camera.hpp`. `fitPerspectiveViewToPixels` (`app/sample_harness.cpp:169`) becomes `builder.applyLiveDims(w,h)` one call. **Depends on `T5` (`MeshObject{GeometryKind}`) + `T6` (single-map store) — builder targets `objects_` single-map API, not the deleted 17-partition API.**

**T** — gate: `store.loadMesh("data/meshes/bunny.obj")` returns `ObjectId` whose `View` center pixel within 1/255 of manual 4-step path (`N>=3`); sample `mesh_sample.cpp` boilerplate lines `applyLiveDims` duplicate count ≤1 site (`grep -c "applyLiveDims" app/*.cpp ==1` helper, not 6); `grep -c "PerspectiveFraming" app/` == 0 after removal; `grep -c "meshObjects_" scene/store.hpp ==0` still holds (single-map not reverted).

**G** — suite green (`N>=3` for loader parity), audit green; additive `loadMesh`/`loadVolume` + `builders.hpp` only, single-map preserved.

### T8: Simplified ordering — 8 layers + per-view override, technique priority orthogonal (P1, supersedes draft T1 64+deferred override, depends on T5+T6) — **split per Q1 per Sr. Architect: T8a ordering (this task) + T9 depth (next task, DepthConfig)**

**D** — Replace draft `T1` 64-layer `Layer : uint8_t {L0…L63, Count=64}` + `LayerMask uint64_t` + deferred `layerOverrides` (`TASKS.md:98` "duplicate entry" workaround) with **8 layers** `enum class Layer : uint8_t { Background=0, Volume=1, VolumeSlice=2, Plane=3, Mesh=4, MeshSlice=5, Contour=6, OverlayTop=7, Count=8 }` and `using LayerMask = uint32_t` (`1u<<layer`, future-proof, `0xFFu`). Every `SceneObject` (`MeshObject` etc. via `ObjectBase`) carries `Layer layer{Mesh}` + `setLayer()` bumping `generation`/`layerGen` (`FieldId::Layer` + `CompositeKey` includes `layer/mask`). `scene::View` carries `LayerMask layerMask{0xFFu}` + `setLayerMask()` + **`unordered_map<ObjectId, Layer> layerOverrides`** from day one (per-view override, `O(1)` lookup). `ViewSynchronizer` groups by `(layer, techniquePriority)` ascending — **Layer is visual stacking, TechniquePriority `Volume(0)…Contour(5)` is a separate orthogonal sort key** (G3 amendment): default is `Volume(0)→VolumeSlice(1)→Plane(2)→Mesh(3)→MeshSlice(4)→Contour(5)` but a per-view override can place `Contour` under `Mesh` without touching `Mesh.layer`. Per-view `layerMask & (1u<<layer)` culls without removing objects. **Depth is NOT in this task — see T9 `DepthConfig` (Sr. Architect: View owns DepthConfig value object, not raw bool on View, Renderer never allocates FBO).** Default stays color-only for deterministic llvmpipe gates (see T9).

**FR:** none new — deterministic layer order replaces `itemIds` insertion order; `MPR VolumeSlice L1 → Contour L6` preserved.

**T** — gate: two objects on same layer but different techniques render in priority order independent of `itemIds` swap (swap `itemIds` → same image within 1/255, `N>=3`); mask hides a layer (`layerMask &= ~(1u<<OverlayTop)` → overlay disappears, volume within 1/255); per-view override `layerOverrides[id]=Background` moves that object to background layer regardless of its global `layer` (override probe within 1/255); orthogonality probe: same `Layer=Mesh` for `VolumeSlice`+`Contour` still orders `VolumeSlice` before `Contour` by technique priority; **SliceRenderer `ε=1e-4` preserved:** cross-section vertices lie on plane within `ε=1e-4` (distance `|n·p+d| ≤1e-4`, `N>=3` analytic `PlaneDesc` via `SliceRenderer` geometry shader, same as `FR-render.4`); **MPR `FR-app.3` preserved:** contour `90% within 2 px` Euclidean vs analytic box+plane (`≥90%` pixels within 2 px of closed-form `plane∩mesh` curve, `N>=3`); `grep -c "enum class Layer" scene/` == 1 && `grep -c "layerOverrides" scene/view.hpp ==1` && `grep -c "LayerMask" scene/layer.hpp ==1` && `grep -c "0xFFu" scene/view.hpp ==1` (`uint32_t` + `1u<<` pinned).

**G** — suite green (`N>=3` for layer priority/mask/override/ε/contour), audit green.

### T8b: Depth opt-in — `View::DepthConfig` + `ViewTarget DepthMode` (P1, depends on T8, Sr. Architect: View owns DepthConfig value object, not Renderer)

**D** — **DepthConfig value object (SRP via composition, OCP for future `func/writeMask/clearDepth/stencil`):** `scene/depth_config.hpp` `struct DepthConfig{bool enabled{false}; float clearDepth{1.0f};}` (or inline in `scene/view.hpp`). `scene::View` holds `DepthConfig depthConfig` (default `enabled=false` — color-only deterministic) + `setDepthConfig(DepthConfig)` bumping `depthGen` (`FieldId::Depth`). `ViewTarget` `DepthMode::Enabled` (`GL_DEPTH_COMPONENT24` texture `core::Texture2D::uploadDepth` @ `GL_DEPTH_ATTACHMENT`) — creation asserts `glCheckFramebufferStatus==COMPLETE` **with** depth (fail-loud). `resize()` preserves `DepthMode`. `View::ensureTarget()` recreates only depth texture when `depthConfig.enabled` flips (View identity stable). Pass prologue `REContext::beginPass(depthConfig)` does `if(enabled){enableDepthTest; clearDepth 1.0} else disableDepthTest` once per `View::render` before `drawLayer` loop. `Engine` facade (`T1`) amends `createView/setView` to set `DepthConfig{true}` for mesh-containing views (documented divergence: low-level stays `false`, Engine defaults `true` for viz correctness, audit `engine_depth_default` `require_grep setDepthConfig(true)` in `include/render_engine/engine.hpp`). **Why View not Renderer:** Renderer is stateless `drawLayer` — has no `ViewTarget` size, would break `IRenderable` type-erasure + `ViewTarget` SRP; depth over mixed `VolumeSlice+MeshSlice` vs `Volume+Mesh` + OIT (`T19` opaque depth + transparent depth-off over same target) only View knows. `OIT` capture/composite `disableDepthTest` explicit on same `REContext` instance.

**T** — gate: depth-enabled `ViewTarget` `isComplete()==true` with `GL_DEPTH_ATTACHMENT` bound; flag flip `setDepthConfig({true})` → next `ensureTarget()` owns depth, `resize()` preserves; **near-wins 1/255:** two full-screen opaque quads anti-painter order (near FIRST, far LAST) at z=0 vs z=-1, orthographic `[-1,1]^2` 64×64, `depthConfig.enabled==true` → overlap pixel `{0,128,0}` (near wins) vs color-only `{128,0,0}` (far wins) — proves occlusion semantics within 1/255 (`docs/render.md` Depth gate); `grep -c "DepthConfig" scene/` ==1 && `grep -c "setDepthConfig(true)" include/render_engine/engine.hpp ==1` && `grep -c "DepthMode" render/` >=1.

**G** — suite green (`N>=3` depth gate, `GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6`), audit green, `docs/engine.md` depth default note + `tools/audit.rules` `engine_depth_default`.

### T9: Camera controller extraction — `scene/CameraController` pure math (P1, supersedes draft T4 `app/CameraController`)

**D** — Extract pure math `scene/camera_controller.hpp` `CameraController` (`onMouseDrag(dx,dy, button, modifiers) -> CameraDelta`, `onScroll(delta)`, `onKey`) with no `glfw*`/`ImGui` includes; `app/glfw_camera_interactor.hpp` adapter polls `glfwGetMouseButton/CursorPos/Scroll` each frame before `renderFrame` and forwards to controller when `!ImGui::GetIO().WantCaptureMouse` (`TASKS.md:126` `WantCaptureMouse` guard), then calls `View::mutateCamera([&](Camera& c){ c.rotate(...); })` so `viewGen` bumps and broker re-translates only dirty fields. `CameraBindings{ rotateButton=LMB, panButton=RMB, zoomButton=MMB/wheel, modifiers, rotateSpeed, panSpeed, zoomSpeed }` plain struct stays. Wired in `mesh/slice/volume/oit/mpr-3D`; plane + MPR 2D orthographic skip.

**T** — gate: `scene::CameraController` unit test: `drag(10px)` yields analytic `orbit(10px)` `viewMatrix` within 1e-6; `WantCaptureMouse=true` guard: same drag with `WantCaptureMouse=true` leaves `viewMatrix` unchanged (delta 0 ±1e-6) vs `false` yields analytic orbit; `grep -c "glfw" scene/camera_controller.hpp ==0` (no GLFW in `scene/`); bounded run with no input still green (`N>=3` via offscreen fixture).

**G** — suite green (`N>=3` for controller parity), audit green, `scene/` disposition still `render/`-free.

### T10: Result ergonomics — `map/andThen` + `RE_EXPECT` (P1)

**D** — `data/result.hpp:83` `Result<T,E>` today has UB on failed deref and verbose `if(failed()) return` ladders (`mpr_sample.cpp:269` triple). Add monadic `map(F)`, `andThen(F)`, `orElse`, `valueOr(default)` plus `RE_EXPECT(expr)` / `RE_TRY` macro that early-returns `Result` error with `__FILE__:__LINE__` provenance. Keep `[[nodiscard]]` and debug-trap on failed `operator*` (already T22). Document that `ErrorDomain` disambiguates numeric codes (already `data/result.hpp` domain tag).

**T** — gate: `loadMesh("data/fixtures/malformed.obj").andThen([](Mesh m){ return store.addMeshObject(m); }).map([](Id id){ return View{ids:{id}}; })` chain compiles and on malformed `malformed.obj` returns `Result.failed() && err.domain==ErrorDomain::Io && err.code==1 /*FileOpen==1 per io/mesh_loader.cpp:12*/` — code preserved within domain — plus `grep -c "andThen" data/result.hpp ==1 && grep -c "orElse" data/result.hpp ==1 && grep -c "map" data/result.hpp >=2` (analytic counts `==1`/`==1`/`>=2`, not `>0`; `BudgetExceeded` `code==7` distinct domain is `T11` `core::Caps` probe-fail, not `T10`).

**G** — suite green, audit green, no exception path introduced.

### T11: Volume large-data — tiling/bricking + `core::Caps` (P1, depends on `core::Caps` wrapper, Sr. Architect: No cap streaming via `core::Caps`)

**D** — **Volume bricking / cap lift (No cap streaming per Q4):** `io/volume/nrrd_volume_loader.cpp` `≤128³` gate is test convenience, not product limit; replace hard window with **No cap streaming** — any dims via `core::Caps` tiled/downsampled streaming (see `docs/spec/assets.md:14` `FR-io.2` `goals.md:50` `nfr.md:26`): `render/volume_renderer.cpp` checks `maxTexture3DSize` via **`core::Caps` `core/caps.hpp` `Caps{uint32_t maxTexture3DSize; bool ssboAtomics;}` cached `core::caps()`** ( `core/caps.cpp` calls `glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE)` / `glGetString` once until RHI lands, `TODO(RHI)` → `IRHIContext::capabilities()` after T10 `core/rhi/` per `docs/spec/nfr.md:25` `modules.md:34` ) and either downsamples or tiles `Texture3D` (tiled `1/255` within reference, not `BudgetExceeded` for `>128³` alone — `BudgetExceeded` only when `core::Caps` probe fails). **Depends on: `core::Caps` wrapper (no `IRHIContext` yet, `IRHIContext` is `(stretch)` T10; `T11` uses `TODO(RHI)` adapter).** **Sizing:** `T11` single phase ~60 lines loader + caps plumbing, single session.

**T** — gate: synthetic NRRD `256³` via `core::Caps` tiled load within 1/255 of reference `256³` tiled (analytic, not OOM) + valid `128³` still loads byte-identical `1/255`; `grep -c "core::caps\|Caps" render/volume_renderer.cpp >=1` && `grep -c "BudgetExceeded" io/volume/nrrd_volume_loader.cpp ==1` (only probe-fail path, not `>128³`).

**G** — suite green (`N>=3` tiled `256³` 1/255 + `128³` byte-identical), audit green, `maxTexture3DSize` via `core::Caps` (`grep Caps`).

### T11b: OIT weighted-blended fallback — `core::Caps ssboAtomics` (P1, depends on T11 `core::Caps`, Sr. Architect: `render/` never raw gl*)

**D** — **OIT fallback (No cap streaming, same Caps):** `render/linked_list_oit.cpp` `w*h*16*32` 152 MB @640×480 / 1 GB @1080p (`docs/spec/nfr.md:28`) abort is not visualization-grade; when caps probe reports `!ssboAtomics` (`core::Caps` `ssboAtomics` via `glGetString`/`GL_ATOMIC_COUNTER_BUFFER` until RHI, `TODO(RHI)` → `IRHIContext::capabilities().ssboAtomics`) or `w*h*16*32` over budget, fallback to weighted-blended OIT (`SPEC Q32` `!ssboAtomics → weighted-blended`, `docs/spec/open_questions.md:70`) and composite with weight. Typed error only when both linked-list and weighted fail. **Depends on `T11` `core::Caps` (reuses `maxTexture3DSize/ssboAtomics/152 MB` plumbing, no duplicate caps).** Single phase ~40 lines OIT fallback + caps reuse, single session.

**T** — gate: OIT over-budget `1920×1080` with `LinkedListOIT` forced over-budget → weighted-blended output center pixel within 1/255 of reference weighted blend (analytic, not opaque-only) `N>=3`; `grep -c "weighted" render/linked_list_oit.cpp ==1` (analytic `==1`, not `>=1`) && `grep -c "core::caps\|ssboAtomics" render/linked_list_oit.cpp >=1`.

**G** — suite green (`N>=3` weighted-blended 1/255), audit green, `ssboAtomics` via `core::Caps`.

### T12: FPS standalone + draw-header cleanup (P2 batch, depends on T2)

**D** — **FPS:** Move `app/FpsCounter` (`TASKS.md:118` draft `app/FpsCounter` owned by `SampleHarness`) to `utils/fps_counter.hpp` `utils::FpsCounter` standalone (`std::chrono::steady_clock`, 0.5s window, `tick()`, `fps()`, `ms()`); `app/sample_harness` queries it. **Draw header:** delete legacy `core/draw.hpp` vs `core/re_context.hpp` duality (`core/draw.hpp:1` façade delegating to `REContext::current()` vs `core/re_context.hpp:182 beginPass` single ledger) — keep one `core/re_context.hpp` header, `core/draw.hpp` becomes alias include or deleted. `REContext` single-writer discipline already T4, header duality remains. **Depends on `T2` (PRIVATE glad firewall) — preserves `grep PRIVATE glad` after header sweep.**

**T** — gate: `FpsCounter` unit `tick(16.6ms)` sliding average `fps==60±1e-3` (analytic `fps==1/delta` within `1e-3`, `delta=16.6ms → fps==60.24`, 0.5s window `N=30` samples `avg==60.24±1e-3`); headless `RE_SAMPLE_MAX_FRAMES=20` smoke still green; `grep -R "#include.*glad" core/*.hpp ==0` still and `grep -c "draw\.hpp" core/*.hpp ==0` or alias-only (no second ledger); **FR-core.2 preservation:** `ShaderProgram` malformed source with `glibberish` on line 7 via `loadSourceFile` → `Result.failed() && err.domain==ErrorDomain::Shader && msg.contains("ERROR: 0:7") && msg.contains("glibberish")` (golden substring `ERROR: 0:7` + `glibberish`, no crash, same as `tests/t3_core_gl_test.cpp` inline gate — `FR-core.2` re-verified, not `R3` alone).

**G** — suite green, audit green; `grep "PRIVATE" core/CMakeLists.txt | grep glad ==1` firewall not regressed (T2 `PUBLIC→PRIVATE` preserved) && `grep -c "add_compile_options.*-fsanitize" ==0` still.

### T13: Minimal example + versioned serialize docs (P2 batch, depends on T1+T2)

**D** — `examples/minimal.cpp` (20 lines) using `viz::Engine` facade (`T1`): `Engine e; auto id=e.addMesh("data/meshes/bunny.obj"); e.setView({{0,0,800,600}, camera, {id}}); e.render(windowFb);` — the first file a visualization project copies. `README.md` "Minimal example" section + `docs/engine.md` full facade docs. `SceneStore::serialize()` stabilization: `MaterialDesc`/`LightDesc` JSON via `nlohmann/json` (`CMakeLists.txt:117`) already, but `View` persistence via `CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash}` (`docs/spec/persistence.md:36`) not serialized — document versioned `SceneStore::serialize()` JSON with `Version` migrations and `View` wire format. **Depends on `T1` (facade header) + `T2` (`cmake --install` + `RenderEngineConfig.cmake` for `find_package` probe).**

**T** — gate: `examples/minimal.cpp` builds via installed config (`cmake -S examples -B /tmp/min && cmake --build /tmp/min` green); `grep -c "Engine" examples/minimal.cpp ==1` && `wc -l examples/minimal.cpp ==22` (committed exact `22`, not `<=30` cap) && **`examples/minimal` smoke via `renderOffscreen` within 1/255 of `AppContext` oracle (`N>=3`, analytic)**.

**G** — suite green, audit green, `examples/minimal` built by T2 installed config.

### T14: Drift guard — sample line count + `scene/objects` duplicate ratio (P2, audit, depends on T5+T7)

**D** — `tools/audit.rules`: `no_sample_bloat` (`forbid_grep` on `app/*_sample.cpp` line count >80 via `wc` check in `tools/audit.sh` or `max_lines` audit) and `no_object_duplicate` (`forbid_grep` on `scene/objects/*.hpp` duplicate ratio >10% via `tools/audit.sh` `diff` check). Keeps `app/mesh_sample.cpp` 160→80 and 11 identical object headers from re-introducing debt after T5/T7. `app/mpr_slice.hpp` 230-line mix (layout+geometry+oracle) split guard via `max_lines` per file (`mpr_slice` further split via T7/T8 builders — if `mpr_sample.cpp` cannot reach 80 alone, waist gauge is `mpr_slice.hpp` ≤100). **Depends on `T5` (collapse to `MeshObject`) + `T7` (builders trim `mesh_sample.cpp` to ≤80).**

**T** — gate: `app/mesh_sample.cpp` via facade `==42` lines (committed exact `42`, not `≤80` cap, `+ 1/255` layer ordering already in `T8`) && `grep -c "class.*Object.*ObjectBase" scene/objects/*.hpp ==6` (exact `6` technique kinds, not `≤6`) && `app/mpr_slice.hpp` `==98` (exact, not `≤100`); audit green — size caps secondary, primary `1/255` in `T8` ordering/mask.

**G** — suite green, audit green (new rules enforced).

### T15: Minimal light API for visualization consumers (P2, gap G1 — reprioritized from P1 to P2 per spec-review #3 to keep priority monotonic P0→P1→P2; depends on T1+T8, independent of T12-T14)

**D** — Even though `SPEC §1` keeps Phong-only + fixed headlight as non-goal (PBR/`Slice`/`Contour`+`ILight` deferred), visualization reuse needs a *minimal per-View light surface* without promoting the full hierarchy. Publish `scene/light.hpp` `Light` (already `View::lights` `vector<Light>` `scene/view.hpp:61`) through `Engine`: `Engine::setLights(ViewId, vector<Light>)` and `ViewBuilder::withLights(lights)`; document that empty `lights` = existing fixed headlight/unlit 2D preservation (FR-render gates stay byte-identical), non-empty → `broker/light_mapper.hpp` → `render/light.hpp` `ReLight` upload once per `View` before `drawLayer` loop (already `ViewSynchronizer` path). No new `render/light/` hierarchy this iteration — one struct keeps `View::lights` trivial. Keep `render::IMaterial→PhongMaterial` single path; this task only wires the value type end-to-end for the 80% viz case.

**T** — gate: `Engine e; e.setLights(viewId, {Light{Directional, dir{-1,-1,-1}}})` renders within 1/255 of direct `View::setLights` + `ViewSynchronizer` path (`N>=3` via offscreen, analytic non-empty vs empty probe: `empty` preserves headlight pixel, one `Directional` shifts `diffuse` ≥5/255 deterministically); `grep -c "setLights" include/render_engine/engine.hpp ==1` && `grep -c "class Light" scene/light.hpp ==1` (analytic `==1`, not `>=1`/`>0`).

**G** — suite green (`N>=3` light parity), audit green, `docs/engine.md` lights section added; `include/render_engine/engine.hpp` incremental — `T1` facade API (`class Engine`==1, `addMesh==1`, `setView`/`render`) still builds (`grep -c "class Engine" include/render_engine/engine.hpp ==1 && grep -c "addMesh" include/render_engine/engine.hpp ==1`).

### T16: Mapper cache consolidation — `CachedMapperBase` (P2, gap G2 — reprioritized from P1 to P2 per spec-review #3 to keep priority monotonic P0→P1→P2; depends on T5 broker inventory, independent of T12-T14)

**D** — Every `*ObjectMapper` (`broker/mesh_object_mapper.hpp:82`, `teapot_object_mapper.hpp:52`, `volume_object_mapper.hpp`, …) repeats `struct Entry{uint64_t generation; ReType instance;}; unordered_map<uint64_t,Entry> cache_;` + `mapCached` generation short-circuit + `invalidate(id)`. Extract `broker/cached_mapper_base.hpp` `template<AppT,ReT> class CachedMapperBase : public ICachedMapper<AppT,ReT>` that owns `unordered_map<uint64_t,Entry> cache_` + `mapCached`/`invalidate` + `clear()` and requires derived only to implement `map()`. Migrate all cached object mappers to inherit it — one definition, no per-file hand copy. Keeps `PlaneMapper`/`PlaneObjectMapper` stateless `IMapper` (ISP) untouched.

**T** — gate: `grep -c "unordered_map.*Entry.*cache_" broker/*_object_mapper.hpp ==0` after consolidation (analytic 0, cache lives only in base) && `grep -c "class CachedMapperBase" broker/cached_mapper_base.hpp ==1`; cached `MeshObject` generation hit still short-circuits (spy `map` call count 2→1) and `invalidate(id)` evicts exactly that id (per-id probe, `N>=3`).

**G** — suite green, audit green.

### T17: Depth default & naming sweep + doc polish (P2, gaps G4/G6, depends on T8b+T14)

**D** — **Depth default (G4):** document divergence: low-level `render::View::setDepthTest` / `scene::View::setDepthTest` default stays `false` (color-only, deterministic llvmpipe gates), `Engine` facade (`T1`) defaults `depthTest=true` for mesh-containing `createView`/`setView` (viz correctness). Add `tools/audit.rules` guard `engine_depth_default` (`require_grep` that `Engine` wiring sets `DepthConfig{true}` / `setDepthConfig(true)` for mesh layers). **Naming sweep (G6):** `docs/spec/*.md` + task comments still cite `AppMeshObject` — sweep to `scene::MeshObject` (`re::scene` namespace is prefix per `NAMING_CONVENTIONS.md §6`); `PerspectiveFraming` already removed T7, ensure no `App` prefix remains outside `broker/README.md` ACL wording. **Doc polish:** `README.md` module list add `scene/ broker/ utils/ test_utils/` (already `AUDIT_SOURCE_DIRS`), `docs/engine.md` add depth default note.

**T** — gate: `grep -R "AppMeshObject" docs/` ==0 && `grep -R "AppMeshObject" scene/` ==0 (analytic 0 post-sweep, ACL `broker/README.md` allowed `app::` wording waived via `tools/comment_context.allow` if needed); `grep -c "setDepthConfig" include/render_engine/engine.hpp ==1` && `grep -c "DepthConfig{true" include/render_engine/engine.hpp ==1` (analytic `==1`, not `>=1`); audit green.

**G** — suite green, audit green, `docs/engine.md` + `docs/spec/guardrails.md` updated; `include/render_engine/engine.hpp` still `grep -c "class Engine" ==1 && grep -c "setLights" ==1 && grep -c "addMesh" ==1` (incremental, not reverting `T1`/`T15` facade) — `git diff --name-only` shows incremental `engine.hpp`.

## Definition of Done — V5 (T1..T17 + T8b, T11b — 19 sessions)

- [ ] All 19 task gates green; full suite green on a clean tree at T17 (final of 19 sessions T1..T17 + T8b DepthConfig + T11b OIT fallback per Q1/Q2).
- [ ] `suite green N>=3` where GL-touching (T1 facade vs direct parity, T3 `renderViews` vs `SampleHarness` parity + T4 offscreen vs window parity, T7 loader parity, T8 layer priority/mask/override/ε/contour + T8b DepthConfig near-wins, T9 controller analytic, T11 tiled streaming + T11b OIT weighted fallback, T12 FPS slicer, T15 light parity — `tools/logs/task_*.gate.log` shows 3 consecutive `ctest Passed`, `GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6`); `audit green` with `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` — `tools/audit.sh` PASS (canonical via `tools/env.sh:6`; `examples/` intentionally excluded from `AUDIT_SOURCE_DIRS` — `examples/minimal.cpp` is consumer probe, `comment_tag_context` waived via `tools/comment_context.allow` if needed per Finding #15)
- [ ] `ASan+UBSan clean` on all `re_*` libs (`re_project_sanitizers` on 9 libs) + `examples/minimal` (via `cmake --install` `find_package` probe, `T2`/`T13`) + samples exit 0 under `xvfb` (`RE_SAMPLE_MAX_FRAMES=20` bounded, `FR-app.1`)
- [ ] `LICENSE` per dataset dir (`data/meshes/LICENSE`, `data/volumes/LICENSE` enumerated; enforced by `audit.sh` built-in `assets licensed per-dir` + T2 gate `test -f`)
- [ ] `R9` doc-map: `git diff --name-only` at T17 (final 19 sessions) includes `include/render_engine/engine.hpp` + `docs/engine.md` (T1/T15+T8b DepthConfig), `cmake/RenderEngineConfig.cmake.in` (T2), `app/frame_loop.hpp` + `app/imgui_overlay.hpp` (T3), `core/offscreen.hpp` + `render/offscreen.hpp` + `core/caps.*` (T4/T11/T11b via `core::Caps`), `scene/objects/` collapsed (T5), `scene/store.hpp` single-map (T6), `scene/builders.hpp` (T7), `scene/layer.hpp` (`uint32_t` `1u<<`) + `scene/depth_config.hpp` + `scene/view.hpp` (T8 `0xFFu` + T8b `DepthConfig`) + `scene/camera_controller.hpp` (T9), `data/result.hpp` (T10 `andThen`/`orElse`), `render/volume_renderer.cpp` tiled (T11) + `render/linked_list_oit.cpp` weighted fallback `==1` (T11b), `utils/fps_counter.hpp` + `core/re_context.hpp` (`draw.hpp` alias, `PRIVATE glad` preserved) (T12), `examples/minimal.cpp` `==22` + `1/255` smoke (T13), `tools/audit.rules` + `broker/cached_mapper_base.hpp` (T14/T16), `scene/light.hpp` `==1` + `Engine lights` (T15) per rows above (19 sessions)
- [ ] `R3` regression lock: `FR-io.*`/`FR-data.*`/`FR-vol.*`/`FR-core.*`/`FR-render.*`/`FR-app.*` still green via full-suite regression (20 FR table above, no weakening)
- [ ] `R4` evidence: every T asserts explainable constant (analytic `1/255`, `1e-6`, `152 MB` via `w*h*16*32`, `60 fps` within `1e-3`, light shift ≥5/255, cache dedup 0, not `non-empty`/`visual`)
- [ ] `comment_tag_context` audit PASS — every `T[0-9]+`/`SPEC §` comment block carries ≥120 chars self-contained prose (audit `comment_tag_context` PASS; waivers in `tools/comment_context.allow` only for `render/shaders/` + `test_utils/` + `examples/` per Finding #15)
- [ ] `install` reproduces (`cmake --install` + `find_package` minimal probe green), `examples/minimal.cpp` ≤30 lines, `app/*_sample.cpp` ≤80 lines, `scene/objects` technique kinds ≤6, `app/mpr_slice.hpp` ≤100

