# TASKS — RenderEngine

Sequential, gated, review-before-commit loop. One task per session. See
SPEC.md for FRs, §6 for guardrails, NAMING_CONVENTIONS.md for style.

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
  `AUDIT_SOURCE_DIRS="io data volume core render app tests"` and
  `LOOP_BUILD_TEST_CMD` into the runner's environment. T1's gate makes this
  enforceable: a forgotten source must FAIL the gate loudly, never silently
  audit the wrong dirs.
- Every gate also runs the mechanical audit (`tools/audit.sh`) with
  `AUDIT_SOURCE_DIRS="io data volume core render app tests"` (via
  `source tools/env.sh`).
- Unit tests build with ASan+UBSan; GL-touching tests use the offscreen GL
  fixture (headless). No exceptions in v1; typed `Result` errors (SPEC §5).
- Logging via spdlog only (no printf/cout). Doxygen on all public API.

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
offscreen fixture creates a GL 3.3 core context — asserted via
`glGetIntegerv(GL_MAJOR_VERSION)==3`, `glGetIntegerv(GL_MINOR_VERSION)==3`,
and `GL_CONTEXT_PROFILE_MASK & GL_CONTEXT_CORE_PROFILE_BIT` (not the
unreliable `glGetString(GL_VERSION)` string) — and reports no GL errors;
(4) the gate environment is correctly sourced: `$AUDIT_SOURCE_DIRS` equals
`io data volume core render app tests` and `$LOOP_BUILD_TEST_CMD` is
non-empty (R15 — a forgotten `source tools/env.sh` fails loudly instead of
the audit silently scanning default dirs); (5) audit passes with our
source-dir override.

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

- [ ] All 15 task gates green; full suite green on a clean tree at the last task.
- [ ] GPU/readback tests (T7–T11, T14, T15) verified with **N>=3 consecutive
      green runs** (records in `tools/logs/`).
- [ ] Mechanical audit green (`tools/audit.sh`) with
      `AUDIT_SOURCE_DIRS="io data volume core render app tests"`.
- [ ] ASan+UBSan clean on all test binaries (no leaks, no UB).
- [ ] Assets committed with a LICENSE beside every dataset dir; `data/README.md`
      records sources, URLs, licenses, and SHA256 (matches SPEC §7).
- [ ] Documentation map complete: README, docs/core.md, docs/io-data.md,
      docs/volume.md, docs/render.md, docs/samples.md, docs/mpr.md,
      data/README.md.
- [ ] All five capability samples plus the MPR sample run under WSLg/Xvfb and
      exit cleanly.