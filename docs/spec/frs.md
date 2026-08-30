# SPEC §4 — Functional requirements

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §4" mean this file.

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
- **FR-io.2** NRRD volume loader loads a known volume. *Acceptance: **sample `128×128×70` `≤128³` exact** per `data/volumes/sample_ct.nrrd` golden (dims + per-voxel corner values exact); **product has No cap streaming via `core::Caps`** — any dims tiled/downsampled via `core::Caps maxTexture3DSize` within 1/255 of reference tiled (per `T11a` + `NFR §5` `maxTexture3DSize`), `BudgetExceeded` only when `core::Caps` probe fails (not on `>128³` alone; `T11` No-cap supersedes archived `COMPLETED_TASKS.md:121` `T5` `≤128³` memory cap — see `T11a` `core::Caps` tiled, `T11a` gate `BudgetExceeded` code 8, iteration 6 #9).*
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

### render/ — V7 extensions (CSG, Points, Lines)
- **FR-render.7** CSG via Puxel 2-stage SSBO: `Cube(2) − Sphere(0.6)` centered. *Acceptance: hole center pixel matches `B`'s material within 1/255, `outside A` intact, ray through hole sees `clearColor` within 1/255; `transparent(A α0.5)−B + surrounding Mesh α0.6` k-way `over()` merge within 1/255; `paintInterior` true recolors interior surviving `base` fragments vs false surface strip only within 1/255; `nodeCapacity()=w*h*maxFpp*16 ≤157286400` `152 MB` (`640×480×8×16=39321600` `37.5 MB`) + `readResolvedCount` per-pixel `1` where hole.*
- **FR-render.8** Points: `PointObject` single + `PointCloudObject` many. *Acceptance: `3D` Perspective sphere center `EXPECT_NEAR(..., MeshObject{GeometryKind::Sphere} oracle, 1.0/255.0)` 1/255 `N>=3`; `2D` `ClipPlane` same points as flat circles 1/255; `worldUnits=false` `10px` constant at `2` camera distances within 1/255; `fill=Hollow` vs `GridDashed` golden `1/255` `N>=3`.*
- **FR-render.9** Lines: `LineObject`/`PolylineObject` `SSBO+gl_VertexID` 6-vert view-quad. *Acceptance: `640×480` solid red `2px` horizontal across black `≥90%` of geometric `±width/2` band within 1/255 of red (`N>=3`, mirrors `contour` `≥90% within 2px`); dashed `dash 8 gap 4` known pixel `1/255`; `worldUnits=true` attenuates with distance `1/255`; analytic `fwidth` AA + `Rougier mod(s)` + `miterLimit 4→bevel`, `round/square` caps.*

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
- **FR-app.4** Engine facade exposes `addCsg/addPoint/addLine` through `IViewBridge`. *Acceptance: `Engine` headless `renderOffscreen(640,480)` smoke `1/255` per new kind: `addCsg(Cube−Sphere)` hole `1/255`, `addPointCloud(10)` vs `addLine(2px)` `1/255`; `Engine` `DepthConfig` still `1` via `grep -c "DepthConfig\{true" ==1` `N>=3`; `app` never includes `render/` (`acl_app_render`).*