# SPEC — RenderEngine

> Drafted collaboratively via `/loop-elicitation`. Decisions recorded as they
> land; this file is the source of truth for the loop.

## 1. Goals & non-goals

### Product
A C++ real-time rendering engine built on **OpenGL (GL 4.6 core)** with **CMake**
as the build system, developed and run on **Ubuntu inside WSL on Windows**.
Version 1 is a capability-focused engine that renders meshes, volumes (basic
ray casting), planes, mesh slices, and Order-Independent-Transparency (OIT)
compositing, with a Multi-Planar Reconstruction (MPR) sample that shows
Transverse/Coronal/Sagittal views plus a 3D view in one window.

Each capability ships as:
- an engine-side implementation module, and
- one sample application demonstrating how to drive it.

### Core capabilities (v1)
1. **Mesh rendering** — shaded triangle meshes.
2. **Volume rendering** — basic ray casting (front-to-back compositing) of a
   volumetric dataset.
3. **Plane rendering** — textured/shaded planes.
4. **Mesh slice rendering** — a planar slice through a mesh (cross-section).
5. **Transparency / OIT** — correct order-independent transparency compositing.
6. **MPR view** — a single window with 3 orthogonal slice views (Transverse,
   Coronal, Sagittal) and a 3D rendering view.

### Materials
- **Phong** material model for v1, integrated through a **modular material
  interface** designed so additional models (PBR, toon, …) can be added without
  touching the renderer core (SOLID: open/closed, dependency inversion).

### GUI
- Lightweight **Dear ImGui** (immediate-mode) with GLFW/OpenGL3 backend.
- **GLFW + glad2** windowing and GL function loading.

### Success criteria
1. All sample applications run and display the expected capability.
2. All unit tests pass with strong, explainable assertions (no tolerance-abuse).
3. No memory leaks — test binaries build with **ASan+UBSan** and run clean.

### Non-goals (v1)
- **PBR / advanced materials** — Phong only; the interface must allow later models.
- **HDR / post-processing pipeline** — no bloom, tonemapping, SSAO, shadows.
- **Asset import formats** — a single bundled/OBJ-style loader only; no glTF/fbx.
- **Scene graph / transform hierarchy** — per-object transforms only (model
  matrix); no parent/child trees. **Skeletal animation is out of scope.**
- **Out-of-core / streaming** — all data loaded fully into memory.
- **Multi-window / headless rendering** — single window, single GL context,
  always GUI-attached.

## 2. Tech-stack decisions

| Decision | Choice | Rationale |
|---|---|---|
| Language | **C++20** | Modern, broadly supported; GCC 12+ on Ubuntu/WSL with no extra toolchain setup |
| Compiler | GCC (Ubuntu default toolchain) | Standard on Ubuntu/WSL; selected at setup time |
| Build system | **CMake (>= 3.24)** | Runner's build+test gate supports CMake natively |
| GPU API | **OpenGL 4.6 core** (GLSL 460) | The WSL Mesa D3D12 driver exposes GL 4.6 core natively; provides VAOs, FBOs, modern shaders for OIT + ray casting |
| GL loader | **glad2 v2.0.8** (GL 4.6 core generator) | Generated at configure time via FetchContent; pinned release tag (commit 73db193) |
| Windowing | **GLFW 3.4** | Standard; WSLg displays GLFW windows natively |
| Math | **GLM 1.0.1** | De-facto GLSL-compatible math lib; header-only |
| GUI | **Dear ImGui v1.92.9** | Immediate-mode, tiny footprint, OpenGL3 backend, ideal for MPR viewport + panels |
| Unit tests | **GoogleTest v1.15.x** | Strong enforcement-style assertions (user requirement) |
| Logging | **spdlog v1.14.1** | Lightweight OSS logging: trace/debug/info/warn/error/fatal; console/file sinks |
| Textures | **stb_image** (single-header, pinned commit) | Public domain; loads textures and can write outputs |
| Dependency acquisition | **CMake FetchContent, pinned GIT_TAG** | Self-contained, reproducible; feeds dependency-lock guardrail |
| Test binary | **ASan + UBSan** enabled | Memory-leak + UB detection in the gate |
| GL-touching tests | **Offscreen GL context** (hidden GLFW window; EGL-surfaceless fallback) | Unit tests exercise real GL paths headless, under sanitizers |

