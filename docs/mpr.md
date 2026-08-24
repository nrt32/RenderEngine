# app/ — MPR view (layout + slice views + contour + 3D view)

`app/` is the **compositions + samples module** (SPEC §3). MPR is **app-level
composition, not a module**: the Multi-Planar Reconstruction (MPR) sample
composes the existing renderers (VolumeSliceRenderer for the GPU-extracted
slice planes, ContourRenderer for the GPU contour overlay, MeshRenderer for
the 3D view) into a single window. This page documents the **T14/T15
deliverables** (FR-app.2/3), their V3.8b (T11) contour revision, and the
plane-capability review revision that moved the T/C/S views onto the **GPU
extraction path** (`render::VolumeSliceRenderer`): each 2D view's plane is
sampled directly from the cached 3D texture at that view's clip plane, so
there is no frozen CPU slice image anywhere on the live path and scrolling is
interactive by construction (a slice-index change is a uniform/state change).
The CPU oracle `app::makeSliceImage` is retained only as the gate tests'
reference implementation. The page documents the **1280×960 window with a
2×2 viewport grid**, the **T/C/S slice views** along the pinned axis
convention, the **GPU mesh contour overlay** on each slice view
(plane∩mesh cross-section, FR-app.3), the **3D rendering view** (the golden
box mesh, FR-app.3), and the **slice-state ↔ views/camera interplay**
(extraction planes, contour planes AND the 3D camera all track one shared
slice state; the sample auto-scrolls it deterministically). It is part of the
`docs/mpr.md` documentation map (T14/T15 + T11(V3.8b) + plane-capability review).

## The MPR layout (FR-app.2)

One **1280×960** window is split into a **2×2 grid of four 640×480 viewports**
(SPEC §4 FR-app.2):

| Position | View | Content |
|---|---|---|
| **T** top-left | Transverse | plane at constant **Z** (axial), GPU-extracted + mesh contour |
| **C** top-right | Coronal | plane at constant **Y**, GPU-extracted + mesh contour |
| **S** bottom-left | Sagittal | plane at constant **X**, GPU-extracted + mesh contour |
| **3D** bottom-right | 3D view | the golden box mesh (FR-app.3) |

`app::mprViewports(windowWidth, windowHeight)` computes the four rectangles
(GL pixel coordinates, y up from the bottom scanline, matching
`core::setViewport`). For the SPEC window it returns exactly
`{0,480,640,480}`, `{640,480,640,480}`, `{0,0,640,480}`, `{640,0,640,480}` —
the pinned grid positions the T14 gate asserts (FR-app.2(1)).

## The axis sampling convention (FR-app.2, GPU-extracted)

The three 2D views show the volume along their pinned axis
(SPEC §4 FR-app.2):

- **Transverse** = plane at constant **Z** (axial): displayed over **(X, Y)**;
- **Coronal** = plane at constant **Y**: displayed over **(X, Z)**;
- **Sagittal** = plane at constant **X**: displayed over **(Y, Z)**.

Since the plane-capability review deliverable these views do NOT display CPU
rasterized images: each view renders a `render::VolumeSliceInstance` whose

- `model` is `app::sliceVolumeModel(dataset, axis)` — maps the dataset's
  model-space unit cube so voxel-center index *i* lands at display coordinate
  *i + 0.5* on every axis, then applies the per-view axis permutation
  (Transverse identity / Coronal swaps Y/Z / Sagittal `(x,y,z) → (y,z,x)`),
- `plane` is `{normal (0,0,1), point (0,0, heldIndex + 0.5)}` in that display
  frame (the centers of the sliced voxel layer, same convention as
  `app::slicePlane`),
- camera is `app::makeSliceCamera(freeW, freeH)` over the free-axis extents
  (`app::sliceFreeAxes`).

