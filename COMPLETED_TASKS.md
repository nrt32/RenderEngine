# COMPLETED_TASKS — RenderEngine

Historical record of the finished work. The sequential, gated loop (T1–T15)
plus the post-loop dev-experience task (T16) are DONE and archived here; the
active backlog for the next engine iteration lives in `TASKS.md` (V2
future-scope roadmap, numbered 1–10). See `SPEC.md` for FRs, §6 for
guardrails, `NAMING_CONVENTIONS.md` for style.

## Documentation map (T-map)

| Task | Docs updated in the same commit |
|---|---|
| T1 | README.md (build/test commands, env vars), AGENTS.md (build/test note) |
| T2 | data/README.md (sources, licenses, checksums), SPEC §7 (pin URLs + SHA256) |
| T3 | docs/core.md |
| T4 | docs/io-data.md |
| T5 | docs/io-data.md |
| T6 | docs/volume.md |
| T7 | docs/render.md |
| T8 | docs/render.md |
| T9 | docs/render.md |
| T10 | docs/render.md |
| T11 | docs/render.md |
| T12 | docs/samples.md |
| T13 | docs/samples.md |
| T14 | docs/mpr.md |
| T15 | docs/mpr.md; end-of-loop DoD evidence (see "Definition of Done") |
| T16 | README.md (convenience scripts table), SPEC §8 (Convenience scripts note) |

---

## T1: Build & test scaffolding

**D** — CMake skeleton (>= 3.24) with FetchContent pins for every dependency
(GLFW 3.4, glad2 v2.0.8, GLM 1.0.1, Dear ImGui v1.92.9, GoogleTest v1.15.x,
spdlog v1.14.1, stb pinned commit); module dirs created (`io data volume core
render app tests`); spdlog initialized; test binary target built with
**ASan+UBSan** and `-Werror`; clang-format config; **GL-free typed
`Result<T,E>` (SPEC §5) in `data/result.hpp`** — shared by all layers (io/,
core/, render/ reuse it; keeps io/data/volume GL-free); **offscreen GL fixture
as a core/ component** (hidden GLFW window + EGL-surfaceless fallback; raw GL
stays under core/, tests consume it via core/ wrappers); README with
build/test commands **and the `source tools/env.sh` launch prerequisite
(env vars, SPEC §8)**; AGENTS.md build/test note points at `tools/env.sh`.

**T** — gate tests assert: (1) the empty suite builds and runs green with the
sanitizers; (2) a trivial explainable constant (e.g. 2+2==4) passes; (3) the
offscreen fixture creates a GL 4.6 core context — asserted via
`glGetIntegerv(GL_MAJOR_VERSION)==4`, `glGetIntegerv(GL_MINOR_VERSION)==6`,
and `GL_CONTEXT_PROFILE_MASK & GL_CONTEXT_CORE_PROFILE_BIT` (not the
unreliable `glGetString(GL_VERSION)` string) — and reports no GL errors;
(4) the gate environment is correctly sourced: `$AUDIT_SOURCE_DIRS` equals
`io data volume scene core broker render app utils tests` and `$LOOP_BUILD_TEST_CMD` is
non-empty (R15 — a forgotten `source tools/env.sh` fails loudly instead of
the audit silently scanning default dirs; the "non-empty" check is a binary
env-var presence assertion — the sanctioned R4 exemption — not a weak
behavioral assertion, and its companion assertion pins the exact
`AUDIT_SOURCE_DIRS` string); (5) audit passes with our source-dir override.

**G** — clean build, full suite green, ASan/UBSan clean, audit green.

## T2: Asset provisioning

**D** — commit the staged assets produced by /loop-setup (which downloaded,
SHA256-verified, and converted them without touching git): `data/meshes/bunny.obj`
(Stanford bunny) + `data/meshes/teapot.obj` (Utah teapot) from the pinned
alecjacobson/common-3d-test-models URLs (SPEC §7), `data/volumes/sample_ct.nrrd`
(from the pinned Slicer `CT-chest.nrrd`, downsampled to <=128^3 and re-written as
a raw NRRD), `data/fixtures/` golden files; **a LICENSE file beside each external
dataset**; `data/README.md` recording sources, URLs, licenses, checksums; SPEC
§7 URLs + verified SHA256s recorded. (T2 owns the commit; setup never commits.)

**T** — gate asserts: (1) every dataset dir (`data/meshes`, `data/volumes`)
contains a LICENSE file — the gate enumerates each committed dataset dir and
asserts one LICENSE each (audit rule `assets_licensed` is only a floor: it
greps the whole tree); (2) committed files have the expected SHA256s recorded in
SPEC §7 / `data/README.md`; (3) the NRRD header parses to the expected dims
<=128^3; (4) bunny.obj has its known vertex count (hand-counted from the
committed file).

**G** — assets committed, licenses present, audit green.

## T3: core/ GL foundation

**D** — GLFW window/context wrapper, RAII GL objects (VAO, VBO, EBO, Texture,
FBO), ShaderProgram compile/link with typed diagnostics (typed `data::Result`
from T1, SPEC §5).

**T** — gate asserts (FR-core.1/2): (1) create→bind→destroy of each RAII object
   produces no GL errors under the offscreen fixture and is ASan/LSan clean;
   (2) a valid shader compiles/links and reports no error; (3) an intentionally-
   malformed shader (source contains the known-bad token `glibberish` at line 7)
   returns a typed error string containing that token and the offending line
   (`ERROR: 0:7` — golden substring), no crash; (4) destructor order frees GL
   objects (no GL errors on teardown).
   Shaders in gate tests use **GLSL 450** (not 460) per SPEC §8: the headless
   gate runs on llvmpipe, whose GLSL compiler caps at 4.50; a 4.6 core context
   accepts 4.50 shaders.

**G** — suite green, sanitizer clean, audit green.

## T4: io/ + data/ mesh & image

**D** — OBJ-style mesh loader, image loader (stb), `Mesh` container with
computed face normals and AABB, golden fixtures under `data/fixtures/`.

**T** — gate asserts (FR-io.1/3/4, FR-data.1/2): (1) bunny.obj loads with its
known hand-counted vertex/index counts and AABB; (2) a golden fixture mesh has
exact expected bounds; (3) face normal of a known triangle equals the closed-form
cross-product value; (4) image loader returns known dimensions + corner/center
pixel values; (5) malformed input returns a typed error and leaves no partial
state.

**G** — suite green, sanitizer clean, audit green.

## T5: io/ + data/ volume (NRRD)

**D** — NRRD loader (text header + raw block) and `VolumeDataset` with
trilinear sampling.

**T** — gate asserts (FR-io.2/4, FR-data.3): (1) the committed `sample_ct.nrrd`
loads with expected dims <=128^3 and matching voxel values at indexed corners;
(2) an interior sample equals the closed-form trilinear interpolant of the 8
corner values within 1e-6; (3) malformed NRRD returns a typed error, no partial
state; (4) memory stays within the v1 budget cap (<=128^3).

**G** — suite green, sanitizer clean, audit green.

## T6: volume/ pure math

**D** — `TransferFunction` (control points → RGBA), ray/AABB sampling step
computation, front-to-back ray-cast compositing math (pure, no GL).

**T** — gate asserts (FR-vol.1/2/3): (1) transfer function is exact at control
points and a linear ramp between them within 1e-6; (2) compositing a known
(color, alpha) sample sequence matches the closed-form alpha-blend result within
1e-6; (3) step positions for a given AABB+ray are analytic.

**G** — suite green, sanitizer clean, audit green.

## T7: render/ MeshRenderer + Phong

**D** — `IMaterial` + `PhongMaterial` (transparency as a material property),
`MeshRenderer` opaque forward pass (stateless: render(scene, camera, target)),
mesh geometry handling shared with later mesh-family renderers.

**T** — gate asserts (FR-render.1): (1) a known solid-color mesh rendered to an
offscreen target has the expected center-pixel color within 1/255; (2) an
opaque-only scene produces output with **center-pixel alpha == 1.0** (no
transparency engaged — injectable spy confirms the pipeline is off); (3)
materials report `isTransparent()` correctly.

**G** — suite green (N>=3 for readback tests), sanitizer clean, audit green.

## T8: render/ PlaneRenderer

**D** — `PlaneRenderer` for textured quads/planes (feeds MPR).

**T** — gate asserts (FR-render.5): corner/center pixel of a textured quad
matches the source texture sample within 1/255; plane orientation/UV mapping
verifiable analytically.

**G** — suite green (N>=3), sanitizer clean, audit green.

## T9: render/ VolumeRenderer

**D** — `VolumeRenderer` ray-cast GL draw pass that consumes the pure
`volume/` math (dataset texture upload, ray-cast shader, sampling loop).

**T** — gate asserts (FR-render.6): a tiny synthetic volume ray-cast has a
center pixel matching the analytic ray-cast within 1/255.

**G** — suite green (N>=3), sanitizer clean, audit green.

## T10: render/ OIT pipeline

**D** — `ITransparencyPipeline` interface + `LinkedListOIT` impl (capture →
depth-sort → composite); `MeshRenderer` auto-engages it when any material is
transparent.

**T** — gate asserts (FR-render.2/3): (1) two overlapping quads at known depths
composite to the analytic depth-ordered blend within 1/255; (2) an opaque-only
scene produces output with **alpha == 1.0 at the sampled pixels**; adding one
transparent quad flips the pipeline on (injectable spy); (3) the pipeline
interface is swappable (a stub impl drives the same renderer).

**G** — suite green (N>=3), sanitizer clean, audit green.

