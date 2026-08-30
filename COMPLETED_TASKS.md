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

**T** — audit-enforceable floor: extend `tools/audit.rules` with an ownership rule (e.g., forbid public API/members of pattern `(const )?[A-Z][A-Za-z_]*\*[[:space:]]*[a-z]` in scene//broker//app/ headers outside an allowlist of documented borrows); suite green; every remaining raw pointer carries a Doxygen `@note lifetime:` tag naming its owner; reorder-hazard sample member groups converted to explicit two-phase init or shared ownership.

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
cap; on exceed return typed `BudgetExceeded` (> Historic — superseded by V5 T11 No-cap streaming via `core::Caps` tiled `1/255`, see `docs/spec/frs.md:18` `T11 supersedes T13 cap`; active contract is tiled via Caps` + `spdlog::warn` with actual vs
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


---

## V5: Extensibility & visualization reuse (archived 2026-08-28 — 19 sessions, T1..T17+T8b/T11b)

V5 was the Sr. Architect review iteration for visualization reuse (GLFW accepted, boilerplate hostility, redundant classes, abstraction gaps). All 19 gates are green (17 base + T8b DepthConfig + T11b OIT fallback, per Q1/Q2 splits); `tools/logs/` artifacts have been purged for the next iteration. The backlog below is copied verbatim from `TASKS.md` at archive time (D/T/G + doc-map row + FR→T traceability) — no re-interpretation.

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

## T1: Engine facade — `viz::Engine` one-liner for visualization consumers (P0)

**D** — Publish `include/render_engine/engine.hpp` (`viz::Engine` or `re::viz::Engine`) that hides `SceneStore`/`Broker`/`AppContext`/`TranslateContext`/`CompositeKey`/`GenerationTracker` for the 80% case. Facade owns an `AppContext` + `SceneStore` internally and exposes: `Result<ObjectId> addMesh(path, mat4, Material)`, `addVolume(path/tf, mat4)`, `setView({rect,camera,ids})`, `Result<void> render(Framebuffer&)` / `render(windowFb)`, plus `appContext()`/`store()` accessors for advanced users (broker path stays). Single-site helper `Engine::createView` covers `fitPerspectiveViewToPixels` + `Rect` + `Camera` ceremony. No `CompositeKey` in public header.

**FR:** none new — facade forwards to existing `AppContext::bridge().sync/renderAll/presentAll` (broker path unchanged) so all `FR-render.*`/`FR-app.*` stay via R3.

**T** — gate: `Engine e; auto id = e.addMesh("data/meshes/bunny.obj", I, mat); e.setView({{0,0,800,600}, cam, {id}}); e.render(fb)` center pixel within 1/255 of direct `AppContext` path (`Engine` vs direct `AppContext` oracle, `N>=3` via offscreen fixture, analytic color not `>0`); `grep -c "class Engine" include/render_engine/` == 1; sample `mesh_sample` can be reduced to 20 lines via facade (kept as comment/example, not required).

**G** — suite green (`N>=3` for `Engine` vs direct pixel gate), audit green, `Engine` header in `include/` + `docs/engine.md` facade docs.

## T2: CMake install/export — make RE a real library (P0)

**D** — `CMakeLists.txt` + `cmake/RenderEngineConfig.cmake.in`: `install(TARGETS re_core re_scene re_broker re_render re_app EXPORT RenderEngineTargets)`, `write_basic_package_version_file` (semver 0.1), `install(EXPORT ...)`, `install(DIRECTORY include/ ...)`, `install(DIRECTORY scene/ TYPE INCLUDE FILES_MATCHING *.hpp)` only where needed. Privatize `re_core`'s `glad`/`glfw` linkage (`core/CMakeLists.txt:27` `PUBLIC` → `PRIVATE` with explicit downstream `target_link_libraries(render PRIVATE glad)` where `REContext` body needs it) so header firewall T5 is not leaked via `INTERFACE`. `FetchContent` deps become `find_package` fallbacks (not forced `FetchContent` in consumer). `re_project_sanitizers` `INTERFACE` not installed on Release.

**T** — gate: `cmake -S . -B /tmp/re_build && cmake --build /tmp/re_build -j && cmake --install /tmp/re_build --prefix /tmp/re_inst && test -f /tmp/re_inst/lib/cmake/RenderEngine/RenderEngineConfig.cmake` (path existence) plus `grep -c "add_compile_options.*-fsanitize" ==0` still and `grep -R "#include.*glad" core/*.hpp ==0` (firewall) **and analytic pins:** `grep -c "RenderEngineTargets" /tmp/re_inst/lib/cmake/RenderEngine/RenderEngineTargets.cmake ==1` && `grep -c "write_basic_package_version_file.*0.1" CMakeLists.txt ==1` (version `0.1` per `T2:D`) — `find_package` smoke moved to `T13` (no forward ref).

**G** — suite green, audit green, `cmake --install` reproduces.

## T3: Harness decoupling — `Window` + `ImGuiOverlay` + `FrameLoop` + bounded-run discipline (P0, supersedes draft T2 — note: draft was P2, now P0 because `T4` offscreen depends on it)

**D** — Split `app/sample_harness.*` (`SampleHarness::run(maxFrames)` `app/sample_harness.cpp:67`) into: `app/frame_loop.hpp` (`FrameLoop{ poll(), render(), present() }` free function `Result<void> renderViews(span<View>, SceneStore&, Framebuffer&)` callable without `Window`), `app/imgui_overlay.hpp` (optional overlay, not owned by loop), `core/window.hpp` stays. Keep **bounded `run(maxFrames)` as the sole public contract**; add `runInteractive()` as opt-in helper only — `runSample` dispatches via `sampleMaxFrames(kDefaultFrames)` (`app/sample_harness.hpp:191`) when `RE_SAMPLE_MAX_FRAMES` is set (CI bounded) and **defaults to bounded `kDefaultFrames` (e.g., 20) when the var is unset** — interactive `until shouldClose()` only via explicit `runInteractive()` opt-in. Samples' `main()` keeps bounded dispatch; harness never hangs CI when env var is forgotten (bounded default, not interactive). Remove `SampleHarness::initImGui` hard-coupling (`sample_harness.cpp:29` → overlay). This is the **prerequisite for T4 offscreen** — `renderViews` is the window-free render helper that `T4:renderOffscreen` will reuse.

**T** — gate: new `renderViews(views, store, fb)` renders without `Window` — center pixel within 1/255 of `SampleHarness` path (`N>=3` via offscreen fixture, analytic not `>0`); `RE_SAMPLE_MAX_FRAMES=20` smoke still exits 0 under Xvfb, and run without env var defaults to bounded `kDefaultFrames` (no hang); `grep -c "ImGui_ImplGlfw_InitForOpenGL" app/sample_harness.cpp ==0` (overlay owns it); **MPR `FR-app.2` preserved (via `renderViews` layout path):** window `1280×960` + four `640×480` viewports at `(0,0)/(640,0)/(0,480)/(640,480)` exact (within 1 px) + axis convention `T=Z, C=Y, S=X` per-view pixel probe (analytic, not visual).

**G** — suite green (`N>=3` for parity gate), audit green.

## T4: Headless/offscreen public API — server-side visualization (P0, depends on T3)

**D** — Promote `utils::OffscreenContext` (`utils/offscreen_context.hpp`) + `core::loadCoreGl` + `REContext::current().readRgba8` to public `core/offscreen.hpp` + `render/offscreen.hpp` API: `Result<Image> renderOffscreen(uint32_t w, uint32_t h, span<View> views, SceneStore& store)` (creates hidden context, owns `GlfwRuntime` ref, creates `View` FBOs, calls `T3:renderViews` + `ViewSynchronizer`+`ViewCompositor` without a `Window`, reads back via `REContext`). No `core/window.hpp` include in this path. `Window` remains for interactive samples; offscreen path is window-free. Depends on `T3:frame_loop` — fresh session for `T4` has proven `renderViews` to reuse.

**T** — gate: `renderOffscreen(640,480, {view{bunny}}, store)` center pixel within 1/255 of `Window`-path `View::render` oracle for same scene (offscreen vs window parity, `N>=3`); `grep -R "window\.hpp" render/offscreen.* ==0`; **MPR `FR-app.2` offscreen parity:** same `1280×960` / `640×480` viewport dims exact + axis probe via offscreen path (`N>=3`).

**G** — suite green (`N>=3`), audit green, no raw `glReadPixels` outside `core/re_context.cpp` (`grep -R glReadPixels -- core/ ==1, -- render/ ==0, -- utils/ ==0`).

## T5: Collapse mesh-backed object types — `MeshObject` + `GeometryKind` (P0)

**D** — 11 byte-identical headers (`scene/objects/cube_object.hpp` vs `sphere_object.hpp` diff 8 lines; pattern `AssetRef<Mesh> mesh; mat4 transform; MeshMaterialDesc presentation;` `scene/objects/*.hpp:36-40`) collapse to one `MeshObject` carrying `GeometryKind {Mesh, Cube, Sphere, Cylinder, Torus, Cone, Arrow, Grid, Axes, Capsule, PointCloud, Teapot}` or `ProceduralMesh` factory, plus the core six technique kinds (`Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour`). Keep `SceneKind` for technique dispatch only (6 values). `SceneFactory` + `REGISTER_SCENE_OBJECT` stays for truly new techniques (e.g., `StreamlineObject`), not for data-driven mesh variations. `SceneStore` 17 partitions → 6 partitions (next task `T6` tightens further to single-map — see T6). Migrate `broker/*ObjectMapper` for merged kinds to one mapper. **Sizing note (spec-review #12):** `T5` reduces 17→6 partitions but does not yet single-map; `T6` completes single-map — the intermediate 6-partition state is gated (see T5/G) so no churn vacates T5's gate. Sequential rewrite is intentional SRP: `T5` owns `GeometryKind` collapse, `T6` owns `store` template.

**T** — gate: adding `Sphere` no longer needs a new header — `MeshObject{ .geometryKind=Sphere }` via single `MeshObjectMapper` renders within 1/255 of old `SphereObject` path (pixel parity, `N>=3`); `grep -c "class SphereObject" scene/` == 0 after collapse (analytic count 0, not `>0`); `Broker::registeredTypes()` still contains 6 technique kinds.

**G** — suite green (`N>=3` for parity), audit green.

## T6: Store consolidation — single-map + `kindIndex_` (P0, depends on T5)

**D** — Replace remaining 6 hand-written `unordered_map<uint64_t, unique_ptr<ISceneObject>> meshObjects_ …` (`scene/store.hpp:331-351` after `T5`'s 17→6) + remaining `add*/remove*/get*` families with one `unordered_map<Id, unique_ptr<ISceneObject>> objects_` + `unordered_map<SceneKind, unordered_set<Id>> kindIndex_` (already existing `scene/store.hpp:357` secondary index). Provide templated `addObject<T>(T) -> Id`, `get<T>(Id)`, `remove(Id)` plus `objectsOfKind(SceneKind)` `O(kind)` via index (already `scene/store.hpp:126`). Remove 12 `*Count()` hand copies (`scene/store.hpp:186-202`) — keep `count(SceneKind)` + `totalObjectCount()`. One SRP template, not hand-copied families. **Builds on `T5`'s 6-partition gate** — `T5` gate's `Broker::registeredTypes()==6` remains true; `T6` gate checks single-map (`grep -c "meshObjects_\|sphereObjects_" ==0`).

**T** — gate: `Sphere` via `objectsOfKind(Mesh)` vs old partition parity — `count(Mesh)` unchanged; `grep -c "meshObjects_\|sphereObjects_" scene/store.hpp ==0` (analytic 0); `FR-data.*` store add/remove generation bump still analytic within 1e-6; suite green.

**G** — suite green, audit green; `grep -c "meshObjects_" scene/store.hpp ==0` still holds after `T7` (store single-map preserved — `T7` additive `loadMesh` only).

## T7: Loader facade + Scene/View builders — kill sample boilerplate (P1, 25–35 line ceremony, depends on T5+T6)

**D** — `scene/store.hpp`: `Result<ObjectId> loadMesh(path)` / `loadVolume(path)` that does `io::load*` + `register*Asset` + `add*Object` atomically (today `load → shared_ptr → MeshObject → add` 4 steps, 5/6 samples duplicate). Add `scene/builders.hpp`: `SceneViewBuilder{ ViewId, Rect }.withCamera(cam).withItems(ids).withClear(color).build() -> View` and `Objects::mesh(asset, mat4, mat) -> MeshObject` helpers; `PerspectiveFraming` (`app/sample_harness.hpp:83`) removed in favor of `Camera::perspectiveFromFraming(framing, aspect)` on `scene/camera.hpp`. `fitPerspectiveViewToPixels` (`app/sample_harness.cpp:169`) becomes `builder.applyLiveDims(w,h)` one call. **Depends on `T5` (`MeshObject{GeometryKind}`) + `T6` (single-map store) — builder targets `objects_` single-map API, not the deleted 17-partition API.**

**T** — gate: `store.loadMesh("data/meshes/bunny.obj")` returns `ObjectId` whose `View` center pixel within 1/255 of manual 4-step path (`N>=3`); sample `mesh_sample.cpp` boilerplate lines `applyLiveDims` duplicate count ≤1 site (`grep -c "applyLiveDims" app/*.cpp ==1` helper, not 6); `grep -c "PerspectiveFraming" app/` == 0 after removal; `grep -c "meshObjects_" scene/store.hpp ==0` still holds (single-map not reverted).

**G** — suite green (`N>=3` for loader parity), audit green; additive `loadMesh`/`loadVolume` + `builders.hpp` only, single-map preserved.

## T8: Simplified ordering — 8 layers + per-view override, technique priority orthogonal (P1, supersedes draft T1 64+deferred override, depends on T5+T6) — **split per Q1 per Sr. Architect: T8a ordering (this task) + T9 depth (next task, DepthConfig)**

**D** — Replace draft `T1` 64-layer `Layer : uint8_t {L0…L63, Count=64}` + `LayerMask uint64_t` + deferred `layerOverrides` (`TASKS.md:98` "duplicate entry" workaround) with **8 layers** `enum class Layer : uint8_t { Background=0, Volume=1, VolumeSlice=2, Plane=3, Mesh=4, MeshSlice=5, Contour=6, OverlayTop=7, Count=8 }` and `using LayerMask = uint32_t` (`1u<<layer`, future-proof, `0xFFu`). Every `SceneObject` (`MeshObject` etc. via `ObjectBase`) carries `Layer layer{Mesh}` + `setLayer()` bumping `generation`/`layerGen` (`FieldId::Layer` + `CompositeKey` includes `layer/mask`). `scene::View` carries `LayerMask layerMask{0xFFu}` + `setLayerMask()` + **`unordered_map<ObjectId, Layer> layerOverrides`** from day one (per-view override, `O(1)` lookup). `ViewSynchronizer` groups by `(layer, techniquePriority)` ascending — **Layer is visual stacking, TechniquePriority `Volume(0)…Contour(5)` is a separate orthogonal sort key** (G3 amendment): default is `Volume(0)→VolumeSlice(1)→Plane(2)→Mesh(3)→MeshSlice(4)→Contour(5)` but a per-view override can place `Contour` under `Mesh` without touching `Mesh.layer`. Per-view `layerMask & (1u<<layer)` culls without removing objects. **Depth is NOT in this task — see T9 `DepthConfig` (Sr. Architect: View owns DepthConfig value object, not raw bool on View, Renderer never allocates FBO).** Default stays color-only for deterministic llvmpipe gates (see T9).

**FR:** none new — deterministic layer order replaces `itemIds` insertion order; `MPR VolumeSlice L1 → Contour L6` preserved.

**T** — gate: two objects on same layer but different techniques render in priority order independent of `itemIds` swap (swap `itemIds` → same image within 1/255, `N>=3`); mask hides a layer (`layerMask &= ~(1u<<OverlayTop)` → overlay disappears, volume within 1/255); per-view override `layerOverrides[id]=Background` moves that object to background layer regardless of its global `layer` (override probe within 1/255); orthogonality probe: same `Layer=Mesh` for `VolumeSlice`+`Contour` still orders `VolumeSlice` before `Contour` by technique priority; **SliceRenderer `ε=1e-4` preserved:** cross-section vertices lie on plane within `ε=1e-4` (distance `|n·p+d| ≤1e-4`, `N>=3` analytic `PlaneDesc` via `SliceRenderer` geometry shader, same as `FR-render.4`); **MPR `FR-app.3` preserved:** contour `90% within 2 px` Euclidean vs analytic box+plane (`≥90%` pixels within 2 px of closed-form `plane∩mesh` curve, `N>=3`); `grep -c "enum class Layer" scene/` == 1 && `grep -c "layerOverrides" scene/view.hpp ==1` && `grep -c "LayerMask" scene/layer.hpp ==1` && `grep -c "0xFFu" scene/view.hpp ==1` (`uint32_t` + `1u<<` pinned).

**G** — suite green (`N>=3` for layer priority/mask/override/ε/contour), audit green.

### T8b: Depth opt-in — `View::DepthConfig` + `ViewTarget DepthMode` (P1, depends on T8, Sr. Architect: View owns DepthConfig value object, not Renderer)

**D** — **DepthConfig value object (SRP via composition, OCP for future `func/writeMask/clearDepth/stencil`):** `scene/depth_config.hpp` `struct DepthConfig{bool enabled{false}; float clearDepth{1.0f};}` (or inline in `scene/view.hpp`). `scene::View` holds `DepthConfig depthConfig` (default `enabled=false` — color-only deterministic) + `setDepthConfig(DepthConfig)` bumping `depthGen` (`FieldId::Depth`). `ViewTarget` `DepthMode::Enabled` (`GL_DEPTH_COMPONENT24` texture `core::Texture2D::uploadDepth` @ `GL_DEPTH_ATTACHMENT`) — creation asserts `glCheckFramebufferStatus==COMPLETE` **with** depth (fail-loud). `resize()` preserves `DepthMode`. `View::ensureTarget()` recreates only depth texture when `depthConfig.enabled` flips (View identity stable). Pass prologue `REContext::beginPass(depthConfig)` does `if(enabled){enableDepthTest; clearDepth 1.0} else disableDepthTest` once per `View::render` before `drawLayer` loop. `Engine` facade (`T1`) amends `createView/setView` to set `DepthConfig{true}` for mesh-containing views (documented divergence: low-level stays `false`, Engine defaults `true` for viz correctness, audit `engine_depth_default` `require_grep setDepthConfig(true)` in `include/render_engine/engine.hpp`). **Why View not Renderer:** Renderer is stateless `drawLayer` — has no `ViewTarget` size, would break `IRenderable` type-erasure + `ViewTarget` SRP; depth over mixed `VolumeSlice+MeshSlice` vs `Volume+Mesh` + OIT (`T19` opaque depth + transparent depth-off over same target) only View knows. `OIT` capture/composite `disableDepthTest` explicit on same `REContext` instance.

**T** — gate: depth-enabled `ViewTarget` `isComplete()==true` with `GL_DEPTH_ATTACHMENT` bound; flag flip `setDepthConfig({true})` → next `ensureTarget()` owns depth, `resize()` preserves; **near-wins 1/255:** two full-screen opaque quads anti-painter order (near FIRST, far LAST) at z=0 vs z=-1, orthographic `[-1,1]^2` 64×64, `depthConfig.enabled==true` → overlap pixel `{0,128,0}` (near wins) vs color-only `{128,0,0}` (far wins) — proves occlusion semantics within 1/255 (`docs/render.md` Depth gate); `grep -c "DepthConfig" scene/` ==1 && `grep -c "setDepthConfig(true)" include/render_engine/engine.hpp ==1` && `grep -c "DepthMode" render/` >=1.

**G** — suite green (`N>=3` depth gate, `GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6`), audit green, `docs/engine.md` depth default note + `tools/audit.rules` `engine_depth_default`.

## T9: Camera controller extraction — `scene/CameraController` pure math (P1, supersedes draft T4 `app/CameraController`)

**D** — Extract pure math `scene/camera_controller.hpp` `CameraController` (`onMouseDrag(dx,dy, button, modifiers) -> CameraDelta`, `onScroll(delta)`, `onKey`) with no `glfw*`/`ImGui` includes; `app/glfw_camera_interactor.hpp` adapter polls `glfwGetMouseButton/CursorPos/Scroll` each frame before `renderFrame` and forwards to controller when `!ImGui::GetIO().WantCaptureMouse` (`TASKS.md:126` `WantCaptureMouse` guard), then calls `View::mutateCamera([&](Camera& c){ c.rotate(...); })` so `viewGen` bumps and broker re-translates only dirty fields. `CameraBindings{ rotateButton=LMB, panButton=RMB, zoomButton=MMB/wheel, modifiers, rotateSpeed, panSpeed, zoomSpeed }` plain struct stays. Wired in `mesh/slice/volume/oit/mpr-3D`; plane + MPR 2D orthographic skip.

**T** — gate: `scene::CameraController` unit test: `drag(10px)` yields analytic `orbit(10px)` `viewMatrix` within 1e-6; `WantCaptureMouse=true` guard: same drag with `WantCaptureMouse=true` leaves `viewMatrix` unchanged (delta 0 ±1e-6) vs `false` yields analytic orbit; `grep -c "glfw" scene/camera_controller.hpp ==0` (no GLFW in `scene/`); bounded run with no input still green (`N>=3` via offscreen fixture).

**G** — suite green (`N>=3` for controller parity), audit green, `scene/` disposition still `render/`-free.

## T10: Result ergonomics — `map/andThen` + `RE_EXPECT` (P1)

**D** — `data/result.hpp:83` `Result<T,E>` today has UB on failed deref and verbose `if(failed()) return` ladders (`mpr_sample.cpp:269` triple). Add monadic `map(F)`, `andThen(F)`, `orElse`, `valueOr(default)` plus `RE_EXPECT(expr)` / `RE_TRY` macro that early-returns `Result` error with `__FILE__:__LINE__` provenance. Keep `[[nodiscard]]` and debug-trap on failed `operator*` (already T22). Document that `ErrorDomain` disambiguates numeric codes (already `data/result.hpp` domain tag).

**T** — gate: `loadMesh("data/fixtures/malformed.obj").andThen([](Mesh m){ return store.addMeshObject(m); }).map([](Id id){ return View{ids:{id}}; })` chain compiles and on malformed `malformed.obj` returns `Result.failed() && err.domain==ErrorDomain::Io && err.code==1 /*FileOpen==1 per io/mesh_loader.cpp:12*/` — code preserved within domain — plus `grep -c "andThen" data/result.hpp ==1 && grep -c "orElse" data/result.hpp ==1 && grep -c "map" data/result.hpp >=2` (analytic counts `==1`/`==1`/`>=2`, not `>0`; `BudgetExceeded` `code==7` distinct domain is `T11` `core::Caps` probe-fail, not `T10`).

**G** — suite green, audit green, no exception path introduced.

## T11: Volume large-data — tiling/bricking + `core::Caps` (P1, depends on `core::Caps` wrapper, Sr. Architect: No cap streaming via `core::Caps`)

**D** — **Volume bricking / cap lift (No cap streaming per Q4):** `io/volume/nrrd_volume_loader.cpp` `≤128³` gate is test convenience, not product limit; replace hard window with **No cap streaming** — any dims via `core::Caps` tiled/downsampled streaming (see `docs/spec/assets.md:14` `FR-io.2` `goals.md:50` `nfr.md:26`): `render/volume_renderer.cpp` checks `maxTexture3DSize` via **`core::Caps` `core/caps.hpp` `Caps{uint32_t maxTexture3DSize; bool ssboAtomics;}` cached `core::caps()`** ( `core/caps.cpp` calls `glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE)` / `glGetString` once until RHI lands, `TODO(RHI)` → `IRHIContext::capabilities()` after T10 `core/rhi/` per `docs/spec/nfr.md:25` `modules.md:34` ) and either downsamples or tiles `Texture3D` (tiled `1/255` within reference, not `BudgetExceeded` for `>128³` alone — `BudgetExceeded` only when `core::Caps` probe fails). **Depends on: `core::Caps` wrapper (no `IRHIContext` yet, `IRHIContext` is `(stretch)` T10; `T11` uses `TODO(RHI)` adapter).** **Sizing:** `T11` single phase ~60 lines loader + caps plumbing, single session.

**T** — gate: synthetic NRRD `256³` via `core::Caps` tiled load within 1/255 of reference `256³` tiled (analytic, not OOM) + valid `128³` still loads byte-identical `1/255`; `grep -c "core::caps\|Caps" render/volume_renderer.cpp >=1` && `grep -c "BudgetExceeded" io/volume/nrrd_volume_loader.cpp ==1` (only probe-fail path, not `>128³`).

**G** — suite green (`N>=3` tiled `256³` 1/255 + `128³` byte-identical), audit green, `maxTexture3DSize` via `core::Caps` (`grep Caps`).

### T11b: OIT weighted-blended fallback — `core::Caps ssboAtomics` (P1, depends on T11 `core::Caps`, Sr. Architect: `render/` never raw gl*)

**D** — **OIT fallback (No cap streaming, same Caps):** `render/linked_list_oit.cpp` `w*h*16*32` 152 MB @640×480 / 1 GB @1080p (`docs/spec/nfr.md:28`) abort is not visualization-grade; when caps probe reports `!ssboAtomics` (`core::Caps` `ssboAtomics` via `glGetString`/`GL_ATOMIC_COUNTER_BUFFER` until RHI, `TODO(RHI)` → `IRHIContext::capabilities().ssboAtomics`) or `w*h*16*32` over budget, fallback to weighted-blended OIT (`SPEC Q32` `!ssboAtomics → weighted-blended`, `docs/spec/open_questions.md:70`) and composite with weight. Typed error only when both linked-list and weighted fail. **Depends on `T11` `core::Caps` (reuses `maxTexture3DSize/ssboAtomics/152 MB` plumbing, no duplicate caps).** Single phase ~40 lines OIT fallback + caps reuse, single session.

**T** — gate: OIT over-budget `1920×1080` with `LinkedListOIT` forced over-budget → weighted-blended output center pixel within 1/255 of reference weighted blend (analytic, not opaque-only) `N>=3`; `grep -c "weighted" render/linked_list_oit.cpp ==1` (analytic `==1`, not `>=1`) && `grep -c "core::caps\|ssboAtomics" render/linked_list_oit.cpp >=1`.

**G** — suite green (`N>=3` weighted-blended 1/255), audit green, `ssboAtomics` via `core::Caps`.

## T12: FPS standalone + draw-header cleanup (P2 batch, depends on T2)

**D** — **FPS:** Move `app/FpsCounter` (`TASKS.md:118` draft `app/FpsCounter` owned by `SampleHarness`) to `utils/fps_counter.hpp` `utils::FpsCounter` standalone (`std::chrono::steady_clock`, 0.5s window, `tick()`, `fps()`, `ms()`); `app/sample_harness` queries it. **Draw header:** delete legacy `core/draw.hpp` vs `core/re_context.hpp` duality (`core/draw.hpp:1` façade delegating to `REContext::current()` vs `core/re_context.hpp:182 beginPass` single ledger) — keep one `core/re_context.hpp` header, `core/draw.hpp` becomes alias include or deleted. `REContext` single-writer discipline already T4, header duality remains. **Depends on `T2` (PRIVATE glad firewall) — preserves `grep PRIVATE glad` after header sweep.**

**T** — gate: `FpsCounter` unit `tick(16.6ms)` sliding average `fps==60±1e-3` (analytic `fps==1/delta` within `1e-3`, `delta=16.6ms → fps==60.24`, 0.5s window `N=30` samples `avg==60.24±1e-3`); headless `RE_SAMPLE_MAX_FRAMES=20` smoke still green; `grep -R "#include.*glad" core/*.hpp ==0` still and `grep -c "draw\.hpp" core/*.hpp ==0` or alias-only (no second ledger); **FR-core.2 preservation:** `ShaderProgram` malformed source with `glibberish` on line 7 via `loadSourceFile` → `Result.failed() && err.domain==ErrorDomain::Shader && msg.contains("ERROR: 0:7") && msg.contains("glibberish")` (golden substring `ERROR: 0:7` + `glibberish`, no crash, same as `tests/t3_core_gl_test.cpp` inline gate — `FR-core.2` re-verified, not `R3` alone).

**G** — suite green, audit green; `grep "PRIVATE" core/CMakeLists.txt | grep glad ==1` firewall not regressed (T2 `PUBLIC→PRIVATE` preserved) && `grep -c "add_compile_options.*-fsanitize" ==0` still.

## T13: Minimal example + versioned serialize docs (P2 batch, depends on T1+T2)

**D** — `examples/minimal.cpp` (20 lines) using `viz::Engine` facade (`T1`): `Engine e; auto id=e.addMesh("data/meshes/bunny.obj"); e.setView({{0,0,800,600}, camera, {id}}); e.render(windowFb);` — the first file a visualization project copies. `README.md` "Minimal example" section + `docs/engine.md` full facade docs. `SceneStore::serialize()` stabilization: `MaterialDesc`/`LightDesc` JSON via `nlohmann/json` (`CMakeLists.txt:117`) already, but `View` persistence via `CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash}` (`docs/spec/persistence.md:36`) not serialized — document versioned `SceneStore::serialize()` JSON with `Version` migrations and `View` wire format. **Depends on `T1` (facade header) + `T2` (`cmake --install` + `RenderEngineConfig.cmake` for `find_package` probe).**

**T** — gate: `examples/minimal.cpp` builds via installed config (`cmake -S examples -B /tmp/min && cmake --build /tmp/min` green); `grep -c "Engine" examples/minimal.cpp ==1` && `wc -l examples/minimal.cpp ==22` (committed exact `22`, not `<=30` cap) && **`examples/minimal` smoke via `renderOffscreen` within 1/255 of `AppContext` oracle (`N>=3`, analytic)**.

**G** — suite green, audit green, `examples/minimal` built by T2 installed config.

## T14: Drift guard — sample line count + `scene/objects` duplicate ratio (P2, audit, depends on T5+T7)

**D** — `tools/audit.rules`: `no_sample_bloat` (`forbid_grep` on `app/*_sample.cpp` line count >80 via `wc` check in `tools/audit.sh` or `max_lines` audit) and `no_object_duplicate` (`forbid_grep` on `scene/objects/*.hpp` duplicate ratio >10% via `tools/audit.sh` `diff` check). Keeps `app/mesh_sample.cpp` 160→80 and 11 identical object headers from re-introducing debt after T5/T7. `app/mpr_slice.hpp` 230-line mix (layout+geometry+oracle) split guard via `max_lines` per file (`mpr_slice` further split via T7/T8 builders — if `mpr_sample.cpp` cannot reach 80 alone, waist gauge is `mpr_slice.hpp` ≤100). **Depends on `T5` (collapse to `MeshObject`) + `T7` (builders trim `mesh_sample.cpp` to ≤80).**

**T** — gate: `app/mesh_sample.cpp` via facade `==42` lines (committed exact `42`, not `≤80` cap, `+ 1/255` layer ordering already in `T8`) && `grep -c "class.*Object.*ObjectBase" scene/objects/*.hpp ==6` (exact `6` technique kinds, not `≤6`) && `app/mpr_slice.hpp` `==98` (exact, not `≤100`); audit green — size caps secondary, primary `1/255` in `T8` ordering/mask.

**G** — suite green, audit green (new rules enforced).

## T15: Minimal light API for visualization consumers (P2, gap G1 — reprioritized from P1 to P2 per spec-review #3 to keep priority monotonic P0→P1→P2; depends on T1+T8, independent of T12-T14)

**D** — Even though `SPEC §1` keeps Phong-only + fixed headlight as non-goal (PBR/`Slice`/`Contour`+`ILight` deferred), visualization reuse needs a *minimal per-View light surface* without promoting the full hierarchy. Publish `scene/light.hpp` `Light` (already `View::lights` `vector<Light>` `scene/view.hpp:61`) through `Engine`: `Engine::setLights(ViewId, vector<Light>)` and `ViewBuilder::withLights(lights)`; document that empty `lights` = existing fixed headlight/unlit 2D preservation (FR-render gates stay byte-identical), non-empty → `broker/light_mapper.hpp` → `render/light.hpp` `ReLight` upload once per `View` before `drawLayer` loop (already `ViewSynchronizer` path). No new `render/light/` hierarchy this iteration — one struct keeps `View::lights` trivial. Keep `render::IMaterial→PhongMaterial` single path; this task only wires the value type end-to-end for the 80% viz case.

**T** — gate: `Engine e; e.setLights(viewId, {Light{Directional, dir{-1,-1,-1}}})` renders within 1/255 of direct `View::setLights` + `ViewSynchronizer` path (`N>=3` via offscreen, analytic non-empty vs empty probe: `empty` preserves headlight pixel, one `Directional` shifts `diffuse` ≥5/255 deterministically); `grep -c "setLights" include/render_engine/engine.hpp ==1` && `grep -c "class Light" scene/light.hpp ==1` (analytic `==1`, not `>=1`/`>0`).

**G** — suite green (`N>=3` light parity), audit green, `docs/engine.md` lights section added; `include/render_engine/engine.hpp` incremental — `T1` facade API (`class Engine`==1, `addMesh==1`, `setView`/`render`) still builds (`grep -c "class Engine" include/render_engine/engine.hpp ==1 && grep -c "addMesh" include/render_engine/engine.hpp ==1`).

## T16: Mapper cache consolidation — `CachedMapperBase` (P2, gap G2 — reprioritized from P1 to P2 per spec-review #3 to keep priority monotonic P0→P1→P2; depends on T5 broker inventory, independent of T12-T14)

**D** — Every `*ObjectMapper` (`broker/mesh_object_mapper.hpp:82`, `teapot_object_mapper.hpp:52`, `volume_object_mapper.hpp`, …) repeats `struct Entry{uint64_t generation; ReType instance;}; unordered_map<uint64_t,Entry> cache_;` + `mapCached` generation short-circuit + `invalidate(id)`. Extract `broker/cached_mapper_base.hpp` `template<AppT,ReT> class CachedMapperBase : public ICachedMapper<AppT,ReT>` that owns `unordered_map<uint64_t,Entry> cache_` + `mapCached`/`invalidate` + `clear()` and requires derived only to implement `map()`. Migrate all cached object mappers to inherit it — one definition, no per-file hand copy. Keeps `PlaneMapper`/`PlaneObjectMapper` stateless `IMapper` (ISP) untouched.

**T** — gate: `grep -c "unordered_map.*Entry.*cache_" broker/*_object_mapper.hpp ==0` after consolidation (analytic 0, cache lives only in base) && `grep -c "class CachedMapperBase" broker/cached_mapper_base.hpp ==1`; cached `MeshObject` generation hit still short-circuits (spy `map` call count 2→1) and `invalidate(id)` evicts exactly that id (per-id probe, `N>=3`).

**G** — suite green, audit green.

## T17: Depth default & naming sweep + doc polish (P2, gaps G4/G6, depends on T8b+T14)

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



---
## V6: Consolidated engine iteration + hotfixes (archived 2026-08-30 — 25 tasks, T1..T20 incl. splits + stretch + hotfixes)

V6 was the consolidated sequential loop for the V6 roadmap (TASKS.md T1..T20, SPEC §10–§12, §3, §11.6, §13, review follow-ups + hotfixes T19/T20). All 25 gates are green; `tools/logs/` artifacts have been purged for the next iteration. The backlog below is copied verbatim from `TASKS.md` at archive time (D/T/G + doc-map row) — no re-interpretation.


## Consolidated Active Backlog — Dependency-Ordered (21 tasks + 2 stretch = 23 sequential, after #2-#5 splits + T11 split iteration 1 #2)

> **Consolidated 2026-08-28 (Iteration 1 spec-review, rev 2026-08-29 findings #1-#16, count fixed spec-review gate 2026-08-29, iteration 1 re-review 2026-08-29 #2 split T11):** Prior `V6 (T6.1..6.4)` + `V7 (T7.1..7.9)` + `V8 (T8.1..8.6)` presented as three independent backlogs with overlapping scope and out-of-order dependencies. This section is the single binding sequential backlog, dependency-ordered per `SPEC §13.7` (`scene/ → skeleton → broker → View → persistence → asset → RE-minimal`) — see ordering waiver below. `V6 T6.1/T6.2` retired — now `T5/T6` single `Version 1->2`. `T17 RHI` + `T18 evidence` are `(stretch)` per `SPEC §3/§6` — they execute last and do not gate hardening. Splits: `T3→T3a/T3b` (#2), `T8→T8a/T8b` (#3), `T14→T14a/T14b` (#4), `T15→T15a/T15b` + `T15a` lands `audit.rules` floors early (#5), `T11→T11a/T11b` (iteration 1 #2 sizing fix, each <150 lines, one domain). `T11a/T11b`/`T13` marked parallel-eligible after `T1`/`T6` (#6). Historical `T6.x`/`T7.x`/`T8.x` aliases archived in `COMPLETED_TASKS.md` (spec-review #11); active headings use clean `T1..T16` only + `T11a/b` split — traceability via git history. **Count:** `T1,T2,T3a,T3b,T4,T5,T6,T7,T8a,T8b,T9,T10,T11a,T11b,T12,T13,T14a,T14b,T15a,T15b,T16` = 21 active + `T17/T18` stretch = 23 total (header and DoD now agree).<br>**Hotfix 2026-08-30:** user-reported `volume and slice renderer samples are not working` + `all samples have memory leaks` adds `T19` (volume/slice bounded builder state loss) + `T20` (LSAN suppressions) after V6 — see `## T19`/`## T20` and `Hotfix DoD` below; they execute after `T16` in listed order and do not retroactively change the V6 21+2 count (V6 DoD still 21 active, hotfix adds 2 required, total required 23 active after hotfix, 25 incl. stretch).<br>**Ordering waiver (spec-review #5):** `T7` depends only on `T1` (FNV skeleton) and `T12` upgrades to SHA-256 after `T7` (iteration 1 #3 — no forward dependency; `T7` gate is FNV, `T12` gate is SHA-256, per reviewer #3 option B); `T11a/T11b`/`T13` are independent except `T1`/`T11` budgeting, so topologically they are parallel-eligible after `T1`/`T6` — the sequential gate still runs in listed order for review simplicity (runner is sequential, no parallel execution). No dependency violation: every `Depends on` is satisfied before its `T` runs; the slack is documented here.

## T1: Store IO depollution — `loadMesh/loadVolume` out of `Store.hpp`

**D** — Delete `scene/store.hpp:218` `loadMesh/loadVolume` (2 decls) + `scene/store.cpp:1-10` `#include "io/mesh/obj_mesh_loader.hpp"`/`io/volume/nrrd_volume_loader.hpp` (header stays lean) + `scene/CMakeLists.txt:13` `PRIVATE re_io` edge. Keep `nlohmann/json.hpp` include in `scene/store.cpp` only (needed for `SceneStore::serialize()` `docs/spec/persistence.md:10.8` JSON wire — header stays lean via forward decl, `store.cpp` retains `#include <nlohmann/json.hpp>` for `serialize()/deserialize()`). Extract to `utils/asset_utils.hpp` `Result<SharedMesh> loadMeshAsset(path)` + `Result<SharedVolume> loadVolumeAsset(path)` (IO-only, header-only, `utils/` owns filesystem). Keep 4-step `load→shared_ptr→registerMeshAsset→addMeshObject` ceremony in `utils/`; `scene/store.hpp:215` doc retains `4 steps →1 call` note but points to `utils/`. `scene/builders.hpp` `Objects::mesh` stays value builder, not IO. `SceneStore` stays pure value lib `data+volume+glm` per `docs/spec/modules.md:21`. Single `Version` migration for layers lives in `T5/T6`, not here.

**T** — suite green: `grep -c "loadMesh\|loadVolume" scene/store.hpp ==0 && grep -c "obj_mesh_loader" scene/ ==0 && grep -c "nrrd_volume_loader" scene/ ==0 && grep -c "re_io" scene/CMakeLists.txt ==0`; `SceneStore` pixel parity 1/255 on existing mesh/volume gates via `utils::loadMeshAsset`; `R15` launch prerequisite loud fail (spec-review #3): gate asserts `test "$AUDIT_SOURCE_DIRS" = "io data volume scene core broker render app utils test_utils tests" && test -n "$LOOP_BUILD_TEST_CMD"` else `echo "source tools/env.sh required" >&2 && exit 1`.

**G** — audit green, `disposition_scene` `scene|#include.*render/` still green, `scene` no `io` dep, no `json` include; `R15` env vars asserted (`AUDIT_SOURCE_DIRS` exact + `LOOP_BUILD_TEST_CMD` non-empty).

## T2: Engine IO depollution — `Engine::addMesh/addVolume` out of facade

**D** — Depends on `T1`. Remove `include/render_engine/engine.hpp:32-33` `#include "io/mesh/obj_mesh_loader.hpp"`/`io/volume/nrrd_volume_loader.hpp` + inline `87-165` `addMesh(path,transform,mat)`/`addVolume(path,transform,tf)` IO path (3+4 overloads). Keep only store-typed API `addMesh(AssetRef<Mesh>,transform,mat)→ObjectId` + `addVolume(AssetRef<Volume>,…)` delegating to `ctx_.store().add*Object`. Call sites `app/mesh_sample.cpp:32` + samples migrate to `utils::loadMeshAsset`→`registerMeshAsset`→`addMesh`. Delete `33` `io` transitive include from `re_engine` INTERFACE `CMakeLists.txt:210`.

**T** — suite green: `grep -c "loadObjMesh\|loadNrrdVolume" include/ ==0 && grep -c "obj_mesh_loader" include/ ==0 && grep -c "io/mesh" include/render_engine/ ==0`; `Engine` facade tests via `utils::loadMeshAsset` still 1/255.

**G** — audit green, `re_engine` no `re_io` linkage, `acl_app_render` still green.

## T3a: Collapse `render()` dual API — Mesh/Slice/Plane (Ex1a)

**D** — Depends on `T1`+`T2`. Delete `render()` from 3 non-volume renderers: `render/mesh_renderer.hpp:96` `MeshRenderer::render`, `render/slice_renderer.hpp:96` `SliceRenderer::render`, `render/plane_renderer.hpp:167` `PlaneRenderer::render` (each `grep -c "data::Result<void> render(" ==0` for those 3). Keep `drawInstances`/`clipInstances` as `drawLayer` impl; `render/i_renderable.hpp:45` retains. Volume/Contour `render()` stays for `T3b`. Direct tests for those 3 port via `render::View` + `REContext::current().beginPass` + `View::addItem`.

**T** — suite green N>=3 `llvmpipe`: `grep -c "data::Result<void> render(" render/mesh_renderer.hpp` ==0 `&&` `slice_renderer.hpp`==0 `&&` `plane_renderer.hpp`==0; `mesh/plane/slice` direct-oracle parity via `View` path 1/255 `FR-render.1/4/5`; `tools/generate_font_atlas.sh` deterministic `sha256sum data/fixtures/font_atlas_golden.rgba` replaces `TBD_T3a` placeholder in `docs/spec/assets.md:57` with hex64 `sha256sum 74bc1f394723a260d6a8501fc2c499bc515588a915205c8ef01cfb5349d72f22` interim pre-pin at iteration 5 #3 (dummy `64×64` `16384 B` `sha256sum 74bc1f394723a260d6a8501fc2c499bc515588a915205c8ef01cfb5349d72f22` interim; real `RE_SAMPLE_MAX_FRAMES=1` FBO capture overwrites at `T3a` and re-pins, iteration 2 #5 — `TBD_T3a` interim, `T3a` must replace interim before `spec_review.pass` was considered reproducible, now interim pinned); no `>0`.

**G** — audit green, 3 `render()` removed, OIT compositor not yet touched.

## T3b: Collapse `render()` — Volume/VolumeSlice/Contour + single OIT (Ex1b)

**D** — Depends on `T3a`. Delete remaining `VolumeRenderer::render` `render/volume_renderer.hpp:148`, `VolumeSliceRenderer::render` `149`, `ContourRenderer::render` `100`. Delete `render/mesh_renderer.cpp:166-241` inline `anyTransparent ? begin/drawOpaque/drawTransparent/end` (now single `drawInstances` blend-off, OIT only via `broker/view_compositor.cpp:94` `captureTransparents` out-of-band). Remove `drawOpaque/drawTransparent` pair. Port volume/oit tests via `View` path; `Layer` param added in `T6`.

**T** — suite green N>=3 `llvmpipe`: `grep -c "data::Result<void> render(" render/*_renderer.hpp ==0` (all 6 now 0); `volume/volume_slice/oit` direct-oracle parity via `View` path 1/255 `FR-render.2/3/6`; `FR-vol.3` ray/AABB step positions assert `EXPECT_NEAR(step, analytic, 1e-6)` via `volume/` oracle (spec-review #2, active not `hist`); no `>0`.

**G** — audit green, no `render()` in `render/` (6/6), single OIT via compositor.

## T4: Scope depth per-View (remove blind global) — keep `DepthConfig` on `View`

**D** — Depends on `T3a`+`T3b` (iteration 5 #4 fix — after split `T3 → T3a/T3b` binding order is `T3a → T3b → T4`; bare `T3` is undefined per spec-review #11 forbids `V3.x` alias; active headings use `Tn` only). Align with `SPEC §3.1` + `docs/spec/guardrails.md:90` (depth opt-in per `View` via `DepthConfig`, not per-layer). Remove blind view-level duplicate: `scene/view.hpp:59-61` delete raw `bool depthTest` + `depthTestGen` duplicate, keep `DepthConfig depthConfig{false}` value object (`scene/depth_config.hpp`) + `setDepthConfig(DepthConfig)` bumping `depthConfigGen`/`generation` (renamed from `depthTestGen`, spec-review #10). `render/view.hpp:87-88` + `render/view_target.hpp:41` keep `DepthMode` per-`ViewTarget` (driven by `View::depthConfig()`), `core/re_context.cpp:166-185` `beginPass(depthConfig)` enables/clears depth once per `View::render` before `drawLayer` loop (OIT capture/composite explicitly `disableDepthTest`). Keep `include/render_engine/engine.hpp:354-355` `applyMeshDepthDefault{DepthConfig{true}}` single-site via `Engine::setView` (audit `engine_depth_default` `DepthConfig\{true` stays green); delete any second blind enable. Keep `scene/view.hpp:51-53` `depthTestGen` in `ViewCache` `broker/view_synchronizer.hpp:128` but rename to `depthConfigGen` for `T5` layer hash.

**T** — suite green: `grep -w "depthTest" scene/view.hpp ==0` (raw bool gone, word-boundary — spec-review #10) `&& grep -c "depthConfigGen" scene/view.hpp ==1` (renamed from `depthTestGen`, spec-review #10) `&& grep -c "depthTestGen" scene/ ==0` `&& grep -c "struct DepthConfig" scene/depth_config.hpp ==1` (analytic `==1` struct definition in `scene/depth_config.hpp`, iteration 1 #8 exact) `&& test $(grep -Rl "DepthConfig" scene/ | wc -l) ==2` (exact 2 files: `scene/depth_config.hpp` + `scene/view.hpp` via `grep -Rl`, iteration 1 #8 tightens loose `>=1` line-count to file-count `==2`) `&& grep -c "applyMeshDepthDefault" include/ ==1`; mixed `LAYER_0 VolumeSlice prio0 + Mesh prio0` depth-correct 1/255 (slice depth off via `DepthConfig{false}` gate, mesh depth on via `Engine` default true for that View, contour overlay on top), same heterogenous `VolumeSlice prio100` vs `Contour prio0` still before via techniqueOrder after `T6`; sizing waiver (spec-review #4): single session ~150-line diff across 4 modules, single focus depth per-View opt-in via DepthConfig, verified `wc -l` delta <200.

**G** — audit green, `engine_depth_default` still `require_grep DepthConfig\{true` in `include/`, no per-layer `DepthMode` map, no view-level raw `depthTest`; sizing per-file `wc -l` delta <150 per file, total <200 (iteration 6 #3 — `T4` `wc -l delta <200` waiver is sizing proof, not evidence, per-file <150).

## T5: Dumb layers `LAYER_0..7` + per-object `priority` — scene side

**D** — Depends on `T4`. `scene/layer.hpp` `enum Layer:uint16_t{LAYER_0=0..LAYER_7=7, COUNT=8}` no semantic names (`Volume`/`Mesh` etc.), no `LayerMask` type; lower numeric draws first, `64` is doc edit only (no `1u<<layer` UB, no array sized by `COUNT`). `scene/iscene_object.hpp` add `int32_t priority{0}` alongside `Layer layer` on `ObjectBase`/`ISceneObject` (`priority()`/`setPriority()` `++generation` like `setLayer`), new `FieldId::Priority=12` `scene/field_id.hpp`. All 6 concrete objects `scene/objects/*.hpp` `layer{LAYER_0}` single default + `priority{0}`. `scene/view.hpp` + `scene/view.cpp` delete `layerMask{0xFFu}` + `layerOverrides` map + 4 setters; keep `layerGen` only for debugging if needed else remove. `scene/store.hpp` serialize wire drops `LayerMask/LayerOverrides`, **single** `Version 1->2` migration (only here, not duplicated in `T6`).

**T** — suite green: `grep -c "LAYER_0" scene/layer.hpp ==1 && grep -c "LayerMask" scene/ ==0 && grep -c "layerOverrides" scene/ ==0 && grep -c "0xFFu" scene/view.hpp ==0 && grep -c "COUNT[[:space:]]*=[[:space:]]*8" scene/layer.hpp ==1` (exact `COUNT=8`, iteration 15 #2 fix — `DoD: layer_count COUNT=8` exact despite `audit.rules:92` commented until `T5`); `SceneStore` pixel parity 1/255 on existing gates before broker reorder; `serialize` round-trip `Version==2` with old `Version 1` fixture migrates `LAYER_0` default.

**G** — audit green, no `LayerMask`, no semantic names, `priority` present.

## T6: Global `techniqueOrder` + scoped priority ordering (broker side)

**D** — Depends on `T5`. `broker/render_stack.hpp` add global `std::array<SceneKind,6> techniqueOrder{Volume,VolumeSlice,Plane,Mesh,MeshSlice,Contour}` explicit hardcoded renderer call order that governs cross-type order inside each `Layer` (BGFX `Sequential` / UE `AddPass` precedent). `broker/view_synchronizer.hpp` `ViewCache` drop `layerGen` tied to mask, keep `layerOrderHash`. `broker/view_synchronizer.cpp` delete `techniquePriorityFor()` + `mask cull 1u<<eff` + `itOv override` + `curGen ^= mask/overrides` `101-106` and `:293`. New order: `struct OrderEntry{oid,Layer,int orderIdx,int priority,size_t insertionIdx}` `orderIdx=indexIn(techniqueOrder,kind)` `priority=obj->priority()` `stable_sort by (uint16(layer) asc, orderIdx asc, priority asc, insertionIdx asc)` scoped priority inside same `layer+type` bucket so `VolumeSlice prio100` still before `Contour prio0` on same `LAYER_0`. `orderHash ^= layer+orderIdx+priority+oid`. `mapItemToLayer` unchanged. `render/i_renderable.hpp:45` add `Layer` param to `drawLayer` if needed for ordering debug (not for depth — depth stays per-View `DepthConfig` per `T4`/`SPEC §3.1`).

**T** — gate: new layer semantics preserved without mask/override: `same LAYER_0 different type via techniqueOrder 1/255 swap invariant` (`VolumeSlice before Contour`), `LAYER_0 vs LAYER_1 overlay on top 1/255`, `same LAYER_0 same type prio0 vs prio1 scoped priority 1/255` + `scoped cross-type VolumeSlice prio100 still before Contour prio0`, `stable insertion same layer+type+prio`; `serialize Version 2` idempotent on second run; `grep -c "1/255\|1e-6" tests/t6*.cpp ==1` hand-counted (iteration 14 #2 fix — per-task grep floor, was missing).

**G** — suite green N>=3 `llvmpipe`, audit green, sample smoke still (before harness fix).

## T7: Unify asset identity — delete triple store (`byObject_`/`legacyHandleCache`)

**D** — Depends on `T1` (canonical `hashStableBytes` source — FNV-1a 64 skeleton at `T7`, SHA-256 truncated 64 prod via `T12` after, iteration 1 #3 fix — ordering inversion clarified: `T7` gate is FNV skeleton, `T12` upgrades to SHA-256 prod; `T7` no longer claims SHA-256 prod alone). Single identity `data/content_hash.hpp:31` `hashStableBytes` (FNV-1a 64 skeleton at `T7`, SHA-256 prod via `T12` — `T12` lands `data/content_hash.hpp:48-152` SHA-256 canonical; `T7` uses skeleton, `T12` upgrades, per iteration 1 #3) — keep `scene/store.hpp:284-286` `AssetRegistry<T>` as canonical. Delete `broker/asset_store.hpp` mesh-only store + `render/asset_registry.hpp:590-597` `byObject_` dual-key shim + `render/volume_renderer.hpp:204` `legacyHandleCache` + `render/volume_slice_renderer.hpp:194` + `render/plane_renderer.hpp:211` pointer caches. Keep only `byHash_` content-hash dedup; `register*Asset` → `resolve*` via `AssetHandle{index,generation,hash}` (also `VolumeTextureHandle`/`ImageTextureHandle` unify to `AssetHandle`). Update `broker/*_object_mapper.cpp` to use `scene::SceneStore` registry only.

**T** — suite green: `grep -c "byObject_" render/ ==0 && grep -c "legacyHandleCache" render/ ==0 && grep -c "AssetStore" broker/ ==0`; dedup analytic (spec-review #2-#3 tightened): identical bytes share one `Texture3D` across 2 `VolumeRenderer` instances with `EXPECT_EQ(registry.slotCount(),1)` and `EXPECT_EQ(resolve(h1), resolve(h2))` (same `index+gen+hash`) plus `1/255` pixel parity — not bare `grep -c` threshold.

**G** — audit green, `asset_indirection` `data::Mesh::positions` still `render/re_scene` check passes, no pointer-key maps, no `shared()` global mutable (or guarded).

## T8a: Split `View` god → ViewState composition

**D** — Depends on `T4`+`T5`+`T6`. Per `docs/spec/modules.md:20` `View` is `View{rect,clearColor,Camera,plane,lights,itemIds}` + 6 per-field gens — keep binding. Extract non-core state to `scene/view_state.hpp` `ViewState{clearColor,DepthConfig}` composition if needed, delete `mutateCamera` lambda `98-106` → `setCamera(Camera)` bumping `cameraGen` via `SceneStore::bump(FieldId)`. `View` retains 6 fields. `wc -l scene/view.hpp <90` is secondary cap; primary is `grep -c "rectGen\|clearColorGen\|cameraGen\|planeGen\|lightsGen\|itemsGen" scene/view.hpp ==6` (`#8` fix).

**T** — suite green: `grep -c "rectGen\|clearColorGen\|cameraGen\|planeGen\|lightsGen\|itemsGen" scene/view.hpp ==6 && grep -c "mutateCamera" scene/ ==0`; `wc -l scene/view.hpp <90` secondary drift guard via `tools/audit.sh` (not analytic, iteration 1 #7 — primary is `grep -c ==6`); `View` 1/255 on existing gates.

**G** — audit green, `View` SRP, no `mutateCamera`, 6 gens.

## T8b: Split `ViewSynchronizer` god → Hasher/Orderer/Translator

**D** — Depends on `T8a`. Split `broker/view_synchronizer.cpp:86-340` `sync` 250-line god into `ViewHasher` (curGen `layerOrderHash+depthConfigGen`), `LayerOrderer` (`stable_sort OrderEntry`), `ItemTranslator` (`mapItemToLayer` via `Broker::getByKind`). Keep `ViewCompositor` OIT capture. `wc -l broker/view_synchronizer.cpp <220` is secondary drift guard via `tools/audit.sh` (not analytic, spec-review #8); primary is `grep -c "class ViewHasher\|class LayerOrderer" >=2`. `T9`/`T10` now depend on `T8a`+`T8b`.

**T** — suite green: `grep -c "class ViewHasher" broker/view_synchronizer.cpp ==1 && grep -c "class LayerOrderer" broker/view_synchronizer.cpp ==1` (primary exact `==1` each, total `==2`, iteration 6 #1 fix — `>=2` would pass 3+ gods) `&& wc -l broker/view_synchronizer.cpp <220` secondary drift guard via `T8b` gate `wc -l` (not `tools/audit.sh` built-in — `audit.sh` guards only `mesh_sample.cpp`/`mpr_slice.hpp`/`scene/objects`, iteration 16 #2 fix); `View` 1/255; `no_dump_sync` still green.

**G** — audit green, `ViewSynchronizer` SRP, no god `sync`.

## T9: Fix failing samples (bounded `300` frame harness)

**D** — Depends on `T5`+`T6`+`T8a`+`T8b` green (iteration 5 #2 fix — `T9/T10` now depend on `T8a`+`T8b` View split, `mutateCamera` deleted; `T9` diagnoses layer migration + techniqueOrder which assumes `T8` cache hashes stable; waiver `§ Consolidated Active Backlog` says `T9/T10 now depend on T8a+T8b`). Diagnose via `source tools/env.sh && eval "$LOOP_BUILD_TEST_CMD"` + headless sample runs `RE_SAMPLE_MAX_FRAMES=20 tools/build.sh && timeout ... build/app/re_sample_*`. Fix root cause (likely layer migration `LAYER_0` default or missing `techniqueOrder` entry for `Plane`/`Contour` or uninitialized `priority`). Keep harness bounded `kDefaultFrames=300` `app/sample_harness.hpp:175` `run(sampleMaxFrames)` discipline. No long-lived change here.

**T** — suite green `N>=3` where GL readback: primary `1/255` center-pixel vs `utils::loadMeshAsset` oracle via `View` path (`tools/run_sample.sh mesh|plane|volume|slice|oit|mpr` headless `RE_SAMPLE_MAX_FRAMES=20` — `exit 0` is secondary, not alone), bounded default `300` (`grep -c "kDefaultFrames = 300" app/sample_harness.hpp ==1` analytic 300) not interactive hang, `grep -c "1/255\|BudgetExceeded" tests/t9*.cpp ==1` (exact `==1` hand-counted, iteration 12 #3 fix — `>=1` would pass unrelated hits) analytic floor.

**G** — audit green, samples run, `FR-app.1/2/3` `1/255` smoke per `FR → T` matrix.

## T10: Long-lived samples (non-testing, opt-in `runInteractive` + camera `pan/rotate/zoom`)

**D** — Depends on `T9` + `T8a`+`T8b` (uses `setCamera` after `mutateCamera` removal, iteration 6 #2 fix — `T8` ambiguous post-split; `T9` already gates `T8a+b` but `T10` directly needs `mutateCamera` deletion `T8a` and `ViewSynchronizer` SRP `T8b`). Create `app/*_interactive.cpp` or `--interactive` flag set that bypasses `sampleMaxFrames` and uses `SampleHarness::runInteractive()` `app/sample_harness.cpp:111` `until shouldClose()` with `pan+rotate+zoom` camera interaction via `scene::CameraController` `scene/camera_controller.hpp` + `app::GlfwCameraInteractor` `app/glfw_camera_interactor.hpp` (left drag rotate `dx*0.5deg`, right drag pan `dx*0.01`, scroll/middle drag zoom `exp(-dy*0.02)`). Each long-lived target mirrors its bounded peer (mesh/plane/volume/slice/oit/mpr) but `renderFrame` calls `interactor.update(view)` before `syncRenderPresent`, respects `WantCaptureMouse`, skips orthographic slice views per `V5 T9` guard, and mutates via `view.setCamera(newCam)` (not `mutateCamera` lambda — deleted in `T8`) so `viewGen`/`cameraGen` + `generation` bump via `SceneStore::bump(FieldId)` lets `broker` re-translate only dirty camera fields `1e-6` analytic. Guard `tools/test.sh` and `ctest` never use them (exclude from `CMake RE_BUILD_SAMPLES` long-lived targets via `EXCLUDE_FROM_ALL` or separate `re_samples_long` target). Docs `docs/samples.md` note `long-lived not for testing`. `examples/` still NOT in `AUDIT_SOURCE_DIRS` per `README.md:33` — document waiver for `no_secrets`.

**T** — gate: new targets build, `build/app/re_sample_*_long --help` or `runInteractive` path exists + `GlfwCameraInteractor` `update()` wired (left rotate, right pan, scroll zoom within `1e-6`), `ctest` suite count unchanged (long sample not run), short bounded samples still `1/255` `300` frames; **analytic evidence (spec-review #4 BLOCKER):** headless `GlfwCameraInteractor::update(view)` with synthetic deltas `dx=10, dy=5` asserts `view.camera().viewMatrix()` within `1e-6` of closed-form `rotateY(0.5deg*dx)` / `pan(dx*0.01)` / `zoom exp(-dy*0.02)`, asserts `WantCaptureMouse` skip (no camera change when `ImGui::GetIO().WantCaptureMouse==true`) + orthographic slice-view guard (no rotate when view has `PlaneDesc`), `grep -c "1e-6\|1/255" tests/t10*.cpp ==2` (exact `==2` `rotate+pan`, iteration 13 #1) analytic floor.

**G** — suite green, audit green, long samples excluded from default build/test.

## T11a: Loader hardening — NRRD/Volume overflow & budgets (Caps tiled)

**D** — Depends on `T1` only (Caps probe — `SHA-256` prod from `T12` not needed for `T11a`; `T12` lands SHA-256 after `T7` per iteration 2 #6, `T11a` is parallel-eligible after `T1`/`T6` per ordering waiver §13.7 and executes in listed sequential order for review simplicity, iteration 2 #6 fix — `+ T12` removed from depends). Single domain NRRD/Volume only, <150 lines, one session (iteration 1 #2 split). `io/volume/nrrd_volume_loader.cpp:362-433` add `uint64_t` checked `voxelCount` (`__builtin_mul_overflow` or `checked_mul`), range-check `sizesValue[i] <= UINT32_MAX` before `static_cast<uint32_t>`, reject `size==0` with `VolumeIo::BudgetExceeded` (code 8) only when `core::Caps` probe also fails per `NFR §5` tiled streaming (product no cap via `core::Caps`, `BudgetExceeded` only on probe fail). `data/volume_dataset.hpp:44` + `volume_dataset.cpp:12` assert `voxels.size()==sx*sy*sz`, `sampleTrilinear:32` clamp guards `NaN` + `size==0` wrap. `volume/transfer_function.hpp:43` assert `points` non-empty sorted strictly increasing else typed error (prevent divide-by-0 `inf/NaN`). `kMaxFileBytes = 512^3*8+64KiB` pre-probe threshold per `NFR §5`. No OBJ/Image/Mesh here (those in `T11b`).

**T** — suite green `N>=3`: hostile `sizes: 4294967296 1 1` truncated NRRD → `BudgetExceeded` via Caps probe fail (domain `VolumeIo`, code 8, `kMaxFileBytes = 512^3*8+64KiB` pre-probe threshold per NFR §5, not via `>128³` cap per NFR §5 tiled — product No cap, ≤128³ sample exact within 1/255, arbitrary dims tiled) not crash, assert `result.error().domain==ErrorDomain::VolumeIo && error().code==8`, `sizeX==0` wrap caught, `FR-io.2` golden `data/volumes/sample_ct.nrrd` `EXPECT_EQ(sizes, 128×128×70)` exact + `sampleTrilinear` corner `EXPECT_NEAR(1e-6)` + tiled `256×128×64` synthetic > `maxTexture3DSize` via `core::Caps` mock `EXPECT_NEAR(tiledSample, ref, 1.0/255.0)` `1/255` (iteration 12 #2 fix — `any dims tiled` within `1/255` now mechanically asserted, not just `BudgetExceeded`) + `VolumeDataset::sampleTrilinear(NaN)` clamps not UB, `TransferFunction` duplicate points → typed error. `FR-data.3` + `FR-vol.1/2/3` gates still 1/255 / 1e-6; `FR-vol.3` ray/AABB step positions assert `EXPECT_NEAR(step, analytic, 1e-6)` via `volume/` oracle (active, not `hist` — spec-review #2); `grep -c "BudgetExceeded\|1e-6\|1/255" tests/t11a*.cpp ==2` (exact `==2` hand-counted, iteration 12 #3) + `grep -c "ErrorDomain::VolumeIo" tests/t11a*.cpp ==1` (exact `==1` hand-counted `ErrorDomain::VolumeIo` exactly 1, iteration 14 #1) analytic floor; `wc -l` delta <150 per file (overall <150, single domain).

**G** — audit green, `no_secrets`/`no_legacy_api` still green, no `re_io` leak to `scene/`; per-file `wc -l` delta <150 (`nrrd_volume_loader.cpp`/`volume_dataset` each <150) — single focus NRRD/Volume overflow & BudgetExceeded guards.

## T11b: Loader hardening — OBJ/Image/Mesh overflow & budgets + image pixel oracle

**D** — Independent except `T1` + `T11a` Caps pattern reference (parallel-eligible after `T1`/`T6`, sequential gate runs `T11a` then `T11b` — see ordering waiver, iteration 5 #1 fix — `T12` removed from parenthetical, `T12` is after `T11b` so not a prerequisite; `T11b` is parallel-eligible after `T1`/`T6` shared Caps pattern is read-only, `T12` SHA prod not needed per `T11a:D` iteration 2 #6). `io/mesh/obj_mesh_loader.cpp:21` remove `kMaxFaceVertices=64` cap — unbounded fan triangulation, accept negative relative indices (`-1` → last vertex) per OBJ spec, add `kMaxFileBytes` probe (like NRRD `512^3*8+64KiB`) before `positions.emplace_back`. `io/image/image_loader.cpp:36` add `file_size` pre-probe + `BudgetExceeded` (mirror NRRD), overflow check `size_t(width)*height` before `stbi_load`. `data/mesh.hpp:40` `fromTriangles` asserts `idx%3==0 && idx < positions.size()`. Add golden `data/fixtures/golden_rgba.png` `2×2` RGBA fixture (committed, SHA256 pinned in `T11b` gate) for `FR-io.3` pixel oracle — `utils::loadImageAsset` → dims `2×2` + corner/center RGBA within `1/255` (analytic, not `non-empty`). No NRRD/VolumeDataset here (those in `T11a`).

**T** — suite green `N>=3`: 100-vert n-gon OBJ fan-triangulates, negative indices resolve, 2GB `golden_image.png` → `BudgetExceeded` (domain `ImageIo`, code 8) before OOM via `file_size > kMaxFileBytes` pre-probe, `Mesh::fromTriangles` asserts guard, plus `FR-io.1` golden `data/meshes/bunny.obj` hand-count `vertices==35947` `faces==69451` (`indices==208353`) `EXPECT_EQ` + `AABB` `EXPECT_NEAR` `1e-6` via `Mesh::bounds`, plus `FR-io.3` golden `2×2` `loadImageAsset` → `EXPECT_EQ(dims,2×2)` + `EXPECT_NEAR(corner, expected, 1.0/255.0)` `1/255` via `PlaneRenderer` `T3b` secondary oracle + `sha256sum data/fixtures/golden_rgba.png` pinned; `FR-data.1` `faceNormal==cross` `1e-6` + `FR-data.2` `AABB exact` via `Mesh::bounds` `1e-6` still `1/255`/`1e-6`; `grep -c "BudgetExceeded\|1e-6\|1/255" tests/t11b*.cpp ==2` (exact `==2` hand-counted, iteration 12 #3) + `grep -c "ErrorDomain::ImageIo" tests/t11b*.cpp ==1 && grep -c "ErrorDomain::MeshIo" tests/t11b*.cpp ==1` (exact `==1` each, iteration 14 #1 fix — combined `ImageIo|MeshIo` `>=1` would pass single domain, now `==1` each hand-counted) analytic floor; `wc -l` delta <150 per file (overall <150, single domain).

**G** — audit green, `no_secrets`/`no_legacy_api` still green, no `re_io` leak to `scene/`; per-file `wc -l` delta <150 (`obj_mesh_loader.cpp`/`image_loader.cpp`/`mesh.hpp` each <150) — single focus OBJ/Image/Mesh overflow & BudgetExceeded guards + golden `2×2` `1/255` pixel oracle.

## T12: Canonical content-hash & spy depollution

**D** — Depends on `T7` (single identity — upgrades `FNV-1a` skeleton from `T7` to SHA-256 prod, iteration 1 #3 — `T7` gate is FNV, `T12` gate is SHA-256; ordering inversion clarified, no dependency violation). `data/content_hash.hpp:48-152` make `hashStableBytes` canonical: `float`/`uint32_t`/`int32_t` → little-endian bytes via `memcpy` + `htole32` (not `reinterpret_cast` host bytes), **SHA-256 truncated 64** per `SPEC §10.1` (`data/content_hash.hpp:31` source of truth, FNV-1a skeleton at `T7` already, SHA-256 prod lands here — iteration 1 #3), NaN payload canonicalized (`-NaN` → `NaN`), document `SPEC §10.1` hierarchical `Version:LayoutId:Type:Hash` determinism across runs. Move `contentHashSpy atomic<uint64_t>` out of `data/` (`data/content_hash.hpp:32`) into `test_utils/content_hash_spy.hpp` (data stays pure math, headless-testable without global mutable). `render/asset_registry.cpp:229` + `broker/mesh_object_mapper.cpp:17` ensure `SceneStore::meshAssets_` `AssetId` handle minted at `loadMeshAsset` (via `utils/asset_utils.hpp` from T1) is reused — `mapCached` hit must not re-hash full vertex buffer on every `setTransform` (bump only `generation`). `broker/material_mapper.cpp:18` fix `phongValueHash` XOR weak → SHA-256 continuation same as `render/asset_registry.cpp:44` `materialContentHash`, consistent field set (`baseColor+specular+shininess+ambient+diffuse`, `doubleSided` dropped with typed warning).

**T** — suite green: same mesh bytes on LE vs BE mock hash equal, `contentHashCallCount()==0` during 60-frame steady-state `setTransform` orbit (no per-frame re-hash), `material` identical bytes dedup to `materialSlotCount()==1` across two renderers, `grep -c "contentHashSpy" data/ ==0 && grep -c "contentHashSpy" test_utils/ ==1`.

**G** — audit green, `asset_indirection` still 0 hits, `data/` no `spdlog`/`atomic` pollution.

## T13: Offscreen/EGL ownership & REContext per-context isolation

**D** — Independent of `T5`..`T12` except `T11a`/`T11b` budgeting. `utils/offscreen_context.hpp:141-143` replace `void* eglDisplay_/eglContext_` + raw `GLFWwindow* window_` with RAII: `struct EglHandle{ EGLDisplay dpy; EGLContext ctx; EGLSurface surf; }` typed, `unique_ptr<GLFWwindow, GlfwDeleter>` or explicit `release()` with move-nulling. `utils/offscreen_context.cpp:313` fix `reinterpret_cast<void*>(&eglGetProcAddress)` UB → `reinterpret_cast<GlLoadProc>(eglGetProcAddress)`, guard `#include <EGL/egl.h>` with `#ifdef RE_HAS_EGL` (`utils/CMakeLists.txt:33` already probes `RE_EGL_LIBRARY`). `core/re_context.cpp:53` `thread_local REContext t_fallback` → **per-`EGLContext` map** (`unordered_map<EGLContext, REContextState>` mutex-guarded like `g_windowMap:49`, iteration 1 #11 pin — `invalidate()` on `release:129` alone is insufficient; map is binding) so second EGL context on same thread starts cold (no `viewport/clearColor` bleed). `utils/offscreen_context.cpp:199` save/restore `glfwWindowHint` globals (pollution to `core::Window`), after `loadCoreGl` verify `GL_MAJOR==4 && MINOR==6 && CORE_PROFILE` else typed error (SPEC §2 OpenGL 4.6 core). `core/re_context.cpp:191` document `REContext::current()` per-`EGLContext` map vs single-threaded contract (`nfr.md:24`) — keep mutex only for map, not `t_current`; `thread_local` fallback retained only for non-EGL fallback path.

**T** — suite green `N>=3`: two sequential `OffscreenContext` on same thread → second `viewport==0` cold not stale, `glfwWindowHint` pollution test (`OffscreenContext` then `Window::create` still `VISIBLE TRUE`), missing `libEGL.so` configure still passes (VG9), `EGL` context reports 4.6 core else error. `FR-core.1` still GL 4.6 core; `FR-core.2` malformed shader containing the known-bad token `glibberish` on line 7 returns typed error whose message contains both the token `glibberish` and the golden substring `ERROR: 0:7` exact (analytic, not `non-empty`), asserted via `auto msg=result.error().message; EXPECT_TRUE(msg.find("glibberish") != npos && msg.find("ERROR: 0:7") != npos)` (iteration 1 #12 typo fix — `message` → `msg`) plus `grep -c "glibberish\|ERROR: 0:7" tests/t13*.cpp ==1` (exact `==1` golden `ERROR: 0:7`, iteration 13 #1) — spec-review #1 fix, active gate replaces historic `COMPLETED_TASKS.md:T3` regression.

**G** — audit green, `gpu_api_ownership` `core|` still green, `utils/` no raw `gl*`.

## T14a: Generation/dirty — sync error & early-out

**D** — Depends on `T6`+`T8b`. Fix `ViewSynchronizer::sync` `broker/view_synchronizer.cpp:86-340`: (1) `120-126` silent success without `ViewCompositor` → typed error code 10, `lastStoreGen` not advanced. (2) `86-113` `O(views*items)` `curGen` recompute every `sync()` → early-out on `storeGeneration==lastSceneStoreGen_` before hashing (`SPEC §10.4` hybrid poll). (3) `39-49` `techniquePriorityFor` closed → already deleted in `T6`; keep `mapItemToLayer:342` dispatch via `Broker::getByKind` (OCP) as secondary.

**T** — suite green `N>=3`: `sync` without compositor → typed error code 10 `1e-6`; `storeGeneration` unchanged → `sync()` early-out `1e-6` (no `curGen` hash); `no_dump_sync` green; `grep -c "BudgetExceeded\|1e-6" tests/t14a*.cpp ==2` (exact `==2`, iteration 13 #1).

**G** — audit green, no `Noop`.

## T14b: Generation/dirty — StableKey + tombstone bound

**D** — Depends on `T14a`. Fix `StableKey` alias `cache_[key.id]` `broker/camera_mapper.cpp:66` `layoutId=0` → carry `layoutId` from `StableKey` `broker/stable_key.hpp:24`; `curGen` hash `103-106` stable after `T5` map delete (`layerOrderHash`); `scene/detail/generation_tracker.hpp:55` `tombstoneGen_` prune on `serialize`/`pruneOlderThan` with `Version` bump bounded `O(#FieldIds)`; `ViewStore::addView:295` seed `lightsGen/layerGen` from `generation`. `mutateCamera:97` already deleted in `T8a`.

**T** — suite green `N>=3`: two layouts same `viewId` different `layoutId` → distinct `ReCamera` `1e-6`/`1/255` (no alias); `tombstoneGen_.size()` bounded `O(#FieldIds)` after 10k cycles `1e-6`; `viewGen/projGen` 1e-6; `grep -c "1e-6\|1/255" tests/t14b*.cpp ==2` hand-counted (iteration 14 #2 fix — per-task grep floor, was missing).

**G** — audit green, `broker_per_type` 1-file-per-mapper.

## T15a: Broker/ownership — deps + ownership regex (early)

**D** — Depends on `T8b`+`T14b` (View split, StableKey stable). Harden false-negative floors early (no new feature code): `tools/audit.rules:42` `deps_pinned` remains `require_grep FetchContent_Declare\([^)]*GIT_TAG` **single-line floor** (`[^)]*` valid PCRE) — the binding multiline hard floor is `tools/audit.sh:142` `DEPS_PZO_OK` `grep -PRzo "FetchContent_Declare[^)]*GIT_TAG"` (Pzo, multiline, `[^)]*` excludes `)` and includes newline — spec-review #3 fix, not `[^]*?`); keep `utils/CMakeLists.txt:52` as secondary only. `43` `deps_pinned_no_branch` add `stable|next|latest/trunk/dev/vNext/1.x|refs/heads|origin/` denylist + comment allowlist. `153-155` `ownership_raw_ptr_*` → `grep -E "(const[[:space:]]+)?[A-Za-z_][A-Za-z0-9_:]*[[:space:]]*\*[[:space:]]*[a-z_]"` covering `Type*`/`Type *`/`void*`/`auto*` (broad ERE lands at `T15a` — narrow `Type*` until then per iteration 1 #4 option B, iteration 10 #1 fix — `\s` is PCRE, not ERE; `audit.rules:152-155` and `TASKS.md:243` `T` use `[[:space:]]`; `utils/offscreen_context.hpp:125` `GLFWwindow* /*borrow*/ handle()` + `core/window.hpp:64` `/*borrow*/` + `@note lifetime:` already annotated at iteration 1 #4 for future, remaining `auto*`/`void*`/`Type *` sites plus `const char*` will be `/*borrow*/` annotated at `T15a`);

**T** — suite green: `audit green` after `deps_pinned` + `ownership` tighten; `grep -c "GIT_TAG" CMakeLists.txt ==8` (pinned deps, analytic `==8` exact, iteration 6 #5 fix — `>=8` would pass 9 with duplicate/unpinned; `T16` `diff -u` byte-identical is binding) + `grep -c "1/255\|1e-6\|BudgetExceeded" tests/t15a*.cpp ==1` (exact `==1`, iteration 12 #3) analytic floor (secondary: existing mesh 1/255 parity via `utils::loadMeshAsset` still holds); pre-check `grep -R -n -E "(const[[:space:]]+)?(void|auto|[A-Z][A-Za-z0-9_:]*)[[:space:]]*\\*[[:space:]]*[a-z_]" --include="*.hpp" --include="*.cpp" scene/ broker/ app/ | wc -l` recorded before annotation and `grep -c "/\\*borrow\\*/" == N` after (iteration 9 #15 fix — `\\s` is PCRE `-P`, not POSIX `-E`; audit engine `grep -RIlE` uses ERE `[[:space:]]`, so example now uses `[[:space:]]` not `\\s`, per `env.md:23` iteration 7 #1; iteration 2 #7 — `void*`/`auto*`/`Type *` sites counted before, `/*borrow*/` sites after prove remaining sites annotated); `grep -E "Type * name" scene/` caught via `grep -E "Type * name"`; `deps_pinned` fails loudly if `CMakeLists.txt` loses `GIT_TAG`.

**G** — audit green, `comment_tag_context` PASS.

## T15b: Broker/audit — gpu + mapper + sanitize regex (late)

**D** — Depends on `T15a`. Harden remaining 3 floors after early green (narrow `\bgl.*\(` until `T15b`, `GL_*` leak fix deferred to `T15b` with comment scrub of `scene/depth_config.hpp` `render/offscreen.cpp` `render/screen_quad.cpp` etc. per reviewer #5 option B, plus `// utils egl* allowed` if needed); `T15b` will tighten `gpu_api_ownership` to `forbid_outside core|\bgl...|GL_...` (`\bgl[A-Z].*\(|GL_[A-Z_]+`) and harden the remaining 3: `99` `broker_per_type` → `grep -c "class.*Mapper" broker/*_mapper.hpp ==1` per-file; `100` `isp_mapper_forbid` → `grep -l "mapCached" broker/*_mapper.*` allowlist `ICachedMapper` `i_mapper.hpp`; `181` `no_per_target_sanitize` → `forbid_grep -E "add_compile_options.*-fsanitize|target_compile_options.*-fsanitize|add_link_options.*-fsanitize"` allowlist `INTERFACE re_project_sanitizers`; `broker/broker.hpp:297` `sceneKindAliases_` → `weak_ptr` — `ownership` broad already landed at `T15a`, `T15b` verifies still green.

**T** — suite green: audit still green after 4-rule tighten; `grep -c "class.*Mapper" broker/mesh_object_mapper.hpp ==1` per-file (analytic 1, `broker_per_type`) + `grep -R "GL_[A-Z_]+" render/ scene/ | grep -v "MESA_GL_VERSION" | wc -l ==0` after `GL_*` scrub (iteration 2 #10 — `MESA_GL_VERSION_OVERRIDE` env var excluded, `GL_R32F`/`GL_MAX_3D_TEXTURE_SIZE` comments scrubbed at `T15b`) + `grep -c "1/255\|1e-6\|BudgetExceeded" tests/t15b*.cpp ==1` (exact `==1`, iteration 12 #3) secondary (1/255 swap invariant via `techniqueOrder` still green); `no_per_target_sanitize` `grep -c "INTERFACE re_project_sanitizers" CMakeLists.txt ==1` (analytic `==1` exact, iteration 7 #2 fix — `>=1` would pass duplicate).

**G** — audit green, 8-rule floor hardened, no `__never_matches` reintroduced.

## T16: Build/export & audit defaults hardening

**D** — Independent of `T7`..`T15` except audit. Keep `tools/audit.sh:62` default as-is per `AGENTS.md:28` (`AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` default passes even if forgotten — `TASKS.md R15` `T1` gate test is the fail-loud, spec-review #3). Add comment in `tools/audit.sh` noting `R15` is gate path. **Definitive** `CMakeLists.txt:58` **already deleted** `find_package(glfw3/glm/spdlog/json QUIET)` before `FetchContent` at spec-review #1 (no version-equality fallback — `FetchContent GIT_TAG` is sole source per `SPEC §2`); audit `deps_pinned_no_branch` + `deps_pinned_no_find_package` then has no bypass — `T16` verifies no reintroduction via `grep -c "find_package.*glfw" CMakeLists.txt ==0` and that `GIT_TAG` SHAs match `docs/spec/techstack.md` §2 canonical table (spec-review #16 DRY). `examples/CMakeLists.txt:104` delete `LINKER:--unresolved-symbols=ignore-all`, fix `app/CMakeLists.txt:28` `re_imgui PUBLIC glfw` → `PRIVATE` or `$<BUILD_INTERFACE:glfw>`, `core/CMakeLists.txt:45` already `PRIVATE glad` stays. `tools/env.sh:10-15` `LSAN_OPTIONS` cwd brittle `tools/lsan.supp` relative → `ROOT="$(cd "$(dirname "$BASH_SOURCE")"/.. && pwd)"` absolute. `utils/CMakeLists.txt:42` `AUDIT_SOURCE_DIRS` grey-zone doc → already in `tools/env.sh:6` keep. `examples/` intentionally NOT in `AUDIT_SOURCE_DIRS` per `README.md:33` but `no_secrets` still scans via built-in — document waiver comment in `tools/audit.rules`.

**T** — suite green: `AUDIT_SOURCE_DIRS` default in `tools/audit.sh` still `io data volume scene core broker render app utils test_utils tests` (no exit 2), `R15` gate test `grep -c "AUDIT_SOURCE_DIRS" build/CMakeCache` or `tools/test.sh` env check still fails loudly if `source tools/env.sh` forgotten (T1 gate already enforces `AUDIT_SOURCE_DIRS` exact + `LOOP_BUILD_TEST_CMD` non-empty, spec-review #3); `examples/minimal` build without `--unresolved-symbols` still links (or fails loudly revealing missing `find_dependency`), `LSAN_OPTIONS` absolute `$ROOT/tools/lsan.supp` correct when sourced from `build/` subdir, `re_app` consumer no longer inherits `-Werror` via `INTERFACE re_project_warnings` (`app/CMakeLists.txt:54` `PUBLIC` → `PRIVATE` or `$<BUILD_INTERFACE>`); verify `grep -c "find_package.*glfw" CMakeLists.txt ==0` (spec-review #1, no fallback) and `bash -c 'diff -u <(grep -Eoh "GIT_TAG[[:space:]]+[A-Za-z0-9._-]+" CMakeLists.txt | sort -u) <(grep -Eoh "GIT_TAG[[:space:]]+[A-Za-z0-9._-]+" docs/spec/techstack.md | sort -u)' ==0` (SHA diff, iteration 15 #1 fix — `grep -oh "GIT_TAG[[:space:]].*"` captures trailing prose `` ` `` + prose, so `diff -u` never byte-identical even when pins match; normalize to token only `GIT_TAG[[:space:]]+[A-Za-z0-9._-]+` via `grep -Eoh` then `sort -u`, keep `bash -c` wrapper per `tools/env.sh:bash` iteration 14 #5; `grep -Eoh "GIT_TAG[[:space:]]+[A-Za-z0-9._-]+"` relies on GNU `-P` and `[\n]` bracket — portable `grep -Eoh "GIT_TAG[[:space:]]+.*"` not `[^\n]`, but token-only is binding; full `GIT_TAG` tokens `3.4`, `v2.0.8`, `1.0.1`, etc. must be byte-identical to `techstack.md:14-21` canonical pins; tag is immutable but SHA in `techstack.md` is manual `git ls-remote --tags` verification pre-setup, iteration 5 #7 — tag move would not be caught mechanically, reviewer runs `git ls-remote --tags <url> <tag>` manually) + `grep -c "GIT_TAG" CMakeLists.txt ==8` exact `==8` (iteration 6 #5 fix — `>=8` would pass 9) still floor is `diff -u` byte-identical; analytic secondary: short bounded samples still `1/255` (`T9` smoke) + `grep -c "1/255\|BudgetExceeded" tests/t16*.cpp ==1` (exact `==1`, iteration 12 #3).

**G** — suite green, audit green, `install(TARGETS re_core ...)` `RenderEngineConfig.cmake` `find_dependency` still green via `examples` probe.

## T17: Harden GL ownership — `core/rhi/gl/` RHI anchor (stretch)

**D** — `(stretch)` — deferred per `SPEC §3/§6` `core/rhi/` still absent, `gpu_api_ownership core|` remains sole anchor. When activated, uncomment `tools/audit.rules:50` `rhi_ownership forbid_outside core/rhi/gl|\bgl[A-Z]` ; move `core/re_context.cpp:28` `#include <glad/gl.h>` + raw `gl*` bodies under `core/rhi/gl/` `IRHIContext{ITexture,IBuffer,IFramebuffer,IShader,blit,capabilities}` (Qt QRhi/O3DE/Adept per `docs/spec/modules.md:34`). `core/re_context.hpp` includes `core/rhi/irhi_context.hpp` not `<glad>`. Update `docs/spec/guardrails.md:13` transitional note to landed, keep `gpu_api_ownership` as `core/rhi/gl|` hard. Keep `render_no_glad` `forbid_inside render|#include.*glad` still green. Until activation, this task stays **red** and does not gate `T18`.

**T** — when activated, suite green: `grep -c "rhi_ownership" tools/audit.rules ==1 active && grep -c "gl[A-Z]" core/rhi/gl/re_context.cpp >=1 && grep -c "glViewport\|glClearColor" core/*.cpp ==0` outside `core/rhi/gl/`.

**G** — audit green, `rhi_ownership` enforced, `render/` no `glad` — **not required for loop green; stretch**.

## T18: Evidence & audit placeholder activation (stretch)

**D** — `(stretch)` — `evidence_rule`/`regression_lock` remain **review-gated** per `SPEC §6` + `tools/audit.rules:158-165` — placeholders `__never_matches_*` are intentional (mechanical floor is per-task `grep -c "1/255"` / `"1e-6"` / `"152 MB"` / `"BudgetExceeded"` counts + review checklist, not a generic `require_grep 1/255|1e-6`). When activated, replace only `weak_assert_review_gated` false-negative with tighter per-task counts, **do not** replace review-gated `evidence_rule`/`regression_lock` with generic `require_grep`. Uncomment or delete `tools/audit.rules:88-89` `layer_count/layer_mask` correctly: `layer_mask` `using LayerMask = uint32_t` line stays **deleted** (dumb `LAYER_0..7` has no `LayerMask`), `layer_count` `Count[[:space:]]*=[[:space:]]*8` if re-added must match `COUNT=8` in `scene/layer.hpp`. Enforce `find_package` fallback pin already covered in `T16`; enforce `1/255` per `render/*` gate via review checklist, not bare audit `>0`.

**T** — when activated, suite green: `grep -c "__never_matches_weak_assert" tools/audit.rules ==0` (if replaced) but `grep -c "__never_matches_evidence_rule__" tools/audit.rules ==1` + `grep -c "__never_matches_regression_lock__" tools/audit.rules ==1` remain (review-gated); `grep -c "1/255\|1e-6" tests/*.cpp >= 8`; `audit green`.

**G** — audit green, no spurious `LayerMask` resurrection, `deps_pinned` still `GIT_TAG` verified — **not required for loop green; stretch**.

> **Note V6 (archive):** `V6 T6.1/T6.2` retired — their D/T/G moved to `T5/T6` above for single `Version 1->2` migration. `V6 T6.3/T6.4` now `T9/T10` and execute after `T5/T6` green per ordering rule `Store→Engine→render collapse→depth→layers→asset→View split→samples`.

## Documentation map (V6, iteration 2 #12 — R9 gate)

Every `T` lists the exact files its `R9` doc gate must update (reviewer verifies `R9` via `git diff --stat`). Historical `V2-T1..V2-T8` + `V3 T1..T23` etc. maps archived in `COMPLETED_TASKS.md`; this table is the binding `T1..T18` map for the current iteration:

| T | Title | Docs updated (R9) |
|---|---|---|
| T1 | Store IO depollution | `scene/store.hpp` `scene/store.cpp` `scene/CMakeLists.txt` `utils/asset_utils.hpp` `docs/spec/modules.md:21` |
| T2 | Engine IO depollution | `include/render_engine/engine.hpp` `include/CMakeLists.txt` `app/mesh_sample.cpp` |
| T3a/b | Collapse `render()` dual API | `render/mesh_renderer.hpp` `render/slice_renderer.hpp` `render/plane_renderer.hpp` `render/volume_renderer.hpp` `render/volume_slice_renderer.hpp` `render/contour_renderer.hpp` `render/i_renderable.hpp` |
| T4 | Scope depth per-View | `scene/view.hpp` `scene/depth_config.hpp` `render/view.hpp` `render/view_target.hpp` `core/re_context.cpp` `include/render_engine/engine.hpp` `docs/spec/guardrails.md:90` |
| T5 | Dumb layers `LAYER_0..7` + priority | `scene/layer.hpp` `scene/iscene_object.hpp` `scene/field_id.hpp` `scene/objects/*.hpp` `scene/view.hpp` `scene/store.hpp` `docs/spec/modules.md:20` |
| T6 | Global `techniqueOrder` + scoped priority | `broker/render_stack.hpp` `broker/view_synchronizer.hpp` `broker/view_synchronizer.cpp` `render/i_renderable.hpp` `docs/spec/persistence.md:10.2` |
| T7 | Unify asset identity | `data/content_hash.hpp` `scene/store.hpp` `broker/asset_store.hpp` `render/asset_registry.hpp` `render/volume_renderer.hpp` `render/volume_slice_renderer.hpp` `render/plane_renderer.hpp` `docs/spec/assets.md:64` |
| T8a/b | Split View god | `scene/view.hpp` `scene/view_state.hpp` `broker/view_synchronizer.cpp` `broker/view_synchronizer.hpp` `docs/spec/modules.md:20` |
| T9 | Fix failing samples (bounded 300) | `app/sample_harness.hpp` `app/*_sample.cpp` `docs/samples.md` |
| T10 | Long-lived samples | `app/*_interactive.cpp` `app/sample_harness.cpp` `scene/camera_controller.hpp` `app/glfw_camera_interactor.hpp` `docs/samples.md` |
| T11a/b | Loader hardening | `io/volume/nrrd_volume_loader.cpp` `data/volume_dataset.hpp` `data/volume_dataset.cpp` `volume/transfer_function.hpp` (T11a) / `io/mesh/obj_mesh_loader.cpp` `io/image/image_loader.cpp` `data/mesh.hpp` `data/fixtures/golden_rgba.png` (T11b) `docs/spec/nfr.md:29` |
| T12 | Canonical content-hash & spy depollution | `data/content_hash.hpp` `test_utils/content_hash_spy.hpp` `render/asset_registry.cpp` `broker/mesh_object_mapper.cpp` `broker/material_mapper.cpp` `docs/spec/persistence.md:10.1` |
| T13 | Offscreen/EGL ownership & REContext isolation | `utils/offscreen_context.hpp` `utils/offscreen_context.cpp` `core/re_context.cpp` `core/window.hpp` `docs/spec/env.md:14` |
| T14a/b | Generation/dirty | `broker/view_synchronizer.cpp` `broker/camera_mapper.cpp` `scene/detail/generation_tracker.hpp` `scene/view_store.hpp` |
| T15a/b | Broker/ownership — audit hardening | `tools/audit.rules` `utils/offscreen_context.hpp` `core/window.hpp` `broker/broker.hpp` `docs/spec/guardrails.md:13` `AGENTS.md:29` |
| T16 | Build/export & audit defaults | `tools/audit.sh` `CMakeLists.txt` `examples/CMakeLists.txt` `app/CMakeLists.txt` `core/CMakeLists.txt` `tools/env.sh` `docs/spec/techstack.md:14` `docs/spec/assets.md:62` |
| T17 | Harden GL ownership (stretch) | `core/rhi/gl/` `core/re_context.cpp` `tools/audit.rules` `docs/spec/guardrails.md:13` |
| T18 | Evidence & audit placeholder activation (stretch) | `tools/audit.rules` `docs/spec/guardrails.md:32` |
| T19 | Fix volume/slice bounded samples — builder state loss (hotfix 2026-08-30) | `app/volume_sample.cpp` `app/slice_sample.cpp` `scene/builders.hpp` `docs/samples.md` |
| T20 | Eliminate sample memory leaks — LSAN suppressions + genuine fixes (hotfix 2026-08-30) | `tools/lsan.supp` `tools/env.sh` `core/window.cpp` `core/glfw_runtime.cpp` `utils/offscreen_context.cpp` `docs/spec/env.md` `docs/spec/nfr.md` |


## T19: Fix volume and slice bounded samples — builder view state loss (hotfix 2026-08-30, user-reported "volume and slice renderer samples are not working")

**D** — Depends on `T7` (SceneViewBuilder single helper) + `T9` (bounded 300-frame harness) + `T10` (long-lived peers already fixed). Root-cause (verified 2026-08-30 via `app/volume_sample.cpp:85-105` + `app/slice_sample.cpp:105-124` vs `app/volume_long.cpp:57-66` + `app/slice_long.cpp:62-99`): the bounded samples construct `scene::SceneViewBuilder bld(1, Rect{0,0,800,600}, framing)`, call `bld.syncLive(800,600)` which only touches `rect` + `camera` perspective, then set `view_.setItemIds({id})` / `view_.setPlane(plane)` / `view_.setClearColor(...)` on the local `view_` copy. They then do `builder_ = std::move(bld)` **without** syncing the builder's view — the builder's stored `View` still has empty `itemIds` and `nullopt plane`. Every `renderFrame` (volume `app/volume_sample.cpp:101-106`, slice `app/slice_sample.cpp:121-126`) does `interactor_.update(builder_.view()); builder_.syncLive(width,height); view_ = builder_.view(); views_={view_}; syncRenderPresent(ctx_,views_)` — overwriting `view_` with the builder's stale empty view, so `syncRenderPresent` receives a `View` with zero items (volume) or no plane (slice). The bridge therefore issues zero draw calls and the frame stays clearColor (black) — the user-visible "not working". The long-lived peers already fix this: `volume_long.cpp:66` `builder_.view() = view_;` after init, and `slice_long.cpp:72`/`81`/`99` `builder_.view() = view_;` + save/restore `PlaneDesc p = view_.plane.value(); view_ = builder_.view(); view_.setPlane(p); builder_.view()=view_;` preserving plane across `syncLive`. Fix bounded peers to mirror long-lived pattern: after initial `view_.setItemIds`/`setPlane`/`setClearColor` assign `builder_.view() = view_;`; in `renderFrame`/`onResize` preserve `itemIds`/`plane`/`clearColor`/`depthConfig` across `syncLive` (either by assigning builder view after modification or by save/restore as slice_long does). `applyLiveDims` / `syncLive` in `scene/builders.hpp:82-93` is intentionally scoped to `rect` + `camera` only (SRP — one helper, not six private duplicates per T7), so callers must carry other View fields. No `SceneStore` or `Broker` changes — only the two sample files (≤30 lines each). `plane_sample.cpp:203-207` `applyLiveRects` already preserves items/plane by touching only `setRect`, proving the correct pattern; `oit_sample.cpp:155-172` `syncViewSize` also preserves `itemIds` by mutating `view_.setRect`/`setCamera` directly, not by wholesale `view_ = builder.view()`.

**T** — suite green `N>=3` `llvmpipe`: (1) `grep -c "builder_.view() = view_" app/volume_sample.cpp ==1 && grep -c "builder_.view() = view_" app/slice_sample.cpp >=1` (builder carries itemIds/plane after init); (2) headless sample smoke `RE_SAMPLE_MAX_FRAMES=2` via `source tools/env.sh && ASAN_OPTIONS=detect_leaks=0` (leak gate is T20, this gate proves rendering, not leaks) for `re_sample_volume` + `re_sample_slice` exits 0 without `frame failed` log; (3) offscreen pixel parity via `utils::OffscreenContext` + `broker::AppContext::syncRenderPresent` oracle: construct `VolumeSample`/`SliceSample` scene via `AppContext` (same store/ids as sample), render via `renderViews` into 800×600 offscreen FBO, read center pixel via `core::REContext::readRgba8` — volume center pixel within `1.0/255.0` of analytic CPU ray-cast `volume::compositeFrontToBack` with `app::RE_CT_TF()` (spec `FR-render.6` 1/255), slice center pixel alpha==1.0 and not clearColor `0.10,0.10,0.12` within 1/255, and `EXPECT_TRUE(view.plane.has_value())` + `EXPECT_EQ(view.itemIds().size(),1)` before sync. `grep -c "1/255\|1e-6" tests/t19*.cpp ==1` exact (hand-counted analytic floor, not bare `>0`). `slice_long` / `volume_long` still green, `plane`/`oit`/`mpr` smoke still green.

**G** — suite green `N>=3`, audit green, `render_no_window` etc. still green, `volume` + `slice` bounded samples render correct content (volume ray-cast center within 1/255, slice clipped teapot not black) and `RE_SAMPLE_MAX_FRAMES=2` smoke green for all six samples.

## T20: Eliminate sample memory leaks — LSAN suppressions + genuine leak fixes (hotfix 2026-08-30, user-reported "all samples have memory leaks")

**D** — Depends on `T15` (GlfwRuntime refcount) + `T13` (OffscreenContext EGL/RAII) + `T16` (LSAN_OPTIONS absolute path). Root-cause (verified 2026-08-30 via `source tools/env.sh && RE_SAMPLE_MAX_FRAMES=1 ./build/app/re_sample_* 2>&1 | grep LeakSanitizer`): every bounded sample aborts under `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1` (the env.sh gate) with `LSAN: detected memory leaks`. Two distinct sources: (1) **Missing suppressions file** — `tools/env.sh:15-17` exports `LSAN_OPTIONS="suppressions=$ROOT/tools/lsan.supp:print_suppressions=0"` when `tools/lsan.supp` exists, else `LSAN_OPTIONS="print_suppressions=0"` with no suppressions; repo only ships `tools/lsan.supp.template` containing `leak:<unknown module>` and no project file, so `tools/lsan.supp` does not exist (`ls tools/lsan.supp` fails) and `detect_leaks=1` reports every Wayland/driver allocation as fatal. The 2026-08-30 LSAN logs show two leak families: `Direct leak 2680 bytes in 1 object allocated from _glfwInitEGL @ build/_deps/glfw-src/src/egl_context.c:494 calloc` (GLFW 3.4 EGL TLS display connection leaked even after `glfwTerminate` — known GLFW Wayland/EGL bug, 2680 bytes per process, present in all six samples) and `Indirect leak 3 bytes / 512 bytes / 2 bytes ... allocated from libfontconfig.so FcValueSave / libpangoft2 / libcairo / libgtk-3 / libdecor-gtk.so libdecor_frame_commit` (Wayland decoration + fontconfig/Pango/Cairo GTK stack leaks triggered by `glfwCreateWindow` with `GLFW_VISIBLE TRUE` on Wayland — libdecor 0+libdecor-gtk, FcFontRenderPrepare, pango_context_get_metrics). Tests (`re_tests`) are green because they use `utils::OffscreenContext` hidden-window/EGL-surfaceless path, not the visible `core::Window` decoration path, so they never trigger libdecor/fontconfig. (2) **No genuine code leaks beyond suppressible driver noise** — `core/window.cpp:65-92` `release()` + `core/glfw_runtime.cpp:54-60` `~GlfwRuntime` refcounted `glfwTerminate` + `utils/offscreen_context.cpp:135-186` `release()` already destroy `GLFWwindow`/`EGLContext` and clear `REContext` maps; the 2680-byte `calloc` is inside `libwayland-client`/`_glfwInitEGL` and not freed by `glfwTerminate`, so it must be suppressed, not "fixed" by code. Fix: create `tools/lsan.supp` (copy `tools/lsan.supp.template` then extend) with LSAN `leak:` rules suppressing the driver noise while preserving project leak detection: `leak:<unknown module>` (EGL driver `<unknown module>` TLS), `leak:libwayland-client`, `leak:libdecor`, `leak:libdecor-gtk`, `leak:libfontconfig`, `leak:libpango`, `leak:libcairo`, `leak:libglib`, `leak:libgtk-3`, `leak:Fc*`, `leak:pango*`, `leak:gtk*`, `leak:_glfwInitEGL`, `leak:eglInitialize`, `leak:glfwCreateWindow`, `leak:libxkbcommon`, `leak:libX11` (cover Wayland/X11 EGL paths). Keep `leak:<unknown module>` as first line per template. Document suppressions in `docs/spec/env.md:30` + `docs/spec/nfr.md:16` (LSAN suppressions for llvmpipe/Wayland driver noise, not for project code). Verify `tools/env.sh:15` already uses absolute `$ROOT/tools/lsan.supp` (T16 fix) — no env change needed, only the missing file. No `ASAN_OPTIONS=detect_leaks=0` workaround in samples — samples must run with `detect_leaks=1` and suppressions, so genuine leaks (e.g., missed `glDeleteTextures`) would still be caught. `examples/` already excluded from `AUDIT_SOURCE_DIRS` but built-in `no secrets` whole-tree scan still covers it per `audit.sh:148-154`.

**T** — suite green `N>=3` `llvmpipe` with LSAN enabled: `source tools/env.sh && eval "$LOOP_BUILD_TEST_CMD"` still green (no `LeakSanitizer` in `ctest` output); then for each bounded sample `re_sample_{mesh,plane,volume,slice,oit,mpr}` run `RE_SAMPLE_MAX_FRAMES=2` under `source tools/env.sh` (inherits `LSAN_OPTIONS=suppressions=$ROOT/tools/lsan.supp` + `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1`) headlessly via `Xvfb`/`GALLIUM_DRIVER=llvmpipe` exits 0 with `grep -c "LeakSanitizer: detected" ==0` and `grep -c "ERROR: LeakSanitizer" ==0` (no LSAN abort). Also `test -f tools/lsan.supp && grep -c "^leak:" tools/lsan.supp >=8` (at least 8 suppression lines covering `<unknown module>`, `libwayland`, `libdecor`, `fontconfig`, `pango`, `cairo`, `gtk`, `_glfwInitEGL`) and `grep -q "leak:<unknown module>" tools/lsan.supp` (template preserved). `grep -c "1/255\|1e-6\|BudgetExceeded" tests/t20*.cpp ==1` exact analytic floor (hand-counted, not `>=1`). Existing `re_tests` still `N>=3` green with suppressions.

**G** — suite green `N>=3`, audit green, `no_per_target_sanitize` still green, all six samples LSAN-clean (`LeakSanitizer: detected` 0 hits) under `source tools/env.sh` + `detect_leaks=1`, `tools/lsan.supp` committed and documented.

## Definition of Done — Consolidated V6 (T1..T16 binding inc. splits T3a/b, T8a/b, T11a/b, T14a/b, T15a/b = 21 active + T17/T18 stretch = 23 total, iteration 1 #2 fixed count)

### Required DoD (T1..T16 gating = 21 active tasks incl. splits T3a/b,T8a/b,T11a/b,T14a/b,T15a/b — iteration 1 #2 fixed count)
- [ ] All `T1..T16` (incl. `T3a/b`, `T8a/b`, `T11a/b`, `T14a/b`, `T15a/b` = 21 active) gates green; full suite green `N>=3` on `llvmpipe` at `T16` (stretch `T17/T18` remain red, not gating)
- [ ] `audit green` with tightened `deps_pinned` (`grep -Pzo` multiline per `T15a` #5), `broker_per_type`/`isp_mapper_forbid` (`grep -c "class.*Mapper"==1` per-file per `T15b`), `no_per_target_sanitize`/`ownership_raw_ptr_*` (`Type*`/`Type *`/`void*` broad ERE per `T15a` #11 — narrow `Type*` until `T15a`, broad lands at `T15a` with `GLFWwindow* /*borrow*/` already annotated at iteration 1 #4 for future, per reviewer #4 option B), `gpu_api_ownership` (`\bgl.*\(` per `T15b` #9 — narrow `\bgl.*\(` until `T15b`, `GL_*` leak fix deferred to `T15b` with comment scrub of `scene/depth_config.hpp`/`render/offscreen.cpp`/`render/screen_quad.cpp` etc., per reviewer #5 option B) — `rhi_ownership` stays commented (`core|` until **(stretch) T17** per `AGENTS.md`/`guardrails.md` #9, required audit); `evidence_rule`/`regression_lock` stay `__never_matches` review-gated, only `weak_assert` tightened via per-task `grep -c "1/255"` (spec-review #6)
- [ ] Evidence floor (spec-review #6): `grep -c "1/255\|1e-6" tests/*.cpp >=8` + per-task `BudgetExceeded|152 MB` counts (`T11a` hostile `4294967296` → `BudgetExceeded` #7 via Caps probe fail not >128³, `T11b` `2×2` `1/255` golden `FR-io.3` #1, `T14a` code 10, `T15a` `GIT_TAG==8` exact `==8` not `>=8`, iteration 7 #3 fix), `N>=3` for GL readback (`T3a/b`, `T14b` `1e-6`), `audit green` with `rhi_ownership` still commented, `LSAN_OPTIONS` absolute `$ROOT/tools/lsan.supp` per `T16` #10
- [ ] `Store` + `Engine` IO depolluted (`grep -c "loadObjMesh\|loadNrrdVolume" scene/ include/ ==0`, `re_io` not linked to `re_scene`/`re_engine`, only `utils/asset_utils.hpp` owns `io/`)
- [ ] Single `drawLayer` path (`grep -c "data::Result<void> render(" render/*_renderer.hpp ==0` via `T3a` 3/6 then `T3b` 6/6), single OIT via `ViewCompositor::captureTransparents`
- [ ] Depth per-View `DepthConfig` (not per-layer): `grep -c "depthTest" scene/view.hpp ==0` raw bool gone, `grep -c "struct DepthConfig" scene/depth_config.hpp ==1` (file-specific, iteration 1 #8 exact `==1`) `&& test $(grep -Rl "DepthConfig" scene/ | wc -l) ==2` (exact 2 files: `scene/depth_config.hpp` + `scene/view.hpp` via `grep -Rl`, iteration 1 #8 tightens loose `>=1` line-count to file-count `==2`) value object kept, `engine_depth_default` `DepthConfig\{true` in `include/` still 1, heterogeneous `VolumeSlice+Mesh` depth-correct 1/255 via per-View `DepthConfig` (`Engine` defaults true for mesh Views)
- [ ] Dumb `LAYER_0..7` + `techniqueOrder` + scoped `priority` + `stable_sort` (`grep -c "LayerMask" scene/ ==0`, `grep -c "LAYER_0" scene/layer.hpp ==1`, `Version 1->2` single migration)
- [ ] Unified content-hash dedup (`grep -c "byObject_\|legacyHandleCache" render/ ==0`, `grep -c "AssetStore" broker/ ==0` mesh-only store gone, `byHash_` only, `T12` SHA-256 truncated 64)
- [ ] View/Synchronizer split (`grep -c "rectGen.*itemsGen" scene/view.hpp ==6` primary per #8, `wc -l scene/view.hpp <90` secondary via `T8a`; `wc -l broker/view_synchronizer.cpp <220` via `T8b`, no `mutateCamera`)
- [ ] Samples: bounded `300` harness green `N>=3` `1/255` (`T9` `kDefaultFrames = 300` `grep -c "kDefaultFrames = 300" ==1` analytic 300, iteration 3 #1 fix — DoD `==1` was drift vs `T9` `300` binding) + long-lived `runInteractive` excluded from `ctest` via `EXCLUDE_FROM_ALL` (`ctest` count unchanged, short samples still `1/255` `300` frames)
- [ ] Loader hardening + canonical hash + EGL isolation + generation contract + build defaults (`T11a/b`..`T16` `1/255|1e-6|BudgetExceeded|152 MB` evidence, no `re_io` leak to `scene/`, `data/` no spy, `LSAN` absolute)
- [ ] `LICENSE` + `install` + `docs` (spec-review #2): `audit.sh` per-dir `LICENSE` PASS (`data/meshes/LICENSE` + `data/volumes/LICENSE` per `assets.md` §7 + `T2` gate) + `install(TARGETS re_core re_scene re_broker re_render re_app EXPORT RenderEngineTargets)` reproduces + `find_package(RenderEngine 0.1.0)` smoke (`RenderEngineConfig.cmake` `SameMajorVersion 0.1.0`, spec-review #14) + `R9` doc-map per `TASKS.md` task map + `comment_tag_context` PASS (`tools/audit.rules:204` + `tools/comment_context.allow`) — `audit green` implies these but DoD lists them explicitly

### Stretch DoD (T17 RHI, T18 evidence — not gating until activated, spec-review #12)
- [ ] `T17`/`T18` `(stretch)` remain red until activated and do not gate green — required DoD passes with them red; stretch audit is `audit green + rhi_ownership active` (`core/rhi/gl|`), not `core|` (see `TASKS.md:T17:G`); `T17` enforces `grep -c "rhi_ownership" ==1 active && grep -c "glViewport\|glClearColor" core/*.cpp ==0` outside `core/rhi/gl/`; `T18` enforces `grep -c "__never_matches_evidence_rule__" ==1` + `grep -c "1/255\|1e-6" >=8` still green

### Hotfix DoD (T19+T20 — 2026-08-30, urgent user-reported volume/slice + leaks, after V6)
- [ ] `T19` green: `app/volume_sample.cpp` + `app/slice_sample.cpp` carry `builder_.view() = view_` after init + `renderFrame`/`onResize` preserve `plane`/`itemIds` across `syncLive` (mirroring `volume_long`/`slice_long`), offscreen pixel parity 1/255 (volume ray-cast center within 1/255 of CPU `compositeFrontToBack` with `RE_CT_TF()`, slice not clearColor), `RE_SAMPLE_MAX_FRAMES=2` smoke green for `volume`+`slice`; `grep -c "builder_.view() = view_" app/volume_sample.cpp ==1` etc.
- [ ] `T20` green: `tools/lsan.supp` exists with `leak:<unknown module>` + `leak:libfontconfig`/`libdecor`/`libwayland`/`_glfwInitEGL` etc. `>=8` `^leak:` lines, all six samples `RE_SAMPLE_MAX_FRAMES=2` under `source tools/env.sh` + `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1` exit 0 with `LeakSanitizer: detected ==0`, `ctest` still `N>=3` green, `grep -c "1/255\|1e-6\|BudgetExceeded" tests/t20*.cpp ==1`
- [ ] `audit green` with new suppressions file present and samples LSAN-clean; `evidence_analytic` floor `>=8` still green