### Platform / environment notes
- Host: **Ubuntu inside WSL on Windows**; display via **WSLg** (Windows 11).
- Samples require a display (WSLg); tests run **headless** (offscreen GL context).
- Environment (packages, toolchain, env vars) enumerated in §8 and executed by the SETUP phase.

## 3. Module blueprint

Layer-first, not capability-first. Everything drawable is an `IRenderer`;
views compose renderers. **MPR is app-level composition, not a module.**

```
io/          loaders ONLY, no GL        (mesh/ volume/ image/)
data/        CPU containers, no GL      (Mesh, VolumeDataset, Image, TransferFunction)
             + GL-free typed Result<T,E> (data/result.hpp, shared by all layers)
volume/      pure math: dataset sampling/interp, transfer function,
             ray-cast compositing math  <- NO GL, headless-testable
core/        GL foundation: GLFW context, RAII GL objects, ShaderProgram,
             thin core::Draw API, offscreen GL test fixture  <- SOLE owner of raw GL calls
render/      ONE class per rendering technique, unified IRenderer interface
             |- IMaterial + PhongMaterial      (transparency = material property)
             |- ITransparencyPipeline + LinkedListOIT  (swappable OIT impl)
             |- MeshRenderer   (opaque forward pass + AUTO-engaged OIT for
             |                  transparent materials; single mesh entry point)
             |- SliceRenderer  (mesh-family, GEOMETRY-SHADER plane clip; GPU-only;
             |                  reuses mesh geometry handling + materials; NO OIT in v1)
             |- PlaneRenderer  (textured quads/planes — feeds MPR)
             |- VolumeRenderer (ray-cast GL draw; volume/ provides the pure math)
app/         compositions + samples + ImGui overlay
             |- SceneView  (composes MeshRenderer + VolumeRenderer +
             |              optional Slice/Plane)
             |- MPRView    (composes 3x PlaneRenderer for T/C/S + 1x SceneView 3D)
tests/       headless unit tests (consume core/ wrappers + the core/ fixture)
```

### Design principles (SOLID)
- **OIT is a characteristic, not a peer renderer.** Transparency lives on
  `IMaterial`; `MeshRenderer` auto-engages the injected `ITransparencyPipeline`
  (v1: per-pixel linked list, capture → depth-sort → composite) when any mesh's
  material is transparent. The pipeline interface is swappable (open/closed,
  dependency inversion) so future OIT variants need no renderer changes.
- **Slicing is geometry, not compositing.** `SliceRenderer` is a mesh-family
  technique using a geometry shader to clip against a plane (pure GPU). It
  shares mesh geometry handling and the material system but does **not** use
  OIT in v1.
- **Stateless renderers.** `IRenderer::render(scene, camera, target)` receives
  its data per call; renderers own only GL resources. One mesh can be drawn by
  both SceneView and MPR views without duplication. Data lives in `data/` +
  app-level scene structs.
- **Dependency inversion:** renderers depend on `IMaterial` /
  `ITransparencyPipeline` abstractions, never concrete material/OIT classes.
- **GL ownership:** raw `glXxx(...)` calls appear ONLY under `core/` (RAII GL
  objects + thin `core::Draw` API). `render/` draw passes, `app/`, and `tests/`
  use `core/` wrappers. `io/`, `data/`, `volume/` are GL-free. This makes the
  GL-ownership audit rule mechanically enforceable (single-dir anchor).

## 4. Functional requirements

Every FR is testable by an **explainable acceptance constant** — an analytic
value, a spec-derived number, or a golden-corpus hit. No golden-image
regression diffing in v1 (deferred to a future version); every assertion is
analytic/explainable. Tolerances: color pixels within **1/255**, pure math
within **1e-6**, plane-geometry within **ε (1e-4 relative)**.

### io/ (loaders — no GL)
- **FR-io.1** OBJ-style loader loads a known bundled mesh. *Acceptance: vertex
  count, index count, and computed AABB match hand-counted values of the golden
  file.*
- **FR-io.2** NRRD volume loader loads a known volume. *Acceptance: dimensions
  and per-voxel values at indexed corners match the golden file.*
- **FR-io.3** Image loader (stb) loads known images. *Acceptance: dimensions +
  pixel values at corners/center match known fixtures.*