## T11: render/ SliceRenderer

**D** — `SliceRenderer` mesh-family technique using a geometry shader to clip a
mesh against a plane (pure GPU) and emit the cross-section; reuses mesh geometry
handling + materials; NO OIT in v1.

**T** — gate asserts (FR-render.4): emitted cross-section vertices lie on the
clip plane (analytic distance <= 1e-4 relative); clipped mesh renders correctly
on a known mesh.

**G** — suite green (N>=3), sanitizer clean, audit green.

## T12: app/ sample scaffolding + mesh/plane/volume samples

**D** — shared sample harness as an app/ component (window + ImGui overlay
wiring + run loop); mesh, plane, and volume samples driven through it.

**T** — gate asserts (FR-app.1, partial): each of the mesh/plane/volume samples
runs under WSLg (or Xvfb in the gate), opens a window, and exits cleanly (exit
code 0, no sanitizer reports) within a timeout.

**G** — samples run, suite green, sanitizer clean, audit green.

## T13: app/ slice + OIT samples (completes FR-app.1)

**D** — slice and OIT samples wired through the harness; per-sample
instructions for driving each capability (per-sample README section or inline
help text) for all five capabilities.

**T** — gate asserts (FR-app.1, full): the slice and OIT samples run under WSLg
(or Xvfb in the gate), open a window, and exit cleanly (exit code 0, no
sanitizer reports) within a timeout — the complete 5-sample smoke set passes.

**G** — samples run, suite green, sanitizer clean, audit green.

## T14: app/ MPR view — layout + slice views

**D** — MPR sample: one **1280×960** window with a **2×2 viewport grid** (four
**640×480** viewports; T top-left, C top-right, S bottom-left, 3D bottom-right,
per SPEC FR-app.2); the T/C/S views render the volume slice along the pinned
axis convention (T = constant Z, C = constant Y, S = constant X); shared
slice-state/camera scaffolding for the 2D views.

**T** — gate asserts (FR-app.2): (1) viewport dims equal the SPEC constants
(window 1280×960; four 640×480 viewports at the pinned grid positions);
(2) each 2D slice view samples the volume along its axis per the SPEC
convention (pixel check per view).

**G** — MPR runs, suite green (N>=3), sanitizer clean, audit green.

## T15: app/ MPR contour + 3D view + camera

**D** — mesh contour overlay on each slice view (plane∩mesh cross-section), the
3D rendering view (mesh), camera interplay between slice-state and the 3D view;
completes FR-app.2/3.

