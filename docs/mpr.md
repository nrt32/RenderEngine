# app/ — MPR view (layout + slice views + contour + 3D view)

`app/` is the **compositions + samples module** (SPEC §3). MPR is **app-level
composition, not a module**: the Multi-Planar Reconstruction (MPR) sample
composes the existing renderers (PlaneRenderer for the slice views,
MeshRenderer for the 3D view) into a single window. This page documents the
**T14/T15 deliverables** (FR-app.2/3): the **1280×960 window with a 2×2
viewport grid**, the **T/C/S slice views** that sample the volume along the
pinned axis convention, the **mesh contour overlay** on each slice view
(plane∩mesh cross-section, FR-app.3), the **3D rendering view** (the golden box
mesh, FR-app.3), and the **slice-state ↔ 3D-view camera interplay**. It is part
of the `docs/mpr.md` documentation map (T14/T15).

## The MPR layout (FR-app.2)

One **1280×960** window is split into a **2×2 grid of four 640×480 viewports**
(SPEC §4 FR-app.2):

| Position | View | Content |
|---|---|---|
| **T** top-left | Transverse | slice at constant **Z** (axial) + mesh contour |
| **C** top-right | Coronal | slice at constant **Y** + mesh contour |
| **S** bottom-left | Sagittal | slice at constant **X** + mesh contour |
| **3D** bottom-right | 3D view | the golden box mesh (FR-app.3) |

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

## The mesh contour overlay (FR-app.3)

Each of the three slice views overlays the **plane∩mesh cross-section contour**
of the golden box mesh: the box's intersection with that view's slice plane,
computed in closed form and rasterized into the slice image at the contour
color `app::kContourColor` (pure red → RGBA8 `255,0,0,255`) before the
PlaneRenderer displays it (so the contour is literally overlaid on the slice).

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
- `app::meshPlaneContour(mesh, plane)` — the plane∩mesh cross-section curve as
  2D segments in the slice image's pixel space. Each triangle that strictly
  straddles the plane contributes the segment between its two crossing points,
  projected onto the view's two free axes (Transverse keeps (x, y), Coronal
  (x, z), Sagittal (y, z)); tangent/coplanar triangles contribute nothing. For
  the golden box the curve's **union is exactly the rectangle boundary** of the
  box's cross-section: 8 triangle-segments (the 4 side faces contribute 2
  triangles each) covering the 4 rectangle edges.
- `app::overlayContour(image, curve, color)` — returns a copy of the slice
  image with the curve rasterized into it: every pixel whose center is within
  the FR-app.3 **2 px band** (Euclidean distance, plus a 1e-3 float-rounding
  guard) of a segment is written with the contour color; pixels outside the
  band keep the slice background exactly.

The T15 gate asserts (FR-app.3(1)): for each slice view's plane, **≥ 90% of
the pixels within 2 px (Euclidean) of the analytic plane∩mesh intersection
curve match the contour color**, where the analytic curve (the closed-form
rectangle from the box + plane) is computed independently in the test, and the
scaffolding's segments are verified to lie on and cover that curve exactly.

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

### `app::SlicePlane`, `app::ContourSegment`, `app::kContourColor`
(`app/mpr_contour.hpp`)

- `SlicePlane {axis, coordinate}` — the plane of one slice view: perpendicular
  to `axis` at `coordinate` (voxel-index units, through the voxel centers).
- `ContourSegment` — a 2D line segment in a slice view's image pixel space
  (a `std::array<glm::vec2, 2>`).
- `kContourColor` — the FR-app.3 contour color, pure red (RGBA8 255,0,0,255).

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
(FR-app.2(2)).

### `app::slicePlane`, `app::makeBoxMesh`, `app::meshPlaneContour`,
`app::overlayContour`, `app::make3dCamera` (`app/mpr_contour.cpp`)

The T15 scaffolding (see the sections above). Pure CPU math + image
rasterization (headless-testable); it depends on render/ only for the plain
`render::Camera` struct. The GL draw of the 3D view is `render::MeshRenderer`'s
job, consumed by the sample.

### `app::MPRView : app::ISample` + `main()` (`app/mpr_sample.cpp`)

The MPR sample. On construction it loads the CT volume
(`data/volumes/sample_ct.nrrd`, SPEC §7), builds the three slice images at the
initial slice state (middle slice per axis), overlays the box's contour on each
(the golden box `[32,96]×[32,96]×[10,60]` inside the 128×128×70 volume, so
every slice plane cuts it), and configures the 3D-view camera (make3dCamera)
and scene (box + opaque Phong material). Per frame it:

1. renders each of the three slice views into its own **640×480 offscreen FBO**
   via `render::PlaneRenderer`: for view *i* the ortho maps that view's slice
   image pixel space `[0,imgW]×[0,imgH]` onto the full viewport and the shared
   unit quad is scaled onto that pixel rectangle (`makeSliceCamera` /
   `makeSliceModel`), so the whole slice fills the view;
2. renders the **3D view FBO** (the golden box) via `render::MeshRenderer`
   with the slice-state-driven camera;
3. presents the four FBOs onto the window's default framebuffer in their
   viewport regions via the **engine multi-view compositor**
   (`render::ViewRenderer` + `core::blit`, SPEC §9 V2.4 / V2 T2, docs/render.md):
   the sample shares only the per-view `ViewRect`s + `Scene` objects, the engine
   dispatches each through `IRenderer` into the view's own FBO and blits each
   FBO into its window rect — **no app-side viewport blending** (the old
   textured-quad present pass is gone).

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

## Acceptance constants (FR-app.2/3, docs/mpr.md)

| Quantity | Value | Where it comes from |
|---|---|---|
| Window size | `1280×960` | SPEC §4 FR-app.2 |
| Each viewport | `640×480` | SPEC §4 FR-app.2 (`window/2 × window/2`) |
| Grid positions | T `{0,480}`, C `{640,480}`, S `{0,0}`, 3D `{640,0}` | the four equal quadrants, y up from the bottom scanline |
| Transverse axis | constant Z | SPEC §4 FR-app.2 (axial) |
| Coronal axis | constant Y | SPEC §4 FR-app.2 |
| Sagittal axis | constant X | SPEC §4 FR-app.2 |
| Slice-image pixel | `round(tf.sample(voxel)·255 + 0.5)` per channel | FR-vol.1 exact at control points; the T14 gate uses a synthetic volume + TF so each pixel's red byte equals the underlying voxel value |
| Contour color | `(255, 0, 0, 255)` | `app::kContourColor` = pure red (FR-app.3) |
| Contour band | 2 px (Euclidean), +1e-3 float guard | SPEC §4 FR-app.3 |
| Contour coverage | ≥ 90% of in-band pixels match the contour color | SPEC §4 FR-app.3. The 2 px band around the gate's 32×32 rectangle holds **exactly 508 pixels** (closed-form count: 128-unit perimeter × 4-unit band ≈ 512, minus the overlapping corner regions; no pixel center sits at distance exactly 2.0, so the count is rounding-independent) and the rasterizer colors all of them (segment union = boundary), so the matched fraction is ~100% |
| Golden box | `[32,96]×[32,96]×[10,60]` voxel-index units | the sample's box inside the 128×128×70 CT; every slice plane cuts it |
| Contour segment count | 8 (for the golden box at any cutting plane) | the 4 side faces contribute 2 crossing triangles each (hand-counted) |
| 3D-view camera | eye = crosshair + normalize(1,1,1)·1.5·diag, looking at the crosshair | `app::make3dCamera` (the slice-state ↔ 3D-view interplay) |
| 3D-view center pixel | `{51, 102, 204}` ± 1/255 | the +Z face of the flat-shaded box, entered by the center ray at (89,89,60) (FR-app.3(2)) |
| Sample exit code | `0` | the harness returns 0 only after all frames rendered cleanly; any frame error → 1, any hang → 124, any ASan/UBSan abort → signal (FR-app.1) |

The T14 gate asserts the layout constants (1) and, for each of T/C/S, that the
slice image's pixel bytes read the volume along the pinned axis (2), plus the
MPR sample smoke run (gate G, N≥3). The T15 gate asserts the contour overlay
(≥ 90% of the 2 px band pixels match the contour color, per view) and the 3D
view (center pixel = base color), plus the MPR sample smoke run (gate G,
N≥3). The layout/slice/contour/camera tests are CPU-only; the 3D-view test
renders through render::MeshRenderer under the offscreen fixture; the smoke
test spawns the sample subprocess.

## Guardrails observed

- **GL ownership**: raw `glXxx` calls live only under `core/`. The MPR sample
  renders through `render::PlaneRenderer`, `render::MeshRenderer` and `core/`
  wrappers only (guardrail `gpu_api_ownership`); `app/mpr_slice.*` and
  `app/mpr_contour.*` are GL-free.
- **Typed diagnostics**: frame/GL failures surface as typed `data::Result`
  errors (SPEC §5); never silent.
- **Deterministic / single-threaded**: one window, one GL context, one render
  thread (SPEC §5). Slice images, contour overlays and the 3D-view camera are
  built from a fixed slice state (the deterministic CT + a fixed transfer
  function + the closed-form box cross-section).
- **Logging**: spdlog only.
- **Doxygen** on all public API (SPEC §5).