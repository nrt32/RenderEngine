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