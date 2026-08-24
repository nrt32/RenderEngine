# app/ — sample harness & samples

`app/` is the **compositions + samples module** (SPEC §3): it owns the shared
sample harness (visible window + ImGui overlay wiring + run loop), the sample
applications that drive each rendering capability, and (in later tasks) the
`SceneView`/`MPRView` compositions. This page documents the **T12 + T13
deliverable**: the `SampleHarness` component and the **full 5-capability sample
set** driven through it — mesh, plane, volume (T12) and slice, OIT (T13) — each
with per-sample driving instructions (FR-app.1, complete). It is part of the
`docs/samples.md` documentation map (T12/T13).

## Components

### `core::Window` (`core/window.hpp`, `.cpp`)

A **core/** component (the T12 samples need a *visible* window; the offscreen
fixture creates a hidden one): an RAII visible GLFW window with a GL 4.6 core
context. Like `utils::OffscreenContext` it owns the raw context-creation calls
on the interactive sample path, with the raw GL-loader anchor (`core::loadCoreGl`,
shared with `utils/`) under `core/` (guardrail `gpu_api_ownership`). The
context is made current on construction and GL entry points are loaded via
`core::loadCoreGl`, so the `core/` wrappers and the `render/` renderers work
unchanged.

- `Window::create(width, height, title)` — visible window + GL 4.6 core
  context; returns a typed error on GLFW/GL-load failure.
- `shouldClose()` / `pollEvents()` / `swapBuffers()` / `requestClose()` — the
  window-event primitives the harness run loop drives.
- `handle()` — the raw `GLFWwindow*` for the ImGui GLFW backend.
- `width()` / `height()` — the client-area pixel size (the framebuffer size the
  samples render into).
- Version probe via `glGetIntegerv` (GL_MAJOR_VERSION / GL_MINOR_VERSION),
  matching the offscreen fixture (SPEC §2/§8).

### `app::ISample` + `app::SampleHarness` (`app/sample_harness.hpp`, `.cpp`)

The **shared sample harness** (T12 deliverable): a reusable app/ component that
owns a visible window, wires the Dear ImGui (GLFW + OpenGL3) overlay, and runs
the frame loop. A sample implements `ISample` and the harness drives it:

| Member | Purpose |
|---|---|
| `ISample::renderFrame(width, height)` | render one frame of the sample's 3D scene into the window's **default framebuffer** (a `render::RenderTarget` with `framebuffer == nullptr`, see below). Returns a typed error on failure (SPEC §5). |
| `ISample::title()` | one-line description shown in the ImGui overlay. |
| `ISample::instructions()` | optional multi-line help text shown in the overlay (T13) describing how to drive the sample's capability; empty by default. |
| `SampleHarness::run(maxFrames)` | the run loop: poll events → ImGui new-frame → sample `renderFrame` → ImGui overlay → present. Stops cleanly after `maxFrames` frames or on window close; returns the process exit code (0 clean). |
| `app::sampleMaxFrames(default)` | reads the `RE_SAMPLE_MAX_FRAMES` env var (bounded run, so the gate can terminate samples headlessly). |

Per frame the harness:
1. `pollEvents()`, then starts the ImGui frame (`ImGui_ImplOpenGL3_NewFrame`,
   `ImGui_ImplGlfw_NewFrame`, `ImGui::NewFrame`);
2. calls `sample_->renderFrame(w, h)` into the window's default framebuffer —
   if it returns a typed error, the run aborts with exit code 1 (never silent);
3. draws the overlay (`title`, frame counter, and — when the sample provides
   them — the per-capability driving instructions from `ISample::instructions()`),
   renders ImGui (`ImGui_ImplOpenGL3_RenderDrawData`), and swaps buffers.

The ImGui OpenGL3 backend uses its own self-contained imgl3w loader (no glad
wiring); it resolves GL entry points lazily against the current context, which
`core::Window` already made current. ImGui is compiled once into `re_imgui`
(core + demo + GLFW/OpenGL3 backends) as **third-party** code — it is not built
with the project's warnings-as-errors (SPEC §5 applies `-Werror` only to
RenderEngine's own targets).

### Default-framebuffer rendering (null `RenderTarget::framebuffer`)

The samples render into the window's on-screen default framebuffer, not an
offscreen FBO. `MeshRenderer`, `PlaneRenderer`, and `VolumeRenderer` now treat a
**null** `RenderTarget::framebuffer` as "bind the default framebuffer"
(`core::bindDefaultFramebuffer`, i.e. `glBindFramebuffer(GL_FRAMEBUFFER, 0)`) —
added in T12 for the interactive sample path. **T13 extends this to the two
remaining renderers used by the new slice/OIT samples**: `SliceRenderer::render`
and `LinkedListOIT::end` (the composite pass) also treat a null framebuffer as
the default, so the slice and OIT samples can render on-screen. Offscreen FBO
targets (the T7–T11 gate paths) are unchanged; a zero-width/height target is
still rejected with a typed error.

## Samples (T12 + T13)

Each sample is a small executable (`app/re_sample_*`) that loads its data,
builds a scene, and hands an `ISample` to a `SampleHarness`. All five exit
cleanly (code 0) after `RE_SAMPLE_MAX_FRAMES` frames (default 300) so the gate
can run them under Xvfb within a timeout (FR-app.1). Build them with
`RE_BUILD_SAMPLES=ON` (default; also forced on whenever `RE_BUILD_TESTS` is on,
because the T12/T13 gate spawns them).

| Sample | Executable | Demonstrates | Data |
|---|---|---|---|
| Mesh | `re_sample_mesh` | opaque shaded mesh (Phong) | `data/meshes/bunny.obj` (SPEC §7) |
| Plane | `re_sample_plane` | GPU-extracted volume planes (axial + oblique, FR-render.5 extension) | `data/volumes/sample_ct.nrrd` + CT window/level transfer function |
| Volume | `re_sample_volume` | ray-cast volume (front-to-back compositing) | `data/volumes/sample_ct.nrrd` + CT window/level transfer function |
| Slice | `re_sample_slice` | geometry-shader plane clip of a mesh | `data/meshes/teapot.obj` (SPEC §7), clipped by a horizontal midplane |
| OIT | `re_sample_oit` | order-independent transparency (linked-list) | three overlapping transparent quads (in code, deterministic) |

The mesh sample frames the bunny with a perspective camera computed from its
AABB (eye pulled back along +Z by `radius / tan(fov/2)`); the plane sample
loads the CT volume and shows two GPU-extracted planes side by side through
`render::VolumeSliceRenderer` — left, the axial plane at the middle voxel
layer in the shared MPR display frame; right, the oblique diagonal plane
`x + z = 1` through the cube center viewed along its normal — replacing the
former procedural gradient quad (a plane in this engine means a slice
extracted from volume data, so the sample demonstrates exactly that, with no
CPU slicing anywhere); the volume sample
renders the CT chest with a deterministic transfer function (air transparent,
soft tissue opaque/bright). The slice sample loads the teapot and clips it by a
horizontal plane at its vertical midpoint (`y = 0.5*(min.y + max.y)`, kept side
`y >= midpoint`) through `render::SliceRenderer` — the geometry shader keeps the
upper half and emits the on-plane cross-section (slicing is geometry, not
compositing, SPEC §3). The OIT sample stacks three overlapping transparent quads
(red near, green middle, blue far) through `render::MeshRenderer` with an
injected `render::LinkedListOIT` pipeline, so the center region shows all three
blended in depth order regardless of draw order (FR-render.2/3).

### Driving each capability (per-sample instructions)

Every sample's ImGui overlay shows a short "How to drive this capability" help
block (from `ISample::instructions()`, T13). Each sample is static in v1 (one
window, one GL context, one render thread, SPEC §5 — no camera controls), so
"driving" the capability means running the sample, observing the rendered
result, and exiting cleanly:

| Sample | How to drive it |
|---|---|
| `re_sample_mesh` | observe the shaded bunny (opaque Phong, FR-render.1); close the window to exit. |
| `re_sample_plane` | observe the two GPU-extracted CT planes (axial left, oblique right; FR-render.5 extension); close the window to exit. |
| `re_sample_volume` | observe the ray-cast CT chest (FR-render.6); close the window to exit. |
| `re_sample_slice` | observe the teapot cut open along its horizontal midplane with the on-plane cross-section (FR-render.4); close the window to exit. |
| `re_sample_oit` | observe the three overlapping transparent quads blended in depth order (FR-render.2/3); close the window to exit. |

The overlay instructions text is the authoritative per-sample help for each
capability; the table above summarizes it.

### Running the samples

```sh
source tools/env.sh && cmake --build build -j
# Interactive (WSLg display):
./build/app/re_sample_mesh
./build/app/re_sample_plane
./build/app/re_sample_volume
./build/app/re_sample_slice
./build/app/re_sample_oit
# Headless (Xvfb), bounded run for automation:
RE_SAMPLE_MAX_FRAMES=30 xvfb-run -a ./build/app/re_sample_mesh
```

Environment variables:

| Variable | Meaning |
|---|---|
| `RE_SAMPLE_MAX_FRAMES` | number of frames before the sample exits cleanly (default 300). |

## Acceptance constants (FR-app.1, docs/samples.md)

The T12/T13 gate spawns each of the five samples with
`timeout 120 env RE_SAMPLE_MAX_FRAMES=20 ASAN_OPTIONS=detect_leaks=0
GALLIUM_DRIVER=llvmpipe MESA_GL_VERSION_OVERRIDE=4.6 xvfb-run -a <bin>`
(SPEC §8: samples run under WSLg when present, otherwise under Xvfb) and
asserts:

| Quantity | Value | Where it comes from |
|---|---|---|
| Sample exit code | `0` | the harness returns 0 only after `RE_SAMPLE_MAX_FRAMES` frames all rendered without error and ImGui shut down cleanly; any frame failure → 1, any hang → 124 (`timeout`), any ASan/UBSan abort → signal (FR-app.1 "exit code 0") |
| Window-opened marker in log | contains `GL 4.6 core` | `core::Window::create` logs `window: 800x600 GL 4.6 core` only after `glfwCreateWindow` + `gladLoadGL` + the `glGetIntegerv` 4.6 probe succeed (FR-app.1 "opens a window", SPEC §2/§8) |
| Sanitizer signatures in log | none of `AddressSanitizer`, `UndefinedBehaviorSanitizer`, `runtime error:`, `LeakSanitizer` | FR-app.1 "no sanitizer reports"; address/UB detection stays active in the subprocess (the leak gate remains the unit-test suite on llvmpipe, SPEC §8 — the Xvfb windowing stack's fontconfig/pango allocations are third-party driver noise) |
| Per-sample frame count | `20` | `RE_SAMPLE_MAX_FRAMES=20`: bounded run proving the loop iterated; exit 0 implies all 20 frames rendered |

## Guardrails observed

- **GL ownership**: raw `glXxx` calls live only under `core/` (`core/window.cpp`
  owns context creation + glad loading; `core/draw.cpp` owns
  `glBindFramebuffer(GL_FRAMEBUFFER, 0)` via `core::bindDefaultFramebuffer`).
  `app/` renders exclusively through `core/` wrappers and the `render/`
  renderers; ImGui's backends are third-party code compiled into `re_imgui`.
- **Typed diagnostics**: sample load/window/frame failures surface as typed
  errors (SPEC §5) → the sample exits non-zero; never silent.
- **Deterministic / single-threaded**: one window, one GL context, one render
  thread (SPEC §5); the plane sample's two extraction planes are fixed
  (middle axial layer + the diagonal `x + z = 1` cut of the committed CT), so
  the sample is reproducible frame for frame.
- **Logging**: spdlog only (`core::Window` logs the window creation; harness
  errors use spdlog).
- **Doxygen** on all public API (SPEC §5).