Pixel center `(px, py)` of a 1:1-sized target therefore back-projects to
continuous index `(px, py, heldIndex)` exactly, and the extracted bytes equal
the retained CPU oracle `app::makeSliceImage(dataset, tf, axis, index)`
within 1/255 across the whole frame — the gate pins this per axis on an
asymmetric 8×6×4 volume, so any axis permutation or orientation error fails.

**Display orientation:** display +y runs along the second free-axis index in
BOTH the extraction frame and the contour frame (they share one display space,
so slices and outlines stay pixel-glued); the oracle image's rows carry the
same index order, so readback row *py* corresponds to image row *py*
directly.

## The mesh contour overlay (FR-app.3, GPU since V3.8b)

Each of the three slice views overlays the **plane∩mesh cross-section contour**
of the golden box mesh: the box's intersection with that view's slice plane,
computed **ON THE GPU** by `render::ContourRenderer`'s geometry shader
(`contour.geom.glsl`) and drawn as a second ReView layer over the slice image
at the contour color `app::kContourColor` (pure red → RGBA8 `255,0,0,255`).
There is no CPU rasterization pass: the former `app::meshPlaneContour` /
`app::overlayContour` helpers were deleted with `app/mpr_contour.*` in
V3.8b (T11); see docs/render.md ("ContourRenderer") for the geometry-shader
outline and its screen-space thick-line quads.

**Coordinate convention** (shared with `app/mpr_slice`): the volume and the
mesh live in one **voxel-index coordinate space**; voxel `(x, y, z)` has its
center at `(x+0.5, y+0.5, z+0.5)`. A slice view's plane passes through the
centers of the sliced voxel layer (`app::slicePlane`: Transverse `z = zIndex +
0.5`, Coronal `y = yIndex + 0.5`, Sagittal `x = xIndex + 0.5`), and the slice
image's pixel `(px, py)` maps to coordinate `(px+0.5, py+0.5)` on the two free
axes of that view's plane.

- `app::slicePlane(axis, state)` — the plane of a slice view driven by the
  slice state.
- `app::makeBoxMesh(min, max)` — the golden box mesh: a **non-manifold quad
  shell** (6 faces × 4 vertices, 12 triangles) spanning `[min, max]` in
  voxel-index units. Each face owns its own vertices, so every face renders
  **flat** under the v1 +Z lighting (docs/render.md): the +Z face shades to
  exactly the material's base color, the other faces to black. Geometrically
  it is the same box as a manifold build (same 8 corners, same 12 triangles),
  so the plane∩mesh cross-sections are the closed-form rectangles of the box.
  The faces are emitted in painter's order for the 3D view (far faces first,
  the +Z face last), which makes the near +Z face overdraw the far faces at
  the viewport center (v1 FBOs are color-only, no depth test, SPEC §6).

The scene→render translation goes through **broker::ContourMapper**
(`IMapper<scene::ContourObject, render::ContourObject>`, SPEC §11): each view
carries an axis-permutation model (Transverse identity / Coronal swaps Y/Z /
Sagittal maps `(x,y,z)->(y,z,x)`) so every view shares one display frame, plus
the clip plane expressed in that display frame; the mapper registers the box
in the shared AssetRegistry and yields the RE-minimal
`render::ContourObject{AssetHandle, ClipPlane, color}`. The geometry shader
emits exactly **8 outline segments** for the golden box at any cutting plane
(the 4 side faces contribute 2 crossing triangles each), expanded to ±2 px
thick-line quads whose union is exactly the rectangle boundary.

The T15 gate asserts (FR-app.3(1)): for each slice view's plane, **≥ 90% of
the pixels within 2 px (Euclidean) of the analytic plane∩mesh intersection
curve match the contour color within 1/255**, where the analytic curve (the
closed-form rectangle from the box + plane) is computed independently in the
test and the GPU result is verified by framebuffer readback
(`utils::PixelReader`).

## The 3D view + slice-state camera interplay (FR-app.3)

The 3D viewport (bottom-right) renders the golden box mesh through
`render::MeshRenderer` with the `app::make3dCamera` camera:

- `app::make3dCamera(state, meshBounds, aspect)` — the 3D-view camera is
  **driven by the slice state**: it looks at the intersection point of the
  three slice planes — the **crosshair** `(sagittalX + 0.5, coronalY + 0.5,
  transverseZ + 0.5)` — from an eye at
  `crosshair + normalize(1,1,1) * distance` with
  `distance = 1.5 × |bounds.max - bounds.min|` (the box's bounding diagonal),
  using a 45° perspective projection with `near = distance/10`,
  `far = distance*10`. Changing the slice state moves the crosshair, so the 3D
  view refocuses on the slice planes' intersection — the slice-state ↔ 3D-view
  camera interplay (the same slice state also drives the contour planes of the
  2D views).

The T15 gate asserts (FR-app.3(2), "the 3D view draws the mesh"): with the
sample's box + slice state, the camera's center ray enters the box on the +Z
face at `(89, 89, 60)`, whose flat normal is exactly (0,0,1), so the center
pixel of the rendered 3D view is exactly the material's base color
`{51, 102, 204}` within 1/255; the camera math itself is asserted too (the eye
position, the crosshair projecting to the viewport center, and the (1,1,1)
forward direction).

## Components

### `app::MprAxis`, `app::MprViewport`, `app::MprSliceState`
(`app/mpr_slice.hpp`)

The pure scaffolding types:

- `MprAxis {Transverse, Coronal, Sagittal}` — enumerates the three orthogonal
  views and encodes which axis each samples along.
- `MprViewport {x, y, width, height}` — a GL pixel rectangle.
- `MprSliceState {transverseZ, coronalY, sagittalX}` — which voxel-index plane
  each 2D view is on (the "slice state"). The slice state drives both the
  slice views' contour planes and the 3D view camera's look-at target
  (make3dCamera, T15).

### `app::SlicePlane`, `app::kContourColor` (`app/mpr_slice.hpp`)

- `SlicePlane {axis, coordinate}` — the plane of one slice view: perpendicular
  to `axis` at `coordinate` (voxel-index units, through the voxel centers).
- `kContourColor` — the FR-app.3 contour stroke color, pure red
  (RGBA8 255,0,0,255); the GPU contour layer draws its opaque strokes in
  exactly this color.

### `broker::make3dCamera(crosshairCenter, meshBounds, aspect)` (`broker/slice_display.hpp`; formerly `app::make3dCamera` in the deleted `app/mpr_camera.hpp`)

The 3D-view camera helper (moved from the deleted `app/mpr_contour.*` in
V3.8b): returns the plain `render::Camera` struct (no GL calls) — see the
slice-state camera interplay section above.

### `app::mprViewports(int w, int h)` (`app/mpr_slice.cpp`)

Returns the four `MprViewport`s in grid order T, C, S, 3D. Pure math, no GL —
the T14 gate asserts the SPEC constants directly.

### `app::makeSliceImage(dataset, tf, axis, index)` (`app/mpr_slice.cpp`)

Builds a 2D RGBA slice `data::Image` of `dataset` through `tf`, sampling along
`axis` at voxel-index `index` per the convention above. Each voxel is mapped
through the transfer function (FR-vol.1) to a straight RGBA color and stored as
RGBA8 bytes in a top-left-origin image. Since the GPU-extraction revision this
function is the **CPU reference implementation only** — the gate tests compare
GPU-extracted planes against it whole-frame; no sample renders through it
anymore (mechanically enforced by the extraction gate's comment-stripped grep:
zero call sites in app samples).

### `app::sliceFreeAxes(dataset, axis)`, `app::sliceVolumeModel(dataset, axis)` (`app/mpr_slice.*`)

The shared display-frame scaffolding for GPU slice views (pure glm math,
GL-free, headless-testable): `sliceFreeAxes` returns the two free-axis voxel
counts in display order — Transverse `(sizeX, sizeY)`, Coronal `(sizeX,
sizeZ)`, Sagittal `(sizeY, sizeZ)` — and `sliceVolumeModel` returns the model
matrix placing the dataset's unit cube into that view's display frame
(scale by `max(dim−1, 1)`, translate by 0.5 so voxel-center index *i* lands at
display *i + 0.5*, then the per-view axis permutation identical to the contour
overlay's). The gate tests drive these exact functions against the same
`makeSliceCamera(freeW, freeH)` camera the sample composes, so a
sample-vs-test wiring divergence cannot reintroduce itself.

### `app::slicePlane`, `app::makeBoxMesh` (`app/mpr_slice.cpp`),
### `app::make3dCamera`, `app::makeSliceCamera`, `app::makeSliceModel`
### (`broker/slice_display.cpp`)

The MPR scaffolding (see the sections above): the layout/slice/box helpers are
pure CPU math (headless-testable, `app/mpr_slice.*` stays data/+-volume-only
GL-free); the camera/display-transform helpers depend on render/ only for the
plain `render::Camera` struct. The GL draw of the 3D view is
`render::MeshRenderer`'s job, and the GL draw of each contour layer is
`render::ContourRenderer`'s job — both consumed by the sample.

**The 2D slice-view camera contract** (`app::makeSliceCamera`, T11 review):
an orthographic down-Z camera whose eye stands 512 units back along +Z with
far = 1024, so its clip volume covers display-frame z ∈ [-512, +511.9]. That
range must enclose BOTH the slice quad (z = 0) AND every GPU contour layer's
display-frame z — a contour crossing point sits at the held voxel-layer
coordinate + 0.5 (e.g. 35.5 / 64.5 / 64.5 for the sample's T/C/S state), far
above the slice quad's z = 0. A camera that excludes those z values clips
every emitted contour quad away **silently** — no GL error, no failed
`Result`, just no contour pixels; that is exactly the user-verified defect
(2026-08-24) where the pre-fix eye at z = 5 (near/far 0.1/10, clip range
[-4.9, +4.9]) left all three views without contours while the direct-render
gate test kept passing under its own wider camera. The fix moved
`makeSliceCamera`/`makeSliceModel` into shared scaffolding so the gate tests
drive the exact functions the sample composes with (tests/t15_mpr_test.cpp
asserts the enclosure analytically on the projected NDC z of both the slice
quad and the contour plane). The XY pixel mapping does not depend on the eye
distance (pure Z translation feeding an unchanged ortho window), so slice
images rasterize identically to before.

### `app::MPRView : app::ISample` + `main()` (`app/mpr_sample.cpp`)

The MPR sample. On construction it loads the CT volume
(`data/volumes/sample_ct.nrrd`, SPEC §7), registers the golden box mesh
(`[32,96]×[32,96]×[10,60]` inside the 128×128×70 volume, so every slice plane
cuts it at ANY index) once with the shared asset store, and registers the
contour mapper with the Broker. The initial slice state holds the middle
slice per axis. Per frame it:

1. advances the deterministic **auto-scroll**: every 45 frames the round-robin
   next axis steps one voxel layer (wrapping at its dimension) — a pure
   integer mutation of the shared slice state;
2. for each of T/C/S builds the `VolumeSliceInstance` fresh from the CURRENT
   state (dataset ref + TF value + display model + constant-Z plane at the
   held coordinate) and renders it into the view's own **640×480 offscreen
   FBO** via `render::VolumeSliceRenderer::drawLayer` — the extraction samples
   the cached R32F texture on the GPU; a slice change reaches it exclusively
   through uniforms (docs/render.md, "VolumeSliceRenderer");
3. layers that view's **GPU contour** over the extracted plane — translated
   from the same current plane through `broker::ContourMapper` and drawn by
   `render::ContourRenderer` (`drawLayer`, no clear between layers,
   FR-app.3) — so outlines stay pixel-glued to the displayed layer while
   scrolling;
4. renders the **3D view FBO** (the golden box) via `render::MeshRenderer`
   with a camera recomputed from the current slice state (`make3dCamera`):
   moving any slice moves the crosshair and refocuses the 3D view;
5. presents the four FBOs onto the window's default framebuffer in their
   viewport regions via the engine present path (`render::View::blitTo` →
   `core::blit`, T5 V3.4, docs/render.md): the sample builds per-view ReViews
   (ViewTarget + IRenderable list) and blits each FBO into its window rect —
   **no app-side viewport blending**.

There is NO CPU slice image anywhere on this path (the extraction gate greps
the sample source for zero `makeSliceImage` call sites after comment
stripping). The interactive-scroll correctness gate drives the renderer
directly: render → readback → move ONLY the slice-plane point one layer →
render again → the new readback must equal the new layer's closed-form values
(+4 per red byte on the probe field), proving re-extraction through uniforms
with no CPU image involved.

The sample exits cleanly (code 0) after `RE_SAMPLE_MAX_FRAMES` frames (default
300) so the gate can run it headlessly under Xvfb within a timeout (FR-app.1).

## Running the MPR sample

```sh
source tools/env.sh && cmake --build build -j
# Interactive (WSLg display):
./build/app/re_sample_mpr
# Headless (Xvfb), bounded run for automation:
RE_SAMPLE_MAX_FRAMES=30 xvfb-run -a ./build/app/re_sample_mpr
# Headless with a first-frame window capture (T11 defect-gate aid): writes a
# P6 PPM of the composed window after the first frame so the LIVE path can be
# verified pixel-wise without a display-side screenshot tool.
RE_SAMPLE_MAX_FRAMES=3 RE_SAMPLE_DUMP_FRAME=/tmp/mpr.ppm xvfb-run -a ./build/app/re_sample_mpr
```

The `RE_SAMPLE_MAX_FRAMES` env var bounds the run loop (default 300), matching
the other samples (docs/samples.md).

## Acceptance constants (FR-app.2/3, docs/mpr.md)

| Quantity | Value | Where it comes from |
|---|---|---|
| Window size | `1280×960` | SPEC §4 FR-app.2 |
| Each viewport | `640×480` | SPEC §4 FR-app.2 (`window/2 × window/2`) |
| Grid positions | T `{0,480}`, C `{640,480}`, S `{0,0}`, 3D `{640,0}` | the four equal quadrants, y up from the bottom scanline |
| Transverse axis | constant Z | SPEC §4 FR-app.2 (axial) |
| Coronal axis | constant Y | SPEC §4 FR-app.2 |
| Sagittal axis | constant X | SPEC §4 FR-app.2 |
| Extracted-slice pixel | `tf.sample(dataset.sampleTrilinear(px, py, heldIndex))` within 1/255 per channel | the GPU shader samples the cached R32F texture at `(idx+0.5)/dim`, reproducing `sampleTrilinear` voxel-for-voxel; whole-frame equality with the CPU oracle asserted per axis on an 8×6×4 volume |
| Slice state change → pixels | new layer's closed-form values within one frame; probe-field delta exactly +4 per red byte | readback after a plane-point-only change proves re-extraction through uniforms (no CPU image on the path) |
| Auto-scroll cadence | 1 layer / 45 frames, round-robin T→C→S | deterministic animation demonstrating interactive scrolling; bounded smoke runs (20 frames) stay on the initial mid-volume state |
| Contour color | `(255, 0, 0, 255)` | `app::kContourColor` = pure red (FR-app.3); GPU strokes are exact (blending disabled) |
| Contour band | 2 px (Euclidean) = the renderer stroke half-width `uHalfWidthPx` | SPEC §4 FR-app.3; the geometry shader expands each outline segment to a ±2 px thick-line quad with square caps |
| Contour coverage | ≥ 90% of in-band pixels match the contour color (within 1/255), readback-verified | SPEC §4 FR-app.3. The 2 px band around the gate's 32×32 rectangle holds **exactly 508 pixels** (closed-form count: 128-unit perimeter × 4-unit band ≈ 512, minus the overlapping corner regions; no pixel center sits at distance exactly 2.0, so the count is rounding-independent). Every in-band pixel center lies inside some emitted quad, so the matched fraction is ~100% |
| Golden box | `[32,96]×[32,96]×[10,60]` voxel-index units | the sample's box inside the 128×128×70 CT; every slice plane cuts it |
| Geometry-shader contour segments | 8 (for the golden box at any cutting plane) | the 4 side faces contribute 2 crossing triangles each (hand-counted) |
| 3D-view camera | eye = crosshair + normalize(1,1,1)·1.5·diag, looking at the crosshair | `app::make3dCamera` (the slice-state ↔ 3D-view interplay) |
| 2D slice-view camera | ortho down-Z, eye z = 512, far = 1024 → clip range covers display z ∈ [-512, +511.9] ⊇ {slice quad z=0} ∪ {contour crossings at held coordinate + 0.5 ≤ 128.5} | `app::makeSliceCamera`; enclosure asserted analytically in tests/t15_mpr_test.cpp (the T11 no-contour defect was a clip-range exclusion) |
| Slice display permutations | Transverse identity; Coronal swaps Y/Z; Sagittal `(x,y,z)→(y,z,x)` | the axis-permutation models (glm's constructor is COLUMN-major — the transposed Sagittal matrix put the live outline half off-screen until the T11 review fix; each view is now pinned on an asymmetric probe vector in tests/t15_mpr_test.cpp) |
| 3D-view center pixel | `{51, 102, 204}` ± 1/255 | the +Z face of the flat-shaded box, entered by the center ray at (89,89,60) (FR-app.3(2)) |
| Sample exit code | `0` | the harness returns 0 only after all frames rendered cleanly; any frame error → 1, any hang → 124, any ASan/UBSan abort → signal (FR-app.1) |

The T14 gate asserts the layout constants (1) and, for each of T/C/S, that the
CPU oracle reads the volume along the pinned axis (2), plus the MPR sample
smoke run (gate G, N≥3). The T15 gate asserts the GPU contour overlay
(≥ 90% of the 2 px band pixels match the contour color within 1/255, per
view, via `utils::PixelReader` readback of the `ContourRenderer` output),
the broker translation (`ContourMapper` RE-minimal payload + typed errors),
and the 3D view (center pixel = base color), plus the MPR sample smoke run
(gate G, N≥3). The plane-capability review gate additionally asserts, per
axis, that the composed ReView path's GPU-extracted frame equals the retained
CPU oracle whole-frame within 1/255 on an asymmetric volume, plus the
readback-after-state-change interactivity proof and the mechanical floor
(plane sample loads a volume; zero `makeSliceImage` call sites in the MPR
sample; oracle retained). The layout/slice tests are CPU-only;
the extraction/contour/3D-view tests render under the offscreen fixture; the
smoke test spawns the sample subprocess.

## Guardrails observed

- **GL ownership**: raw `glXxx` calls live only under `core/`. The MPR sample
  renders through `render::VolumeSliceRenderer`, `render::ContourRenderer`,
  `render::MeshRenderer` and `core/` wrappers only (guardrail
  `gpu_api_ownership`); `app/mpr_slice.*` is GL-free, and the display-camera factories now live in `broker/slice_display.*` (RE-side types may not be named under `app/`, enforced by the `acl_app_render` audit rule).
- **Typed diagnostics**: frame/GL failures surface as typed `data::Result`
  errors (SPEC §5); never silent.
- **Deterministic / single-threaded**: one window, one GL context, one render
  thread (SPEC §5). The extraction planes, contour overlays, auto-scroll steps
  and the 3D-view camera are all derived from the shared slice state (the
  deterministic CT + a fixed transfer function + the closed-form box
  cross-section), so every frame is reproducible.
- **Logging**: spdlog only.
- **Doxygen** on all public API (SPEC §5).