- **FR-io.4** Loaders reject malformed input with a typed error, leaving no
  partial state. *Acceptance: error enum set, no exception escape, container
  unchanged.*

### data/ (containers, no GL)
- **FR-data.1** Mesh face normal computed analytically. *Acceptance: equals the
  closed-form cross-product normal of a known triangle.*
- **FR-data.2** Mesh AABB. *Acceptance: exact bounds of a golden mesh.*
- **FR-data.3** VolumeDataset trilinear sampling. *Acceptance: interior sample
  equals the closed-form interpolant of the 8 corner values within 1e-6.*

### volume/ (pure math)
- **FR-vol.1** Transfer function: control points → RGBA. *Acceptance: exact at
  control points; linear ramp between them within 1e-6.*
- **FR-vol.2** Ray-cast compositing (front-to-back). *Acceptance: for a known
  (color, alpha) sample sequence, accumulated output matches the closed-form
  alpha-blend result within 1e-6.*
- **FR-vol.3** Ray/AABB sampling along a ray. *Acceptance: analytic step
  positions for given AABB + ray.*

### core/ (GL foundation)
- **FR-core.1** RAII GL objects: create → bind → destroy, no errors/leaks.
  *Acceptance: no GL errors under the offscreen context; ASan/UBSan clean.*
- **FR-core.2** ShaderProgram compile/link with typed diagnostics. *Acceptance:
  a valid shader compiles/links and reports no error; an intentionally-malformed
  shader whose source contains the known-bad token `glibberish` on line 7
  returns a typed error string containing that token and the offending line
  (`ERROR: 0:7` — the GLSL diagnostic prefix + line are the golden substring),
  with no crash.*

### render/
- **FR-render.1** MeshRenderer renders a known mesh to an offscreen target.
  *Acceptance: center pixel color of a known solid-color mesh matches the
  expected value within 1/255.*
- **FR-render.2** OIT linked-list: fragments captured and depth-sorted.
  *Acceptance: two overlapping quads at known depths → composited color matches
  the analytic depth-ordered blend within 1/255.*
- **FR-render.3** OIT auto-engages only when a transparent material is present.
  *Acceptance: an opaque-only scene produces output whose sampled pixels all
  have alpha == 1.0 (no transparency engaged); adding one transparent quad
  flips the pipeline on (observable via injectable spy).*
- **FR-render.4** SliceRenderer (geometry shader) plane ∩ mesh. *Acceptance:
  emitted cross-section vertices lie on the plane (distance ≤ ε).*
- **FR-render.5** PlaneRenderer textured quad. *Acceptance: corner/center pixel
  matches texture sample within 1/255.*
- **FR-render.6** VolumeRenderer ray-casts a tiny synthetic volume. *Acceptance:
  center pixel matches analytic ray-cast of that volume within 1/255.*

### app/
- **FR-app.1** Each capability sample runs, opens a window, and exits cleanly.
  *Acceptance: exit code 0, no sanitizer reports (smoke run with timeout under
  WSLg/Xvfb).*
- **FR-app.2** MPR window shows 4 viewports (T/C/S + 3D) in a 2×2 grid: window
  **1280×960**, each viewport **640×480**, with T top-left, C top-right, S
  bottom-left, 3D bottom-right. *Acceptance: viewport dims equal these
  constants (window 1280×960; four 640×480 viewports at the pinned grid
  positions); each slice view samples the volume along its axis per the pinned
  convention (pixel check per view).* **Axis convention:** Transverse = slice
  at constant **Z** (axial), Coronal = constant **Y**, Sagittal = constant **X**.
- **FR-app.3** MPR 2D views render **slice + contour**: each of the three
  orthogonal views shows the volume slice of that plane **and** the mesh
  cross-section contour (plane ∩ mesh outline) overlaid on it; the 3D view
  shows the mesh. *Acceptance: for the golden box mesh, **≥ 90% of pixels
  within 2 px (Euclidean) of the analytic plane∩mesh intersection curve match
  the contour color** — the curve is computed in closed form from the box+plane
  for each slice view's plane; the 3D view draws the mesh.*

## 5. Non-functional requirements

### Generic (always kept)
- **Build hygiene** — warnings-as-errors in the gate; no warning-suppression
  pragmas/flags.
