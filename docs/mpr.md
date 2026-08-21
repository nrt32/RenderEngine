# app/ — MPR view (layout + slice views)

`app/` is the **compositions + samples module** (SPEC §3). MPR is **app-level
composition, not a module**: the Multi-Planar Reconstruction (MPR) sample
composes the existing renderers (PlaneRenderer for the slice views, later
MeshRenderer + SliceRenderer for the contour/3D view) into a single window.
This page documents the **T14 deliverable** (FR-app.2): the **1280×960 window
with a 2×2 viewport grid** and the **T/C/S slice views** that sample the volume
along the pinned axis convention, plus the **shared slice-state/camera
scaffolding** for the 2D views. It is part of the `docs/mpr.md` documentation
map (T14/T15).

## The MPR layout (FR-app.2)

One **1280×960** window is split into a **2×2 grid of four 640×480 viewports**
(SPEC §4 FR-app.2):

| Position | View | Axis it samples along |
|---|---|---|
| **T** top-left | Transverse | slice at constant **Z** (axial) |
| **C** top-right | Coronal | slice at constant **Y** |
| **S** bottom-left | Sagittal | slice at constant **X** |
| **3D** bottom-right | 3D view | (reserved; mesh + contour arrive in T15) |

`app::mprViewports(windowWidth, windowHeight)` computes the four rectangles
(GL pixel coordinates, y up from the bottom scanline, matching
`core::setViewport`). For the SPEC window it returns exactly
`{0,480,640,480}`, `{640,480,640,480}`, `{0,0,640,480}`, `{640,0,640,480}` —
the pinned grid positions the T14 gate asserts (FR-app.2(1)).

## The axis sampling convention (FR-app.2)

The three 2D views sample the volume along their pinned axis
(SPEC §4 FR-app.2):

- **Transverse** = slice at constant **Z** (axial): the slice image is a
  rectangle over **(X, Y)** at the chosen Z index;
- **Coronal** = slice at constant **Y**: the slice image is a rectangle over
  **(X, Z)** at the chosen Y index;
- **Sagittal** = slice at constant **X**: the slice image is a rectangle over
  **(Y, Z)** at the chosen X index.

## Components

### `app::MprAxis`, `app::MprViewport`, `app::MprSliceState`
(`app/mpr_slice.hpp`)

The pure scaffolding types:

- `MprAxis {Transverse, Coronal, Sagittal}` — enumerates the three orthogonal
  views and encodes which axis each samples along.
- `MprViewport {x, y, width, height}` — a GL pixel rectangle.
- `MprSliceState {transverseZ, coronalY, sagittalX}` — which voxel-index plane
  each 2D view is on (the "slice state"). v1 holds each view on a fixed
  constructor-chosen index (the middle slice of its axis); T15 adds camera
  control that drives these.

### `app::mprViewports(int w, int h)` (`app/mpr_slice.cpp`)

Returns the four `MprViewport`s in grid order T, C, S, 3D. Pure math, no GL —
the T14 gate asserts the SPEC constants directly.

### `app::makeSliceImage(dataset, tf, axis, index)` (`app/mpr_slice.cpp`)

Builds a 2D RGBA slice `data::Image` of `dataset` through `tf`, sampling along
`axis` at voxel-index `index` per the convention above. Each voxel is mapped
through the transfer function (FR-vol.1) to a straight RGBA color and stored as
RGBA8 bytes (round(v·255+0.5)) in a top-left-origin image. The returned
image's `width()`/`height()` and per-pixel bytes are the **explainable
acceptance values** the T14 gate asserts for the per-view pixel checks
(FR-app.2(2)). This is the shared slice-state/camera scaffolding the 2D views
drive.

### `app::MPRView : app::ISample` + `main()` (`app/mpr_sample.cpp`)

The MPR sample. On construction it loads the CT volume
(`data/volumes/sample_ct.nrrd`, SPEC §7), builds the three slice images at the
initial slice state (middle slice per axis), and configures the shared 2D-view
camera scaffolding (one unit quad + one `render::PlaneRenderer`, plus the
per-view orthographic camera derived from each slice image). Per frame it:

1. renders each of the three slice views into its own **640×480 offscreen FBO**
   via `render::PlaneRenderer`: for view *i* the ortho maps that view's slice
   image pixel space `[0,imgW]×[0,imgH]` onto the full viewport and the shared
   unit quad is scaled onto that pixel rectangle (`makeSliceCamera` /
   `makeSliceModel`), so the whole slice fills the view; the 3D view FBO is
   cleared to a background (T15 fills it with the mesh);
2. presents the four FBOs onto the window's default framebuffer in their
   viewport regions via a small textured-quad pass built from `core/` wrappers
   (a GLSL 450 fullscreen-quad shader + one shared unit-quad VAO/VBO/EBO).

The sample exits cleanly (code 0) after `RE_SAMPLE_MAX_FRAMES` frames (default
300) so the gate can run it headlessly under Xvfb within a timeout (FR-app.1).

## Running the MPR sample

```sh
source tools/env.sh && cmake --build build -j
# Interactive (WSLg display):
./build/app/re_sample_mpr
# Headless (Xvfb), bounded run for automation:
RE_SAMPLE_MAX_FRAMES=30 xvfb-run -a ./build/app/re_sample_mpr
```

The `RE_SAMPLE_MAX_FRAMES` env var bounds the run loop (default 300), matching
the other samples (docs/samples.md).

## Acceptance constants (FR-app.2, docs/mpr.md)

| Quantity | Value | Where it comes from |
|---|---|---|
| Window size | `1280×960` | SPEC §4 FR-app.2 |
| Each viewport | `640×480` | SPEC §4 FR-app.2 (`window/2 × window/2`) |
| Grid positions | T `{0,480}`, C `{640,480}`, S `{0,0}`, 3D `{640,0}` | the four equal quadrants, y up from the bottom scanline |
| Transverse axis | constant Z | SPEC §4 FR-app.2 (axial) |
| Coronal axis | constant Y | SPEC §4 FR-app.2 |
| Sagittal axis | constant X | SPEC §4 FR-app.2 |
| Slice-image pixel | `round(tf.sample(voxel)·255 + 0.5)` per channel | FR-vol.1 exact at control points; the T14 gate uses a synthetic volume + TF so each pixel's red byte equals the underlying voxel value |
| Sample exit code | `0` | the harness returns 0 only after all frames rendered cleanly; any frame error → 1, any hang → 124, any ASan/UBSan abort → signal (FR-app.1) |

The T14 gate asserts the layout constants (1) and, for each of T/C/S, that the
slice image's pixel bytes read the volume along the pinned axis (2), plus the
MPR sample smoke run (gate G, N≥3). The pure layout + slice tests are CPU-only;
the smoke test spawns the sample subprocess.

## Guardrails observed

- **GL ownership**: raw `glXxx` calls live only under `core/`. The MPR sample
  renders through `render::PlaneRenderer` and `core/` wrappers only
  (guardrail `gpu_api_ownership`); `app/mpr_slice.*` is GL-free.
- **Typed diagnostics**: frame/GL failures surface as typed `data::Result`
  errors (SPEC §5); never silent.
- **Deterministic / single-threaded**: one window, one GL context, one render
  thread (SPEC §5). Slice images are built from a fixed slice state (the
  deterministic CT + a fixed transfer function).
- **Logging**: spdlog only.
- **Doxygen** on all public API (SPEC §5).