**T** — gate asserts (FR-app.3): (1) contour: for the golden box mesh, **>= 90%
of pixels within 2 px (Euclidean) of the analytic plane∩mesh intersection curve
match the contour color** (curve computed in closed form from the box+plane for
each slice view's plane); (2) the 3D view draws the mesh.

**G** — MPR runs, suite green (N>=3), sanitizer clean, audit green; the
end-of-loop "Definition of Done" evidence below is complete.

---

## Definition of Done (end-of-loop evidence, finalized at T15)

Evidence recorded complete at archive time (boxes checked retroactively at the
V2 SPEC-REVIEW gate).

- [x] All 15 task gates green; full suite green on a clean tree at the last task.
- [x] GPU/readback tests (T7–T11, T14, T15) verified with **N>=3 consecutive
      green runs** (records in `tools/logs/`).
- [x] Mechanical audit green (`tools/audit.sh`) with
      `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils tests"`.
- [x] ASan+UBSan clean on all test binaries (no leaks, no UB).
- [x] Assets committed with a LICENSE beside every dataset dir; `data/README.md`
      records sources, URLs, licenses, and SHA256 (matches SPEC §7).
- [x] Documentation map complete: README, docs/core.md, docs/io-data.md,
      docs/volume.md, docs/render.md, docs/samples.md, docs/mpr.md,
      data/README.md.
- [x] All five capability samples plus the MPR sample run under WSLg/Xvfb and
      exit cleanly.

---

## T16: Dev-experience tooling (convenience scripts)

**D** — commit the four thin wrapper scripts under `tools/` that reconstruct the
§8 contract so manual sessions never re-derive it: `tools/build.sh [target...]`
(configure + build, target pass-through to `cmake --build`), `tools/test.sh`
(configure + build + `ctest --test-dir build --output-on-failure`, exactly
`eval "$LOOP_BUILD_TEST_CMD"`), `tools/run_sample.sh <mesh|plane|volume|slice|oit|mpr>`
(build + run one sample interactively), and `tools/clean.sh` (removes only
`build/`). Each sources `tools/env.sh` itself. Scripts stay **non-authoritative**:
the loop gate still uses `tools/env.sh` + `LOOP_BUILD_TEST_CMD` as the single
source of truth (SPEC §8). README convenience-scripts table and SPEC §8 note
updated (already drafted, uncommitted).

**T** — gate asserts (R15-style, explainable): (1) each script exists, is
executable, and is `shellcheck`-clean; (2) `tools/test.sh` from a clean tree
reproduces the full §8 contract: full suite green AND it sourced `tools/env.sh`
(`$AUDIT_SOURCE_DIRS` equals `io data volume scene core broker render app utils tests` after it
runs); (3) `tools/build.sh re_sample_mesh` builds exactly that target and
`tools/test.sh` output matches `eval "$LOOP_BUILD_TEST_CMD"` (same exit code);
(4) `tools/run_sample.sh` with an invalid name exits 2 and prints the literal
usage line `valid names: mesh plane volume slice oit mpr`; (5) `tools/clean.sh`
removes `build/` and leaves the source tree untouched (asserted via a
no-op-pattern: it references only the `build` path); (6) audit green.

**G** — suite green, audit green, scripts committed with README/SPEC docs.

---

## V2: Multi-view / Asset / Platform / Maintainability (archived 2026-08-23 — 8 tasks)

V2 was the gated sequential loop for the V2 future-scope roadmap (TASKS.md V2.1–V2.8, SPEC §9). All 8 gates are green; `tools/logs/` artifacts have been purged for the pure-redesign V3 iteration. The backlog below is copied verbatim from `TASKS.md` at archive time (D/T/G + doc-map row) — no re-interpretation.

## V2 Documentation map addendum (V2 T1–T8)

| Task | Docs updated in the same commit |
|---|---|
| V2-T1 | docs/render.md (`IRenderer`, `render/types.hpp`) |
| V2-T2 | docs/render.md (`View`/`ViewRect`/`ViewRenderer`, `core::blit`) |
| V2-T3 | docs/render.md (`AssetRegistry`) |
| V2-T4 | tools/env.sh (`AUDIT_SOURCE_DIRS`), AGENTS.md (Layout + build/test notes), TASKS.md preamble (R15/audit dir lists), docs/spec/modules.md + SPEC.md at-a-glance module list (`utils/`) |
| V2-T5 | docs/spec/env.md (per-OS offscreen backend) |
| V2-T6 | docs/core.md (draw-state cache) |
| V2-T7 | docs/render.md (`.glsl` files + malformed fixture) |
| V2-T8 | docs/render.md (`RE_GLSL_VERSION`), docs/spec/env.md (450/460 ceiling note) |

---

## V2-T1: `IRenderer` interface + shared `render/types.hpp` (SPEC §9 V2.3)

**D** — Move `Camera`/`RenderTarget` out of `mesh_renderer.hpp` into a shared `render/types.hpp`; define a pure abstract `IRenderer::render` contract implemented by `Mesh/Plane/Volume/SliceRenderer`. Lands as the dispatch mechanism of the multi-view workstream (task V2-T2). No behavior change.

**T** — full suite green (all prior gates), audit green; renderers unchanged in output (regression lock R3).

**G** — suite green, audit green.

## V2-T2: Multi-view rendering (Model B: per-view FBO + engine blit) (SPEC §9 V2.4)

**D** — Per-view `core::Framebuffer` + a new `core::blit` (`glBlitFramebuffer` under core/); `View`/`ViewRect`/`ViewRenderer`; app shares per-view window-section handles + abstract scene objects; RE dispatches objects to the correct renderer via `IRenderer` (V2-T1), renders into each view's own FBO, then blits each FBO into its window rect. No app-side viewport blending. Drives SceneView/MPRView composition.

**T** — gate asserts (explainable): a 2-view layout in a **1280×480** window renders each view's scene into its own FBO and the final window blit places each view's content in its pinned `ViewRect` — View A = (0,0,640,480), View B = (640,0,640,480) — and the center pixel of each view (A: (320,240), B: (960,240)) matches that view's scene's expected color within 1/255 (readback via the core/ wrapper only, N>=3); MPR sample still green.

**G** — suite green (N>=3 for readback), audit green.

## V2-T3: Asset registry (`AssetHandle`) (SPEC §9 V2.5)

**D** — `render::AssetRegistry::register()` → copyable `AssetHandle{index,generation}`; one GPU object per individual CPU object globally (fixes `MeshRenderer`+`SliceRenderer` double-upload of the same `data::Mesh`). Scene instances store `AssetHandle` instead of raw `const data::*` pointers; handles are the currency views exchange.

**T** — gate asserts (explainable): registering the same `data::Mesh` twice (once via `MeshRenderer`, once via `SliceRenderer`) yields one GPU object — `AssetRegistry::slotCount() == 1` and both handles resolve to the same GL object id; dangling-handle detection on generation mismatch (a stale {index,generation} lookup returns a typed error, no crash).

**G** — suite green, audit green.

## V2-T4: `utils/` module (offscreen context + pixel reader) (SPEC §9 V2.1)

**D** — Move `offscreen_context` + `read_pixels` to `utils/` (`re::utils::OffscreenContext`, `re::utils::PixelReader`); `core/` keeps the raw-GL anchors `loadCoreGl` / `readRgba8`. **Add `utils` to `AUDIT_SOURCE_DIRS`** so the audit still scans them (guardrail `gpu_api_ownership` / `no_production_readback` stay intact).

**T** — gate asserts: `tools/env.sh` sets `AUDIT_SOURCE_DIRS` including `utils`; audit green with raw GL only under core/; tests still pass via `re::utils::*`.

**G** — suite green, audit green.

## V2-T5: Platform-extensible context-backend factory (SPEC §9 V2.2)

**D** — `utils::OffscreenContext` picks the no-display backend per-OS (EGL-surfaceless/Mesa on Linux, ANGLE-EGL or WGL on Windows, CGL on macOS), replacing the Mesa-only `EGL_PLATFORM_SURFACELESS_MESA` hardcode.

**T** — gate asserts: backend selection is deterministic per platform macro; Linux path unchanged (llvmpipe context still GL 4.6 core).

**G** — suite green, audit green.

## V2-T6: Internal dirty-flag draw-state cache (SPEC §9 V2.10)

**D** — Internal dirty-flag cache in `core/draw.cpp`: cache `setViewport`/`setClearColor`/`enable*`/`disable*` values and skip redundant `gl*` calls. Free-function `core::Draw` API + audit anchors unchanged. Motivator: OIT mid-frame toggles.

**T** — gate asserts (explainable): a test-injectable GL-call spy in `core::Draw` records call counts — `setClearColor(red); setClearColor(red)` issues exactly **1** `glClearColor` (the second is a cache hit, no GL call), and the same holds for `setViewport`/`enable*`/`disable*`; output pixels are unchanged within 1/255 (readback via the core/ wrapper only, N>=3).

**G** — suite green, audit green.

## V2-T7: Shader externalization to `.glsl` files (SPEC §9 V2.6)

**D** — Replace inline `constexpr char[]` GLSL in render/ with `.glsl` files loaded by `core::ShaderProgram` (adds syntax highlighting/editor navigation). Keep the completed-loop t3 malformed-shader golden substring `ERROR: 0:7` (COMPLETED_TASKS.md T3) reproducible via a fixture file. Relocation only.

**T** — gate asserts: the completed-loop t3 `ERROR: 0:7` golden substring still produced; all completed-loop shader-backed gates (t7–t11, t14, t15 in COMPLETED_TASKS.md) unchanged.

**G** — suite green, audit green.

## V2-T8: GLSL profile macro (`RE_GLSL_VERSION`) (SPEC §9 V2.7)

**D** — Decouple the shader language level from the llvmpipe ceiling: 450 = portable floor (tests/CI), 460 = hardware floor. Single `#version` concern now that shaders live in files (V2-T7).

**T** — gate asserts (explainable): in the gate env the macro expands to `#version 450` — a `static_assert` on the macro's version value plus compiling a fixture shader whose `#version` line is produced by the macro on llvmpipe. The 460/hardware compile is a **manual sample verification**, not a gate assertion (llvmpipe caps at GLSL 4.50, SPEC §8).

**G** — suite green, audit green.

---

## Definition of Done (end-of-V2 evidence, finalized at V2-T8)

- [x] All 8 V2 task gates green; full suite green on a clean tree at the last task (purged logs were `tools/logs/task_1..8.{log,gate.log,pass,review}` + `run_all.out` + `session_*.lease`).
- [x] GPU/readback tests (V2-T2, V2-T6) verified with **N>=3 consecutive green runs** (records were in `tools/logs/` — purged after archive).
- [x] Mechanical audit green with `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils tests"` (via `source tools/env.sh` — `scene`/`broker` added for V3; V2 audit used `io data volume core render app utils tests`).
- [x] ASan+UBSan clean on all test binaries (no leaks, no UB).
- [x] Documentation map complete (table above): docs/render.md, docs/core.md, AGENTS.md, docs/spec/env.md, tools/env.sh, TASKS.md preamble — exactly as listed per task.
- [x] Sample smoke set (mesh/plane/volume/slice/oit/mpr) still green on `GALLIUM_DRIVER=llvmpipe` + `MESA_GL_VERSION_OVERRIDE=4.6` (450 portable floor).

Artifacts purged this iteration: `tools/logs/run_all.out`, `tools/logs/session_*.lease` (leases from every prior implementer session), `tools/logs/task_*.{log,gate.log,pass,review}` — runner-owned `tools/logs/` is now empty (see AGENTS.md R11: state files under `tools/logs/` are runner-owned). Workspace is clean for the pure-redesign V3 iteration (no feature carry-over — see `docs/spec/roadmap.md` §9.1 and `TASKS.md` V3 backlog redesigned to pure implementation improvements per user direction 2026-08-23).


---

## V3: Pure-redesign `scene`/`broker`/View/List/Persistence + review follow-ups (archived 2026-08-27 — 23 tasks)

V3 was the pure-redesign gated sequential loop for the V3 roadmap (TASKS.md T1–T23, SPEC §10–§12, §3, §11.6, §13, review follow-ups T13–T23). All 23 gates are green; `tools/logs/` artifacts have been purged for the next iteration. The backlog below is copied verbatim from `TASKS.md` at archive time (D/T/G + doc-map row) — no re-interpretation.


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

## T17: Renderer consolidation — one prologue, one quad, one hash, no glad leak

**D** — Deduplicate the copy-paste across the four technique renderers (review §"Duplication debt"): (1) bind-target+viewport+clear+disable-depth/blend prologue repeated verbatim 4× (`mesh_renderer.cpp:149-160`, `plane_renderer.cpp:241-252`, `slice_renderer.cpp:113-128`, `volume_renderer.cpp:185-201`) → extract one internal pass-setup helper (or fold into `core::DrawContext::beginPass`); (2) each `drawLayer` duplicates its own `render()` body minus prologue (`mesh_renderer.cpp:203-236` vs `:69-95`; plane basis-matrix math duplicated nearly line-for-line at `plane_renderer.cpp:284-297` vs `:351-360`; volume uniform block `:212-237` vs `:283-300`; slice clip loop `:130-156` vs `:175-211`) → merge via private shared implementation taking a "clear" flag; (3) full-screen/unit quad VAO built 3× with identical index pattern (`plane_renderer.cpp:119-176`, `volume_renderer.cpp:73-117`, `linked_list_oit.cpp:105-144`, NDC verts defined twice `:32-37`/`:40-45`) → one shared internal quad provider; (4) `geometryFor(handle)` identical in `mesh_renderer.cpp:42-52` and `slice_renderer.cpp:86-96` → shared helper over `AssetRegistry`; (5) `meshContentHash` deliberately duplicates `scene::computeContentHash` (`asset_registry.cpp:19-20`) → move the byte-hash into a GL-free header both layers include (e.g., `data/content_hash.hpp`) so there is one definition; (6) `slice_renderer.cpp:6` includes `<glad/gl.h>` and uses `GL_TRIANGLES` (`:288`) despite "render/ is GL-call-free" headers → route through a core/ constant/wrapper.

**FR:** regression lock R3 — zero pixel change; all renderer gates unchanged within 1/255.

**T** — gate asserts: suite green unchanged (all t7–t15 renderer gates byte-identical tolerance); mechanical greps: prologue pattern count ≤1 occurrence site, `kScreenQuadVerts` definitions ≤1, `#include <glad/gl.h>` under render/ == 0 hits.

**G** — suite green (N>=3 for readback gates), audit green.

## T18: Depth-buffer support — optional depth attachment + per-view depth state

**D** — Architecture-review finding: v1 framebuffers are color-only everywhere (`view_target.hpp`, `RenderTarget` docs), forcing painter's-order workarounds (MPR box face-ordering comment in `makeBoxMesh`) and blocking correct OIT-with-opaque-meshes (T19). Add an optional depth attachment: `core::Texture2D` depth format (or renderbuffer via a small core/ wrapper) attachable by `ViewTarget` when constructed with `DepthMode::Enabled`; `render::View` gains a per-view `depthTest` flag driving `enable/disableDepthTest` in the pass prologue (T17 helper); default stays color-only so every existing analytic gate is untouched; OIT capture/composite explicitly disable depth as today. Document that color-only remains the deterministic-gate default (llvmpipe-safe) and enabled-depth views assert completeness with the depth attachment.

**FR:** new acceptance — with depth enabled, two overlapping opaque meshes at different z render the nearer mesh's color at the overlap pixel (analytic arrangement), whereas color-only renders the later-drawn one; existing gates unchanged.

**T** — gate asserts (N>=3): depth-on target completeness check passes; near-mesh-wins overlap probe within 1/255; depth-off default leaves all prior FBO gates green; `LinkedListOIT` end-to-end still green with depth-on targets.

**G** — suite green (N>=3), audit green. **Feeds:** T19 (required), MPR 3D view cleanup (follow-up allowed to drop face-ordering hack).

## T19: OIT sample — opaque + transparent meshes with depth overlap (no quads)

**D** — Current `app/oit_sample.cpp` uses **three coplanar-normal transparent quads** (one shared golden quad mesh, alphas 0.55, stacked z=+0.5/0/−0.5, camera at (0,0,3)) — no opaque geometry at all, and quads only. Replace with a scene of **actual meshes**: ≥2 opaque meshes (e.g., golden box + bunny.obj or teapot.obj from `data/meshes/`) interleaved with ≥2 transparent meshes (e.g., two nested glass-like boxes/spheres built procedurally or from `data/meshes/`), arranged so that along the view direction each transparent mesh overlaps both opaque meshes and each other (e.g., opaque objects at different depths inside/between two enclosing transparent shells). This exercises the real OIT contract: opaque pass renders first, transparent fragments capture into the per-pixel linked list, depth-sorted composite blends over the opaque result — including the case the current sample never tests (opaque behind/in-front-of transparency). **Depends on T18**: correct opaque self-occlusion needs a depth buffer on the render target (today v1 FBOs are color-only — architecture-review finding "no depth attachment anywhere"; T18 adds the optional depth attachment + per-view depth-test flag this sample consumes).

**FR:** `FR-render.2/3` — center-region pixels match the analytic depth-ordered premultiplied blend of [opaque occluder]→[near transparent]→[far transparent] chain within 1/255; opaque pixels under no transparency stay alpha 255; adding the transparent meshes flips the pipeline on (spy count == number of transparent meshes).

**T** — gate asserts (explainable): known arrangement with closed-form expected composite at ≥3 probe pixels (fully-opaque region, near-transparent-over-opaque region, far-transparent-over-near-over-opaque region); no quad primitives (`grep -c "unitQuadXY\|makeQuadMesh" app/oit_sample.cpp` == 0); pipeline spy engaged exactly for the transparent set; suite green N>=3.

**G** — suite green (N>=3), audit green, OIT sample updated instructions text matching the new scene. **Depends on:** T18.

## T20: Broker becomes the only app path — complete mapper inventory (volume/plane/contour)

**D** — Two related review findings fixed together: (1) `ViewSynchronizer::sync` **silently drops volumes** — a matched `VolumeObject` inserts a locally-defined `Noop : render::IRenderable` (`view_synchronizer.cpp:202-218`) and references a `PlaneMapper` that does not exist anywhere in `broker/`; (2) the broker façade has **zero app consumers** — every sample includes `render/` directly (`mpr_sample.cpp:54-64`, `volume_sample.cpp:32`, …), violating the documented ACL (`broker/README.md:7`: app never includes render/). Work: implement the missing mappers (`PlaneMapper : IMapper<scene::PlaneDesc, render::ClipPlane>` with `Space::VoxelIndex→World` conversion via dataset dims/model, plus `VolumeSliceObjectMapper` producing a real GPU slice draw once T16's extraction exists — until then `VolumeObjectMapper` must produce a working `VolumeRenderer` layer, never a Noop); route ALL samples through `IViewBridge` (`sync/renderAll/presentAll`) and delete direct `render/` includes from `app/*.cpp`; material hand-off stays stubbed only until T14 lands the material slot (`MeshObjectMapper.cpp:21` placeholder gets its real store then).

**FR:** no pixel change for mesh path (regression lock); volume path gains parity with direct-`VolumeRenderer` output within 1/255 (readback probe).

**T** — gate asserts: `grep -R "#include \"render/" app/ --include=*.cpp` → 0 hits; sync of a scene containing `MeshObject+VolumeObject+PlaneObject` produces ≥2 non-Noop layers whose FBO readback matches the direct-renderer oracles within 1/255 (N>=3); `grep -c "Noop" broker/` == 0; PlaneMapper unit test converts voxel-index plane z=35 → world point z=35.5 exactly.

**G** — suite green (N>=3), audit green (`broker_app_reach` now enforceable against live samples), ACL rule un-commented/enforced if it was review-only.

## T21: Persistence honesty — activate write-only scaffolding, unify StableKey

**D** — Review found the persistence mechanism is asserted by tests but not actually driving behavior: `SceneStore/ViewStore::dirtyFieldsSince` return hardcoded field sets ignoring the `dirtyLog_` they maintain (`store.cpp:138-144`, `:204-208`); `tombstoneGen_` is written on erase but never read (no stale-handle detection enforcement, `store.cpp:89`); `view_synchronizer.cpp:49-51` contains a fake `executor_->parallelFor` exercise whose results are discarded alongside `auto sceneDirty = …; (void)sceneDirty;` (:100-103); and identity is defined twice with divergent shapes — `StableKey{version,layoutId,viewId}` in `view_compositor.hpp:34-49` vs a version-less twin in `view_synchronizer.hpp:75-88`. Work: make `dirtyFieldsSince` compute from `dirtyLog_` (bounded drain per the documented contract); enforce tombstones in `resolve()` (stale id after erase → typed error); delete the fake parallel section and consumed-or-removed `(void)` discards; extract ONE `StableKey` into a shared broker header used by synchronizer+compositor; give `CameraMapper` an id-keyed multi-entry cache (current single-slot thrashes with >1 camera, `camera_mapper.hpp:42-45` vs `invalidate(id)` ignoring id at `:59`).

**FR:** none behavioral beyond correctness — existing persistence gates (T6) must stay green while their inputs become genuinely computed.

**T** — gate asserts: mutate only camera → `dirtyFieldsSince` returns `{Camera}` exactly (not the hardcoded 4-field set); erase object then `resolve(oldId)` → typed error (code 2 stale); `grep -c "parallelFor" broker/` == 0; single `StableKey` definition (`grep -rc "struct StableKey" broker/` == 1); two cameras alternating pans each get cache hits (spy/gen counters prove no cross-camera thrash).

**G** — suite green, audit green, `no_dump_sync` still enforced.

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

## Definition of Done — review follow-ups (T13–T23, user-mandated + architecture review)

- [ ] `T13`: no undocumented raw owning-suspect pointers in `scene/ broker/ app/` public APIs; every remaining borrow carries a lifetime note; audit ownership rule green; construction-order hazards removed from all samples.
- [ ] `T14`: unified typed asset store covers mesh + volume + image (+ material slot with real dedup replacing the `material=nullptr` placeholder); per-renderer pointer-keyed texture caches deleted; same-dataset-two-renderers gate proves one GPU texture.
- [ ] `T15`: comment sweep landed — every SPEC/task tag is accompanied by self-contained rationale (incl. the false "deduped RE material handle" claim until T14 makes it true); bare-tag pattern rule green or explicitly allowlisted.
- [ ] `T16`: GPU volume-plane extraction shipped; `plane_sample` demonstrates an extracted volume plane (not a gradient quad); MPR 2D views interactive on the GPU path with CPU oracle retained for tests.
- [ ] `T17` (renderer consolidation): prologue/quad/hash/geometryFor deduplicated; zero pixel drift on all renderer gates; `<glad/gl.h>` gone from render/.
- [ ] `T18` (depth): optional depth attachment + per-view depth flag; near-mesh-wins overlap gate; color-only default untouched.
- [ ] `T19` (needs `T18`): OIT sample = ≥2 opaque + ≥2 transparent real meshes with view-direction overlap, depth-buffer-backed target, analytic composite probes green N>=3.
- [ ] `T20`: all samples route through `IViewBridge`; volume/plane layers are real (no Noop); `PlaneMapper` exists with voxel→world conversion test.
- [ ] `T21`: dirty tracking computed from `dirtyLog_`; tombstoned ids resolve to typed errors; single `StableKey`; no fake parallel code; multi-camera mapper cache.
- [ ] `T22`: domain-tagged error codes; debug-trap on failed `Result` dereference; dead accessors removed.
- [ ] `T23`: resize callback + live-aspect samples; simulated-resize projection gate.


---

## V4: Review follow-ups batch (archived 2026-08-28 — 19 tasks)

V4 was the review follow-ups gated sequential loop (TASKS.md T1–T19, reordered dependency-first). All 19 gates are green; `tools/logs/` artifacts have been purged for the next iteration. The backlog below is copied verbatim from `TASKS.md` at archive time (D/T/G + doc-map row) — no re-interpretation.

## T1: `ISceneObject` polymorphic hierarchy — 15+ types, open for extension (forced)

**D** — Discontinue `variant<MeshObject, …>` as the canonical family type
(`scene/object.hpp:144` today). With ≥15 object kinds and a growing set, a closed
variant is not feasible — every new kind edits the variant alias + every visitor.
Introduce `scene/iscene_object.hpp` `ISceneObject { virtual ~ISceneObject()=default;
virtual ObjectId id() const=0; virtual const glm::mat4& transform() const=0;
virtual uint64_t generation() const=0; virtual std::unique_ptr<ISceneObject> clone() const=0;
virtual SceneKind kind() const=0; }` + `ObjectBase<Derived>` CRTP mixin enforcing
`Kind`/`clone` at compile time and sharing the duplicated
`ObjectHeader{ObjectId, transform, generation, setTransform}`.
Fifteen concrete `objects/*.hpp` (Mesh/Volume/Plane/Contour + 10 new) each derive from
`ObjectBase<Derived>` and register via `REGISTER_SCENE_OBJECT(D)` static registrar into
`SceneFactory`/`Broker`. `scene/store.hpp` keeps 5 partitioned
`map<Id, unique_ptr<ISceneObject>>` (indirection #1 keeps `O(kind)` iteration —
no `5→1` erased scan) — a secondary `kindIndex_` restores typed iteration without
branch. Broker becomes `map<SceneKind, unique_ptr<ISceneMapper>>` (Strategy per Kind,
one file per mapper — `ISceneObject` data is processed only by its own mapper).
`T17` `AssetRef<T>` shared-ptr co-ownership stays (object is heap-allocated, asset stays
shared) — the extra `new` per object is amortised by a future slab/arena in
`SceneStore` (not this task). Today `MeshObject` copy is `memcpy` of a 64 B value into
the map node (`addMeshObject` copies by value, no heap for the object itself) — after the
move each object is `make_unique<D>` + map node (one heap for the wrapper, plus the
existing shared-ptr control block for the asset). This cost is accepted for open
extension; the alternative — bespoke `variant` visitor updates on every new kind — is
the blocked path. Semantics of `variant` (trivial copy, exhaustive `std::visit` compile
error) are replaced by virtual `clone()` + startup registry completeness check
(`Factory::create(kind)` fails loud if a Kind lacks a mapper) — runtime, not
compile-time exhaustiveness, but loud. Materials cascade only (per user `e`: lights stay
`variant` — `T1` does not cascade to `LightDesc`).

**Phases (one session, three incremental commits — size waived 2026-08-27 spec-review):**
- *Phase A — ISceneObject + ObjectBase + factory proof:* land `ISceneObject`,
  `ObjectBase<Derived>`, `REGISTER_SCENE_OBJECT` registrar, `SceneFactory` and 2–3
  example kinds (Mesh/Volume/Plane) with broker stub.
- *Phase B — Partitioned SceneStore:* migrate to 5 `map<Id, unique_ptr>` + `kindIndex_`
  typed iteration, preserve `O(kind)` guarantee.
- *Phase C — Broker registry migration:* `Broker` becomes
  `map<SceneKind, unique_ptr<IMapper>>` per-file Strategy; `variant<MeshObject` ==0.

**T** — gate: new `TeapotObject` (or any 16th kind not in the variant) renders through the bridge by adding one header + one `registerMapper<TeapotObject>` line, with zero edits to `store`/`ViewSynchronizer`; `grep -c "variant<MeshObject" scene/` == 0; `Broker::registeredTypes()` contains `TeapotObject` count 1; offscreen center pixel within 1/255 of analytic Teapot composite (not >0); suite green.

**G** — suite green, audit green.

## T2: `REContext` — global state mirror per GL context, multi-thread ready (rename `DrawContext`)

**D** — Rename `DrawContext` → `REContext` (`core/re_context.hpp` — not draw-only; also used by tests and readback, per user direction). Make the context **global per GL context** while preserving future multi-context/multi-threaded rendering: `REContext::current()` = `thread_local` pointer set by `loadCoreGl()` / `makeContextCurrent(GLFWwindow*)` mapping `GLFWwindow* → REContextState`; each context owns its mirror (viewport, clearColor, depthTest, blend, blendFunc, cull, FBO bindings, VAO, program, image units). Single-threaded gate stays (SPEC §5), but state is not process-global singleton — worker threads with private contexts get private mirrors with no lock; shared resources noted out-of-scope (GL share groups). Explicit invalidation at boundaries (`SampleHarness` post-ImGui, `invalidate()` public for tests) — no auto-guess. Delete per-frame local `ctx` instances in renderers; drop ignored `(void)ctx` params from `IRenderable::drawLayer`.

**T** — cross-pass dedup spy proves 2 layers sharing state issue 1 `glViewport`; `current()` switches correctly after `makeContextCurrent` to a second offscreen context; regression R3 byte-identical.

**G** — suite green, audit green.

## T3: Broker — pair-key {AppT, ReT} for get/register (A1)

**D** — `Broker::get<AppT,ReT>` keyed only on `AppT` (`broker.hpp:57-65`) then
`static_cast<IMapper<AppT,ReT>*>` — wrong `ReT` is UB. Fix: key by
`hash_combine(type_index(AppT), type_index(ReT))` for both `ownedByApp_` and
lookup/registration (`registerMapper` same). Mismatch → `nullptr` (typed error
downstream) instead of type-punning; same-`AppT`/different-`ReT` registrations
become distinct entries or asserted (no silent overwrite). `get<MapperT>` stays
exact-keyed as today.

**FR:** none (type-safety; no pixel change).

**T** — suite green; gate: `broker.get<MeshObject, ReWrongType>()` returns
`nullptr` (not a mis-typed pointer); registering `MeshObject→ReMeshObject`
then `get<MeshObject,ReMeshObject>` still finds it.

**G** — suite green, audit green.

## T4: Single draw-state cache — analysis then unify on REContext (R3)

**D** — Analysis-first cleanup of the two live regimes: `core/re_context.cpp` global
`g_cache`/`g_spy` + free functions (used by `LinkedListOIT`) vs instance
`core::REContext` (every pass prologue via `REContext::current()`). Do NOT trust the spec's EOL-5 verdict
a priori. Deliverable (1) analysis note: single-writer discipline per cached
state (viewport/clear/depth/blend), ImGui backend save/restore semantics, and
whether `LinkedListOIT` can take `REContext&` from the compositor flow;
(2) implementation: converge on `REContext`-everywhere — port `LinkedListOIT`
call sites (`linked_list_oit.cpp:193,274,276,280,289`) to accept `REContext&`
from `ViewCompositor` (which already creates one per view via `REContext::current()`), keep legacy global
free functions temporarily for regression-lock tests (`t2_skeletons_test.cpp:242`,
`t6_v2_draw_cache_test.cpp`) then migrate those tests and delete `g_cache`/`g_spy`
+ `invalidateDrawCache()` (legacy `core/draw.*` → `core/re_context.*`; T2 rename). No mixed regime remains; `invalidateDrawCache` test-only
discipline ends.

**FR:** no pixel change.

**T** — suite green; mechanical: `grep -c "g_cache\|g_spy\|invalidateDrawCache" core/` == 0 after migration (or allowlisted legacy shim with expiry note); OIT + prologues share one ledger via `REContext` (spy proves `setViewport` duplicate 2→1, analytic count 1 not `>0`); no skipped-glEnable class bugs possible.

**G** — suite green (N>=3 for OIT/compositor), audit green.

## T5: GL header firewall — move REContext body out of re_context.hpp (R4)

**D** — Make every `core/` public header GL-call-free again. Move `REContext`
inline GL calls/constants out of `core/re_context.hpp:23,174,205-270` (formerly `core/draw.hpp`) into
`core/re_context.cpp` (out-of-line), drop `<glad/gl.h>` from the public header.
Privatize `re_core`'s `glad`/`glfw` linkage where downstream includes permit
(`core/CMakeLists.txt:27-34` PUBLIC → PRIVATE with explicit downstream
deps). Add gate test: no `<glad` include in any installed/public `core/` header.

**FR:** none (header hygiene; R9 row).

**T** — suite green; `grep -R "#include.*glad" core/*.hpp` == 0 hits; downstream
targets (`render/`, `app/`, `tests/`) still build without transitive glad leak
(verified by including `core/re_context.hpp` in a minimal TU that forbids `<glad`).

**G** — suite green, audit green.

## T6: Infra/tests batch — helpers, monolithic binary, env coupling (IT1-IT5)

**D** — Batch infra: `IT2` extract `tests/test_helpers.{hpp,cpp}` (`makeQuadMesh`, `readPixel`, `expectPixel`, `WindowTarget`, `makeCamera`) — single source, ~150 lines removed leaving drift; `IT1`/`IT3`/`IT4`/`IT5` **deferred/documented** — monolithic `re_tests` + `tN_` naming + xvfb hard-fail are intentional gate choices (single context, task traceability, config-fail loudness) — add `IT` note in `docs/spec/nfr.md` and `NAMING_CONVENTIONS.md` rather than restructure now. Double-checked. **Ownership split vs T18 (spec-review #5):** `T6` helpers keep `makeQuadMesh`/`makeCamera`/`WindowTarget` only; pixel-read moves entirely to `test_utils/` in `T18` (`test_utils::PixelReader` via `REContext::readRgba8`/`REContext::current().readRgba8`, raw `glReadPixels` stays `core/re_context.cpp` count 1, `test_utils` count 0). After T18, `grep -c "readPixel" tests/test_helpers.*==0` (deprecated helper migrated), `T18` owns `PixelReader` façade migration (`grep -R "glReadPixels" -- core/ ==1` + `-- test_utils/ ==0`).

**FR:** no gate change.

**T** — suite green; gate: `grep -c "makeQuadMesh" tests/*.cpp` == 1 (helper, analytic count 1); suite still single binary (`ctest -V` shows 1 test `re_tests`); monolithic binary intentional (single GL context).

**G** — suite green, audit green.

## T11: ASan+UBSan for all engine libs (R7)

**D** — Instrument all nine `re_*` static libs, not just test/sample TUs.
Define `INTERFACE` target `re_project_sanitizers`
(`-fsanitize=address,undefined -fno-omit-frame-pointer -O1` under Debug, option-gated)
linked by every `re_*` target; delete ad-hoc per-dir flag blocks
(`tests/CMakeLists.txt:84-90`, `app/CMakeLists.txt:97-103`). Verify sample binaries
remain clean (llvmpipe/Mesa false-positive triage if needed via
`ASAN_OPTIONS` suppressions already proven). Keep Release non-instrumented via
`option(RE_ENABLE_SANITIZERS)`.

**FR:** `SPEC §5` sanitizer contract now covers intra-library errors (stack/scope/intra-object).

**T** — suite green with sanitizers on all libs; `cmake --build --verbose` graph shows
`re_core` etc. compile with `-fsanitize` (exact `grep -c "\-fsanitize.*re_" ==9`, `grep -c "add_compile_options.*-fsanitize" ==0`, known driver suppressions documented).

**G** — suite green, audit green.

## T7: Owner-driven AssetHandles for volumes/images — eliminate per-frame content hashing (R1)

**D** — Make volume/image identity owner-driven like meshes (follow the proven
`registerMeshAsset → AssetId → AssetHandle` path). Today `lookupVolume`/
`lookupImage` recompute FNV-1a over every byte per instance per frame
(`render/asset_registry.cpp:404,459`) violating `data/content_hash.hpp:31`
("hashed at load/register time, never per frame"). Work, staged:
(1) broker mappers (`VolumeObjectMapper`, `VolumeSliceObjectMapper`,
`PlaneObjectMapper`/`PlaneMapper`) register volumes/images through
`SceneStore`/`broker::AssetStore` at sync, handing renderers `AssetHandle`
instead of `shared_ptr<const T>`; renderers' `textureFor` becomes O(1) handle
resolve. Volumes first, then images. (2) delete lazy-hash `lookupVolume`/
`lookupImage` insertion paths and contract-violating comment; keep explicit
register→resolve only. Direct-renderer tests register explicitly in fixtures
(or via a shared test helper). This also closes R8a/R8b: pinned refs==0 lazy
slots can no longer appear and the `byObject_` pointer-key shim is deleted —
content-hash IS identity.

**FR:** no pixel change — `FR-render.*`/`FR-app.*` gates unchanged (regression lock).

**T** — gate asserts (explainable): spy counter proves `hashStableBytes`/FNV
executes **zero** times during a steady-state 60-frame loop after warm-up
(volume + plane); registry slot count constant across 1000 distinct-image
frames (no pinned-slot growth); same `VolumeDataset` registered through two
`VolumeRenderer` instances yields one `Texture3D`; suite green N>=3.

**G** — suite green (N>=3), audit green, lazy-hash lookup paths removed
(`grep -c "hashStableBytes" render/asset_registry.cpp ==0` — T7 owns per-frame hash deletion; per-renderer `textureFor` map removal owned by `T14` `grep -c "textureFor" render/*_renderer.* ==0`).

## T8: OIT memory docs — document no-fallback contract (R2)

**D** — No code fallback. Deployment targets are known, so OIT may legitimately
fail when the SSBO budget (`w*h*16*32`, ~1 GB @1080p) cannot be satisfied.
Align documentation with reality: `docs/render.md` OIT section + `docs/spec/nfr.md`
+ `render/itransparency_pipeline.hpp:47-48` comment are corrected — `begin()`
failure typed error aborts the transparent-capable mesh pass (no silent
blend fallback). Document capacity: per-view cost formula, example table
(640×480≈152 MB, 1080p≈1 GB), and that unsupported/over-budget hardware yields
opaque-only rendering for that pass (typed error surfaced via bridge).

**FR:** none (docs-only; R9 row).

**T** — suite green; `grep -c "renders without OIT" render/itransparency_pipeline.hpp` == 0 (fixed); mechanical doc gates: `grep -c "640×480.*152" docs/render.md ==1` and `grep -c "w\*h\*16\*32" docs/spec/nfr.md ==1`; per-view cost `w*h*16*32` verified — `640×480=152 MB` (4915200*32), `1920×1080≈1.03 GB` analytic, not `>0`.

**G** — suite green, audit green.

## T9: Broker polish — alias removal, cycle, dirty granularity, generation dedup (A2, A3, A5, A6, A7, A8, A4 note)

**D** — Batch broker/`scene` polish trivially deduced during review:
`A2` delete `aliasByApp_` (derive from `ownedByMapper_` — fixes stale raw alias);
`A3` remove `ViewSynchronizer::weak_ptr<ViewCompositor>` cycle — `ViewBridge::sync` passes `compositor*` explicitly;
`A5` add `clearColorGen`/`depthTestGen` per-field gens to `scene::View` + `ViewCache`;
`A6` extract shared `detail::GenerationTracker` for `SceneStore`/`ViewStore` (`recordDirty_`, `dirtyFieldsSince`, tombstones);
`A7` template-generate six `SceneStore` method families and add symmetric `count()` for all kinds;
`A8` unify `SceneStore` staleness contract (typed `resolve` + borrowed accessors with `@note lifetime:`);
`A4` doc-only: `presentAll(core::Framebuffer*)` leak acknowledged, deferred to RHI `IRHIFramebuffer` (note in `docs/render.md` + `broker.md`). All double-checked before fixing — concerns flagged inline.

**Phases (one SRP — broker polish — three incremental commits, waived 2026-08-27):**
- *Phase A (A2+A3 — broker wiring):* delete `aliasByApp_`, remove `weak_ptr<ViewCompositor>` cycle; no API change.
- *Phase B (A5+A6 — generations):* add per-field `clearColorGen`/`depthTestGen`, extract shared `GenerationTracker`.
- *Phase C (A7+A8 — store consistency):* template-generate method families, add `count()`, unify staleness contract (A4 doc defer).

**FR:** no pixel change; broker API tightened.

**T** — suite green; gates: `grep -c "aliasByApp_" broker/` == 0; `grep -c "weak_ptr.*ViewCompositor" broker/` == 0; `setClearColor` bumps dedicated gen; `SceneStore`/`ViewStore` share one `GenerationTracker` impl (no hand-copied duplicate); all six `count()` present.

**G** — suite green, audit green.

## T10: Render internals batch — dedup prologue/quad/constants, perf fixes (RI1-RI9)

**D** — Batch render dedup + perf (all trivial, double-checked before fixing):
`RI1` one `LazyProgramCache` for the eight shader loaders; `RI2` unify GLSL clip classifier epsilons or document divergences; `RI3` single `kMaxTfPoints=8` header + `tfSample` shared include, OIT `kNullNode`/stride/cap constants single-sourced; `RI4` remove per-frame TF vector allocs (reuse/stack); `RI5` hoist `inverse(uViewProj)` to CPU uniform; `RI6` Contour `drawLayer` sets `uView/uProj/uViewport` once; `RI7` document `RE_SHADER_DIR` reloc note (or install/copy shaders) — keep baked path but gate warns; `RI8` ViewTarget resize via `glClear` not zero-upload; `RI9` drop dead `aNormal` pipeline. `RI10` `captureCrossSection` worst-case alloc documented as test-only (WONTFIX).

**Phases (one SRP — render dedup/perf — three incremental commits, waived 2026-08-27):**
- *Phase A (RI1/RI3 — constants):* single-source `LazyProgramCache`, `kMaxTfPoints`, `kNullNode`/OIT constants.
- *Phase B (RI2/RI5/RI6 — shader/uniforms):* unify clip epsilons, hoist `inverse(uViewProj)`, Contour uniform hoist, drop `aNormal`.
- *Phase C (RI4/RI8/RI9 — alloc/perf):* remove per-frame TF allocs, `glClear` resize, document `RE_SHADER_DIR` + RI10 WONTFIX.

**FR:** zero pixel drift on all renderer gates within 1/255 (N>=3).

**T** — suite green (N>=3); greps: `kScreenQuadVerts`/`LazyProgram` deduped, `grep -c "inverse(uViewProj)" render/shaders/ ==0 && grep -c "uInvViewProj" render/shaders/ >=1, per-frame TF alloc count == 0; pixel drift 0 within 1/255 N>=3.

**G** — suite green, audit green.

## T12: Validation gaps batch — uniforms, texture/FBO checks, parsing, Result, Aabb, EGL, hygiene (VG1-VG5, VG7-VG12)

**D** — Batch validation hardening trivially deduced: `VG1` `setUniform*` checks `-1` + location cache (no per-call `std::string` alloc); `VG2` texture unit range `0..15` assert; `VG3` FBO attach/isComplete bind-state asserts; `VG4` `read_pixels` overflow check + `PACK_ALIGNMENT` save/restore; `VG5` OBJ `strtol` `ERANGE` check + `errno` reset; `VG7` `Result<T>` `[[nodiscard]]` on type, `Error` embed retained but documented, monadic `map/andThen` helpers retained from T22 as optional; `VG8` single `Aabb` canonical type (or type-alias) with one default; `VG9` `utils/CMakeLists` EGL `REQUIRED` → optional + `AUDIT_SOURCE_DIRS` grey-zone doc; `VG10` anon-namespace internals in `shader_program.cpp`; `VG11` optional `assert(hasPendingGlError())` debug hook in core wrappers; `VG12` retire `pinned_deps_anchor.hpp` shim, audit `queryGlError` usage, Window teardown dedup, logging level knob doc. `VG6` already via T16. Runs **sanitized** via `T11` `re_project_sanitizers` (dependency order — `T11` precedes `T12`).

**Phases (one SRP — validation hardening — three incremental commits, waived 2026-08-27):**
- *Phase A (VG1-3 — GL validation):* `setUniform` -1 check + cache, texture unit 0..15, FBO attach/isComplete, `read_pixels` overflow + `PACK_ALIGNMENT`.
- *Phase B (VG5+VG7 — parsing/Result):* OBJ `strtol ERANGE`, `Result [[nodiscard]]`, monadic helpers.
- *Phase C (VG8-12 — hygiene):* single `Aabb`, EGL optional, anon-namespace, `hasPendingGlError` hook, retire shim, logging knob.

**FR:** no pixel change; loaders become stricter (malformed giant index → typed error, not silent wrong geometry).

**T** — suite green (ASan+UBSan clean via T11); gates: uniform typo → silent no-op gone (logged/warned, location cache hit count exactly 1 not `>0`); `bind(16)` asserts out-of-range (analytic bound 15); malformed OBJ index → typed error `ERANGE`; `Aabb` single definition (count 1); build with EGL missing still configures (0 PkgConfig failures).

**G** — suite green, audit green.

## T13: NRRD loader size pre-probe with typed error (R10)

**D** — Check file size before the whole-file slurp that today precedes budget
validation. Probe `std::filesystem::file_size` (or `stat`) before reading;
compare against derived ceiling (axis limits 128³ × dtype size) and absolute
cap; on exceed return typed `BudgetExceeded` + `spdlog::warn` with actual vs
limit sizes. Keeps the glitch as "silent failure with appropriate logs" per
user call (typed error to caller, warn log for diagnostics). No behavior
change for valid files.

**FR:** `FR-io.3` preserved; hostile-size file fails fast with typed error, not OOM.

**T** — suite green; gate: >128³ or host-file-size > cap input → typed error
code `BudgetExceeded` and no multi-GB allocation observed (mocked large file
or synthetic header with huge dims); valid volume still loads byte-identical.

**G** — suite green, audit green.

## T14: Collapse transparency to one path — delete IRenderer::render(Scene) variant (R5 + A9)

**D** — Make the bug class unrepresentable. Delete the test-only
`IRenderer::render(const Scene&)` variant dispatch (`render/types.hpp:8-17`
"kept ONLY for the direct single-item render() tests") and the `Scene` alias
`variant<const MeshScene*, ...>`. Keep only `IRenderable::drawLayer` (the broker
path), which has one defined transparent-mesh behavior (compositor's
out-of-band capture when `ITransparencyPipeline` is wired; `drawLayer`
otherwise draws with blending off — never the silent-drop of the direct path).
Port the four dispatch-site tests: `t1_v2_ir_dispatch_test.cpp` exercise moves to
`beginPass` + `drawLayer` via a minimal `View`/`REContext`. Removes the four
copies of dispatch boilerplate and the parallel `re_scene/mesh_object.hpp`
vocabulary's second entry point; pairs with `T2` consolidation but is
independently gated. Also closes A9 vestigial dispatch debt.

**FR:** no pixel change for any gate reached via the broker path (regression lock);
direct-renderer tests ported byte-identical within 1/255.

**T** — suite green; `grep -c "IRenderer::render\|using Scene =" render/` == 0;
`grep -c "Noop\|byObject_" broker/` == 0 (Noop already deleted by T20/T7; this gate adds the dispatch-removal proof; `lookupVolume` hash-path proof owned by `T7` `hashStableBytes==0`); no
transparent-mesh silent-drop path remains (`mesh_renderer.cpp` skipTransparent=true
drop only survives if ever reintroduced — asserted absent) + `grep -c "textureFor" render/*_renderer.* ==0` (per-renderer maps deleted, distinct from `T7` hash deletion).

**G** — suite green (N>=3), audit green.

## T15: GLFW global lifecycle — refcounted GlfwRuntime + tests↛window audit (R6)

**D** — Two owners (`core::Window` visible, `utils::OffscreenContext` hidden
for tests) share one process-global `glfwInit/glfwTerminate` pair with
mismatched policies (`Window` always terminates at `window.cpp:66`,
`OffscreenContext` never does at `offscreen_context.cpp:89-92`). Introduce
`core::GlfwRuntime` — `static mutex + int refs` refcounted RAII
(`acquire()` → `shared_ptr` token: 0→1 `glfwInit`, 1→0 `glfwTerminate`);
both `Window` and `OffscreenContext` hold `shared_ptr<GlfwRuntime>`
instead of raw calls. Add mechanical guardrail: `tools/audit.rules` forbids
`tests/` including `core/window.hpp` (enforces "tests use only offscreen").

**FR:** no behavior change beyond correct global teardown; window creation
smoke still passes.

**T** — suite green; order-independent teardown: `OffscreenContext` + `Window`
created in either order and destroyed in either order leaves no UB/leak with
`GlfwRuntime::refCount()==0` after both destroyed and ASan/LSan clean (simulate via
fixture interleaving test); `grep -R "glfwTerminate" -- core/ utils/`
== 1 hit (inside `glfw_runtime.*` only); `grep -R "window\.hpp" tests/` == 0.

**G** — suite green, audit green.

## T16: TransferFunction — valid default + defensive sample + toByte clamp (R9 + VG6)

**D** — Empty `TransferFunction` is UB (`sample()` derefs `front()` on empty
`transfer_function.cpp:16`) yet ctor accepts any vector (`hpp:34-38`). Fix
(a)+(c): default ctor produces a valid degenerate ramp pinned
`vec4(0,0,0,0) → vec4(1,1,1,1)` (transparent black→opaque white) instead of empty;
`sample()` defensively returns transparent black if points empty; `toByte`
(`mpr_slice.cpp:22-24`) clamps with `std::clamp(v,0.f,1.f)` before `uint8_t` cast
(closes the float→int UB for any out-of-range TF color). Factory `(b)`
(`Result`-returning) deferred as optional.

**FR:** no pixel change for existing TFs (regression lock).

**T** — suite green; gate: `TransferFunction{}.sample(0.0f)==(0,0,0,0)`,
`sample(0.5f)==(0.5,0.5,0.5,0.5)` within 1e-6, `sample(1.0f)==(1,1,1,1)`,
`toByte(1.5f)==255` and `toByte(-0.2f)==0` both clamped and defined (no UB); ASan clean.

**G** — suite green, audit green.

## T17: App/samples batch — CT dedup, harness, hardcoded ids, resize gap (AS1-AS7)

**D** — Batch app polish: `AS1` one `makeCtTransferFunction()` in `app/` shared header; `AS2` `runSample()` helper dedups six `sync→renderAll→presentAll + load→window→run` mains + constants; `AS3` `oit_sample` capture returned `ObjectId`s (no `{1,2,3,4}` hardcode); `AS4`/`AS5`/`AS6`/`AS7` noted but **deferred** per T23 scope overlap — `T23` already owns `onResize` + live aspect, so AS4/AS5 feed into T23; AS6 PPM/box/oracle stays library-grade-documented (no move this batch); AS7 background clear stays sample-side until `presentAll` compositing lands. Double-checked — no larger refactor this task.

**FR:** sample smoke still exits 0; OIT ids stable across store policy changes.

**T** — suite green; gate: `grep -c "makeCtTransferFunction" app/` == 1 definition; `oit_sample` no hardcoded `{1u,2u,3u,4u}`; `runSample` present.

**G** — suite green, audit green.

## T18: Test-support extraction to `test_utils/` — keep RE critical code lean, GL via `REContext`

**D** — Move test-consumed surface out of critical RE into a peer lib `test_utils/` (`AUDIT_SOURCE_DIRS += test_utils`). Candidates identified by the architecture review / user direction: `core/read_pixels.{hpp,cpp}` raw `glReadPixels` anchor (`core/read_pixels.hpp:30` — every pixel-gate test's evidence path), `utils/pixel_reader.*`, `render/linked_list_oit::readCapturedFragmentCount()`, `render/slice_renderer::captureCrossSection()` + `TransformFeedback` harness. `utils/offscreen_context.*` **stays in `utils/`** (owned by `T15 GlfwRuntime` — `T15` and `T18` no longer collide; `T15` owns `OffscreenContext` lifetime via `GlfwRuntime`, `T18` owns `PixelReader`/`read_pixels`/capture helpers only). Raw `gl*` stays exclusive to `REContext` (`core/re_context.cpp` is the only `glReadPixels`/`glGetBufferSubData` site; audit `gpu_api_ownership` / `no_production_readback` now allow `core|test_utils` — raw stays `core`, façade in `test_utils`). New peer lib `test_utils/` (`AUDIT_SOURCE_DIRS += test_utils`, `disposition` rules updated). `test_utils::PixelReader` calls `REContext::current().readRgba8(…)`, not a second anchor. **Constraint:** every context-setting GL call still flows through `T2 REContext` — no test helper touches raw GL.

**T** — `grep -R "glReadPixels" -- core/` == 1 hit (inside `re_context.cpp`), `grep -R "glReadPixels" -- test_utils/` == 0; suite still green via `test_utils::PixelReader`; `utils/offscreen_context.*` remains in `utils/` (not moved).

**G** — suite green, audit green.

## T19: `View` explicit lights field — *(stretch, deferred)*

**D** — *(stretch — deferred per SPEC §1 non-goal Phong-only + SPEC §12.1/12.3 `ILight` hierarchy stays spec-only this iteration; promotes to binding only when SPEC §1/§12 promote lights)* `scene::View` and `render::View` gain `vector<Light> lights` (was implicit/absent). App: `Light { Type dir/point/spot; vec3 pos/dir; vec4 color; float intensity; … }` + `setLights()` bumping `lightsGen` (adds to `CompositeKey` per §10). RE: `ReLight` (derived uniform-ready) uploaded per view before `drawLayer` loop; empty vector = unlit (2D). Broker: `LightMapper : IMapper<Light,ReLight>` + `ViewMapper` composes `LightMapper`. Persistence: `lightsGen` participates in `ViewSynchronizer` dirty check (per-field, not whole-view dump). This task is **not required for V3 green**; SPEC §12 `Light` hierarchy remains spec-only until promoted — **gate only enforced when SPEC §1/§12 promote lights; otherwise DoD line waived (stretch)**.

**T** — two lights on one view produce analytic two-light composite distinct from single-light within `1/255` at probe; empty lights = unlit as before — **stretch gate: only enforced when lights promoted per SPEC §1/§12; waived until then**.

**G** — suite green, audit green — **stretch deferred; not required for V3 green while SPEC §1/§12 Phong-only non-goal holds**.

## Definition of Done — review follow-ups (T1–T19 reordered, dependency-first, 1:1 with D)

**Loop artifacts (generic, every T):**
- [ ] `suite green` — `N>=3` for any GL/readback/OIT/spy task (`T4` draw-cache spy, `T7` handles 60-frame hash 0 + slot growth 0, `T8` OIT, `T10` dedup zero drift, `T11` sanitizer N>=3 toolchain prove, `T12` VG1-3 texture/FBO/REContext spy where GL-touching, `T14` collapse, `T15` GlfwRuntime order-independent teardown, plus any `readRgba8`/`glViewport` spy gate; `tools/logs/task_*.gate.log` shows 3 consecutive `ctest Passed`, `GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6`), `N>=1` otherwise (pure CPU/logic tasks `T3` pair-key, `T5` firewall, `T6` helpers, `T9` polish where CPU-only, `T13` NRRD pre-probe, `T16` TF, `T17` app batch where harness smoke, `T18` test_utils façade (0 raw), `T19` stretch lights) — **GL-touching subset of `T12` inherits `N>=3`; pure-CPU subset of `T12` is `N>=1` sanitized via T11**
- [ ] `audit green` with `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` — `tools/audit.sh` PASS incl. `disposition_scene/render` (mechanical `forbid_inside`), `broker_per_type`, `no_dump_sync`, `asset_indirection`, `ownership_raw_ptr_*`, `comment_tag_context`, `render_no_glad`, `no_noop_broker`, `assets_licensed` per-dir (audit floor `require_grep LICENSE` is floor only; per-dataset-dir gate `test -f data/meshes/LICENSE && test -f data/volumes/LICENSE` via T2 enforces completeness, audit.sh complements with `git ls-files` per-dir check where present)
- [ ] `ASan+UBSan clean` on all `re_*` libs (not just tests) + samples exit 0 under `xvfb` (FR-app.1) — `option(RE_ENABLE_SANITIZERS)` ON for Debug, `ASAN_OPTIONS` suppressions documented in `docs/spec/env.md` + `docs/spec/nfr.md`
- [ ] `LICENSE` beside every dataset dir (`data/meshes/LICENSE`, `data/volumes/LICENSE` enumerated) — `test -f data/meshes/LICENSE && test -f data/volumes/LICENSE` + `grep -c LICENSE` floor — T2 gate
- [ ] `R9` doc-map: `git diff --name-only` includes listed docs per T-map row (review checks)

**Per-task gates (reordered T1–T19, 1:1 with D):**
- [ ] `T1` hierarchy: `ISceneObject` base + `ObjectBase<Derived>` — 15+ types open via `REGISTER_SCENE_OBJECT`, `variant<MeshObject` ==0, partitioned store `O(kind)` (5 maps + kindIndex_), Broker `map<SceneKind,unique_ptr<IMapper>>` — suite green (3 phases A/B/C).
- [ ] `T2` REContext: `DrawContext→REContext` global per-GL-context `thread_local current()`, `g_cache` deleted, cross-pass spy 2→1 proves dedup, second offscreen context switches `current()` — suite green.
- [ ] `T3` pair-key: `Broker::get<AppT,ReT>` pair-key `{AppT,ReT}` via `hash_combine(type_index(AppT),type_index(ReT))` — wrong `ReT` returns `nullptr` not UB (typed miss); distinct registrations for same AppT/different ReT — suite green.
- [ ] `T4` draw-cache: single-writer analysis note + unify on `REContext` (port `LinkedListOIT` to `REContext&`, `g_cache/g_spy/invalidateDrawCache==0`, legacy `core/draw.*` → `core/re_context.*`) — spy proves `setViewport` duplicate 2→1 (count 1 not >0) — suite green N>=3.
- [ ] `T5` header firewall: no `<glad` in any `core/*.hpp` (`grep -R "#include.*glad" core/*.hpp ==0`), `core/CMakeLists` `glad` PRIVATE — minimal TU including `core/re_context.hpp` builds without transitive glad leak.
- [ ] `T6` infra batch: `tests/test_helpers.*` single source `makeQuadMesh==1` (helper, analytic count 1), `re_tests` single binary preserved (`ctest -V` shows 1 test) — suite green.
- [ ] `T7` owner-driven handles: volume/image `lookupVolume/Image` lazy-hash deleted, spy hash count 0 over 60-frame loop after warm-up (volume+plane), registry slot growth 0 over 1000 distinct-image frames, same VolumeDataset via two renderers → one Texture3D — suite green N>=3.
- [ ] `T8` OIT docs: per-view cost `w*h*16*32` table verified — `640×480=152 MB` (4915200*32), `1920×1080≈1.03 GB` (analytic ~1037 MB) + `grep "renders without OIT" render/itransparency_pipeline.hpp ==0`; doc mechanical: `grep -c "640×480.*152" docs/render.md ==1` and `grep -c "w\*h\*16\*32" docs/spec/nfr.md ==1`.
- [ ] `T9` broker polish: `aliasByApp_==0`, `weak_ptr<ViewCompositor>==0`, `setClearColor` bumps dedicated `clearColorGen`, `SceneStore/ViewStore` share one `GenerationTracker` impl, all six `count()` present — suite green (3 phases).
- [ ] `T10` render dedup: `kScreenQuadVerts`/`LazyProgram` deduped, `grep -c "inverse(uViewProj)" render/shaders/ ==0 && grep -c "uInvViewProj" render/shaders/ >=1, per-frame TF alloc 0, `kMaxTfPoints==8` single-sourced — zero pixel drift 1/255 N>=3 (3 phases).
- [ ] `T11` sanitizers: `INTERFACE re_project_sanitizers` (`-fsanitize=address,undefined -fno-omit-frame-pointer -O1` Debug, option-gated) linked by all 9 `re_*` libs, `cmake --build --verbose` shows `-fsanitize` on `re_core` etc. (exact `grep -c "\-fsanitize.*re_" ==9` + `grep -c "add_compile_options.*-fsanitize" ==0`), ad-hoc per-dir flag blocks deleted (`tests/CMakeLists.txt:84-90`, `app/CMakeLists.txt:97-103`), known driver suppressions via `ASAN_OPTIONS` documented — suite green.
- [ ] `T12` validation: uniform `-1` checked + location cache hit count exactly 1 (not >0), `bind(16)` asserts out-of-range (bound 15), malformed OBJ index → typed error `ERANGE` (`errno` reset), single `Aabb` definition (count 1), build with EGL missing still configures (0 PkgConfig failures) — suite green (sanitized via T11) — **N>=1 sanitized, N>=3 for GL-touching VG1-3 (`setUniform`/`bind`/`FBO`/`PACK_ALIGNMENT` spy where GL context involved; `T11` sanitizers already prove `N>=3` toolchain, validation CPU gates `N>=1` but GL spy gates `N>=3`)**.
- [ ] `T13` NRRD pre-probe: `std::filesystem::file_size` before slurp; >128³ or >cap → typed error `BudgetExceeded` + `spdlog::warn` with actual vs limit, no multi-GB alloc (mocked large file or synthetic huge header); valid volume loads byte-identical — suite green (Depends:none, sanitized N>=1; if after `T11` then ASan clean via `re_project_sanitizers`).
- [ ] `T14` collapse variant: `IRenderer::render(Scene)` + `using Scene =` variant deleted (`grep -c "IRenderer::render\|using Scene =" render/ ==0`), `drawLayer` only (broker path); 4 dispatch-site tests ported via `View/REContext`, no transparent-mesh silent-drop path remains — suite green N>=3.
- [ ] `T15` GlfwRuntime: `core::GlfwRuntime` refcounted RAII (`static mutex + int refs`, `acquire()->shared_ptr`); order-independent teardown (OffscreenContext+Window either order leaves `refCount()==0` + no UB/leak, ASan/LSan clean, `grep -R "glfwTerminate" -- core/ utils/ ==1` inside `glfw_runtime.*` only, `grep -R "window\.hpp" tests/ ==0`) — suite green.
- [ ] `T16` TransferFunction: default ramp pinned `vec4(0,0,0,0) → vec4(1,1,1,1)` — `sample(0.0)==(0,0,0,0)`, `sample(0.5)==(0.5,0.5,0.5,0.5)` within 1e-6, `sample(1.0)==(1,1,1,1)`; `toByte(1.5f)` clamped 255, `toByte(-0.2f)` clamped 0 — ASan clean.
- [ ] `T17` app batch: `grep -c "makeCtTransferFunction" app/ ==1` definition (shared header), `runSample()` present deduping six mains, `oit_sample` no hardcoded `{1u,2u,3u,4u\}`; sample smoke exits 0, OIT ids stable across store policy — suite green.
- [ ] `T18` test_utils: `grep -R "glReadPixels" -- core/ ==1` inside `re_context.cpp`, `grep -R "glReadPixels" -- test_utils/ ==0`; `test_utils::PixelReader` via `REContext::current().readRgba8` (no second anchor), `AUDIT_SOURCE_DIRS` includes `test_utils`, disposition + gpu ownership allow `test_utils` façade only — suite green.
- [ ] `T19` View lights *(stretch — deferred)*: `scene::View` and `render::View` gain `vector<Light> lights` (`setLights()` bumps `lightsGen` into `CompositeKey`); Broker `LightMapper : IMapper<Light,ReLight>` + `ViewMapper` composes; two lights on one view produce analytic two-light composite distinct from single-light within 1/255 at probe; empty vector = unlit (2D) as before — suite green; not required for V3 green while SPEC §1/§12 Phong-only non-goal holds.