- **Determinism** — reproducible builds; deterministic test ordering.
- **Documentation** — Doxygen comments on all public API (required by user).
- **Portability** — builds and runs on Ubuntu/WSL target; no Windows-only code
  paths (cross-platform CI out of scope).
- **Memory/sanitizers** — ASan + UBSan on test binaries (already in gate).

### Product-specific (adopted)
- **Deterministic rendering** — same scene + camera → same frame output;
  required by the analytic pixel checks.
- **Single-threaded** — one render thread, no concurrency in v1. Documented so
  no premature mutex/threading is added.
- **Memory budget caps on sample data** — sample scenes capped; the committed
  sample volume is downsampled to ≤ 128³ (§7) to stay within sane GPU/RAM on WSL.
- **Typed error reporting** — runtime failures (load, GL, shader) surface as
  typed, actionable diagnostics, never silent.
- **Logging** — **spdlog** (pinned) provides trace/debug/info/warn/error/fatal;
  no custom logging framework. Logging-discipline guardrail still applies: no
  raw `printf`/`std::cout` for diagnostics.
- **Profiling (macro-gated)** — in `core/`: scoped profiler macros compiled out
  unless enabled; can measure FPS, data-load time, data-transfer (upload) time,
  draw-call time.
- **Soft performance floor: NOT adopted for v1** (no automated FPS gate;
  interactivity is a manual sample check).

## 6. Guardrails / rules (hard, enforced by tools/audit.rules + AGENTS.md)

- **Dependency lock** — every third-party dep pinned via FetchContent `GIT_TAG`
  (GLFW 3.4, glad2 v2.0.8, GLM 1.0.1, ImGui v1.92.9, GoogleTest v1.15.x,
  spdlog v1.14.1, stb pinned commit). A `GIT_TAG` must be a **release tag or
  commit SHA, never a branch name** (the reviewer verifies this). No unpinned
  fetches, no vendored binaries. *Audit: `deps_pinned`.*
- **GL ownership** — raw `glXxx(...)` calls only under `core/` (RAII objects +
  thin `core::Draw` API); `render/`, `app/`, `tests/` use `core/` wrappers;
  never in `io/`, `data/`, `volume/`. *Audit: `gpu_api_ownership`.*
- **Forbidden patterns** — legacy fixed-function GL anywhere
  (`no_legacy_api`); hard-coded secrets anywhere (`no_secrets`); readback raw
  calls only under `core/` (test-consumed) (`no_production_readback`); no raw
  `printf`/`std::cout` for diagnostics —
  use spdlog (`no_raw_diagnostics`).
- **Evidence rule + regression lock** (always keep, generic) — every gate
  produces verifiable evidence; a failing gate blocks the task; established
  behaviors must not regress without explicit approval.
- **Asset/data licensing** — only clearly-licensed free data (CC0 / CC-BY /
  CC-BY-SA / public domain) for meshes and volumes; a LICENSE file committed
  beside every dataset; no unlicensed redistribution. *Audit: `assets_licensed`
  (grep is only a floor — the T2 gate enforces one LICENSE per dataset dir).*
- **Build hygiene** — warnings-as-errors, no warning-suppression flags/pragmas
  (generic built-in checks).

## 7. Data & asset plan

**Policy:** assets are committed **in-repo** under `data/` and reused by tests.
They must be small and clearly licensed; a `LICENSE` file sits beside every
external dataset (audit rule `assets_licensed`). Tests additionally use
procedural in-code geometry/volumes for determinism (no external dependency in
the test gate).

**Volume format:** io/volume loads **NRRD** (text header + raw, **uncompressed**
voxel block). The setup-time converter (`tools/convert_nrrd.py`, Python 3
stdlib only) downsamples the pinned CT source to ≤128³ and re-writes it as a
small raw NRRD for commit.

| Asset | Source (pinned URL) | License | Target path | Notes |
|---|---|---|---|---|
| Stanford bunny (OBJ) | `https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/stanford-bunny.obj` (SHA256 `1eb35d1e21ce99e5ce911353b6be278990713448dd9e8f5c9387f9de39b32205`) | Public domain | `data/meshes/bunny.obj` | sample mesh rendering; 2.4 MB |
| Utah teapot (OBJ) | `https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/teapot.obj` (SHA256 `1b5396fedd74b577e32cef41146582c2f2e1a050d5b4915193c0ac1ad4187ed4`) | Public domain | `data/meshes/teapot.obj` | sample mesh/slice rendering |
| CT chest sample volume | `https://github.com/Slicer/SlicerTestingData/releases/download/SHA256/4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e` (published SHA256 `4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e`, file `CT-chest.nrrd`) | CC-BY-SA 4.0 (Medical Decathlon) | `data/volumes/sample_ct.nrrd` | downsample ≤128³ → raw NRRD; used by volume/MPR samples + tests |
| Golden fixtures | Hand-authored small meshes/volumes/images | Project-owned | `data/fixtures/` | committed, tiny, used by io/data tests (hand-counted acceptance constants) |
| Procedural geometry | Generated in code at runtime | n/a | n/a | deterministic tests; no file dependency |

**Fetch method (two-phase: SETUP stages, T2 commits):** because assets are
committed, setup does NOT download into the repo at build time. The setup phase
(`/loop-setup`) downloads the pinned source files above, verifies SHA256, runs
`tools/convert_nrrd.py` (downsample the CT to ≤128³ and re-write as raw NRRD),
and stages the results under `data/` — but does **not** commit. Committing the
assets, LICENSE files, `data/README.md` (sources, URLs, licenses, checksums),
and recording the verified SHA256s in this section is the **T2 implementer's**
deliverable. Re-running setup is therefore idempotent and never touches git
state.

### Meshes (sample OBJs)
- `data/meshes/bunny.obj` — Stanford bunny, public domain.
- `data/meshes/teapot.obj` — Utah teapot, public domain.
- Both small enough to commit; used by the mesh + slice samples.

### Volumes
- `data/volumes/sample_ct.nrrd` — a small freely-licensed CT sample,
  downsampled to ≤128³ at setup (memory budget cap per §5), committed as NRRD.
- Tests use procedural synthetic volumes (analytic voxel fields) so expected
  values are closed-form.

### Fixtures
- `data/fixtures/` — hand-authored golden meshes/volumes/images with
  hand-counted acceptance constants (FR-io.1/2/3, FR-data.2).

## 8. Environment requirements

### System packages (not in repo, provisioned by /loop-setup)
- GCC/G++ (>= 12, for C++20), `cmake` (>= 3.24), `ninja` (optional), `git`,
  `clang-format` (T1 ships the config; NAMING_CONVENTIONS §7 enforces it).
- GL dev headers: `libgl1-mesa-dev`, `libegl1-mesa-dev`, `libx11-dev`,
  `libxrandr-dev`, `libxcursor-dev`, `libxi-dev`, `libxinerama-dev`
  (GLFW build deps), plus mesa GL drivers for WSLg (`mesa-utils` for
  `glxinfo` verification).
- Display: WSLg on Windows 11 (native) for interactive samples. For headless
  test runs: no display needed (offscreen GL context). `xvfb` is a **REQUIRED**
  package — the sample smoke gates (T12/T13) run under WSLg when present,
  otherwise under `xvfb`.
- Build tools: `curl`/`wget` (asset fetch at setup), `python3` (conversion
  tooling for NRRD downsample at setup — `tools/convert_nrrd.py`, **stdlib
  only, no pip deps**), `unzip`.

### Toolchain
- Compiler: GCC 12+ (Ubuntu default on 22.04+).
- CMake >= 3.24 (FetchContent + GIT_TAG pinning).

### Environment variables
- `DISPLAY` (WSLg auto; X server fallback only if not W11).
- `LOOP_BUILD_TEST_CMD` — must be set to the CMake build+test command when
  launching the loop (runner needs it; default runner logic knows CMake, but
  set explicitly).
- `AUDIT_SOURCE_DIRS="io data volume core render app tests"` — required for
  audit ownership rules to see our non-default layout.

### GL/GPU notes
- WSLg exposes OpenGL via Mesa; target GL 4.6 core (the D3D12 gallium driver reports
  core 4.6 natively; llvmpipe caps at 4.5). Verify with `glxinfo -l`. The headless
  test env forces llvmpipe with `MESA_GL_VERSION_OVERRIDE=4.6` so the gate asserts
  the SPEC 4.6 target on a deterministic, leak-clean software driver.
- Tests create an offscreen GL context (hidden GLFW window; EGL-surfaceless
  fallback) so the gate never needs a display.