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
  samples render into), kept LIVE by the framebuffer-size callback (below).
- **Framebuffer-size events (`core::FramebufferSizeState`, T23)** — `create()`
  registers the GLFW framebuffer-size callback; every event overwrites the
  stored physical pixel size and latches a dirty flag that
  `Window::consumeFramebufferResized()` returns-and-clears (one delivery per
  event batch). The bookkeeping lives in a shared state block targeted by the
  GLFW user pointer, so the registration survives `Window` moves, and paths
  that never create a visible window (the offscreen unit-test fixture) never
  fire it — the headless gates are unaffected.
- Version probe via `glGetIntegerv` (GL_MAJOR_VERSION / GL_MINOR_VERSION),
  matching the offscreen fixture (SPEC §2/§8).

### `app::ISample` + `app::SampleHarness` (`app/sample_harness.hpp`, `.cpp`) + `app::FrameLoop` + `app::ImGuiOverlay` (V5 T3 decoupled)

The **shared sample harness** (T12 deliverable, V5 T3 decoupled): a reusable
app/ component that owns a visible window and runs the frame loop. A sample
implements `ISample` and the harness drives it. V5 T3 splits the former
monolithic harness into three ownership domains — `core::Window` stays the
visible-window owner, `app::FrameLoop` (`app/frame_loop.hpp`) owns the
window-free `renderViews` helper, and `app::ImGuiOverlay`
(`app/imgui_overlay.hpp`) is the SOLE owner of the Dear ImGui wiring — so
`renderViews` is callable without a `Window` (prerequisite for T4 offscreen).

| Member | Purpose |
|---|---|
| `ISample::renderFrame(width, height)` | render one frame of the sample's 3D scene into the window's **default framebuffer** (a `render::RenderTarget` with `framebuffer == nullptr`, see below). `width`/`height` are the CURRENT framebuffer pixel size — samples must derive view rects and camera aspect from these live dims, never from compile-time constants. Returns a typed error on failure (SPEC §5). |
| `ISample::onResize(width, height)` | optional resize hook (T23): called once per pending framebuffer-size event batch with the current pixel size, BEFORE the affected frame; default no-op. Samples that re-derive everything per frame need not override. |
| `ISample::title()` | one-line description shown in the ImGui overlay. |
| `ISample::instructions()` | optional multi-line help text shown in the overlay (T13) describing how to drive the sample's capability; empty by default. |
| `SampleHarness::run(maxFrames)` | BOUNDED run loop (V5 T3 sole public contract): poll events → (resize delivery) → `ImGuiOverlay::newFrame` → sample `renderFrame` → `ImGuiOverlay::drawSampleOverlay` → `ImGuiOverlay::render` → present. Stops cleanly after `maxFrames` frames or on window close; returns the process exit code (0 clean). |
| `SampleHarness::runInteractive()` | opt-in `until shouldClose()` loop (V5 T3): same steps but without a `maxFrames` bound — interactive sessions only, never CI. Samples keep bounded dispatch (`run(sampleMaxFrames(kDefaultFrames))`). |
| `app::sampleMaxFrames(default)` | reads the `RE_SAMPLE_MAX_FRAMES` env var (bounded run). When the var is UNSET/empty the helper returns `default` (e.g. 300 or 20), never an unbounded loop — a forgotten env var never hangs CI (V5 T3 bounded-default). |
| `app::renderViews(views, ctx, fb)` | window-free helper (`app/frame_loop.hpp`, V5 T3): `sync → renderAll → presentAll` into any `core::Framebuffer*` (nullptr = window default). No `Window` or GLFW, so T4 `renderOffscreen` reuses it. |
| `app::FrameLoop` | optional coordinator (`app/frame_loop.hpp`): `poll()`, `render(views)`, `renderTo(views, fb)`, `present()`, `shouldClose()` — separates the loop so the caller can interleave overlay work. |
| `app::ImGuiOverlay` | optional overlay (`app/imgui_overlay.hpp`, V5 T3): the sole owner of `ImGui_ImplGlfw_InitForOpenGL` etc. — `grep -c ImGui_ImplGlfw_InitForOpenGL app/sample_harness.cpp ==0`. |

Per frame the harness (`run(maxFrames)`, bounded):
1. `pollEvents()`, then — when a framebuffer-size event arrived since the
   previous frame (T23) — delivers `ISample::onResize(currentW, currentH)`
   before anything else consumes the frame;
2. `overlay_.newFrame()` (`ImGui_ImplOpenGL3_NewFrame`,
   `ImGui_ImplGlfw_NewFrame`, `ImGui::NewFrame`);
3. calls `sample_->renderFrame(w, h)` into the window's default framebuffer —
   if it returns a typed error, the run aborts with exit code 1 (never silent);
4. `overlay_.drawSampleOverlay(*sample_, frame, maxFrames)` then
   `overlay_.render()` (`ImGui::Render`, `ImGui_ImplOpenGL3_RenderDrawData`),
   invalidates `REContext::current()`, and swaps buffers.

The window-free path (`renderViews`) does the same `sync → renderAll → presentAll`
without a `Window` or overlay — center pixel within 1/255 of the harness path
(V5 T3 parity gate, `N>=3`).

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

### Live window size (T23)

Every sample derives its view rects and camera aspect from the **live
framebuffer pixel size** — compile-time window constants pick only the OPENING
window size and never feed projections, so a window resize reframes geometry
instead of stretching it:

| Sample | What tracks the live dims |
|---|---|
| mesh / slice / volume | full-window view: `app::fitPerspectiveViewToPixels` re-derives rect `{0,0,w,h}` + projection aspect `w/h` each frame (fov/near/far and the eye framing stay fixed) |
| OIT | full-window view: rect `{0,0,w,h}` + the arrangement ortho window grown to `±aspect` horizontally (`oit_scene::cameraFor`) |
| plane | two-view split re-resolved to left/right halves of the CURRENT window each frame; extraction cameras keep their dataset-extent ortho windows (the MPR display convention) |
| MPR | the 2x2 grid re-resolves via `app::mprViewports(w, h)` (four equal quadrants of the live window) and the 3D camera aspect follows its live quadrant |

**Manual verification** (interactive, WSLg): run any sample, then drag a
window edge/corner — the scene reframes to the new shape with no stretching
and no GL errors; MPR re-splits its grid live. The automated T23 gate covers
the same contract deterministically without a display:
`tests/t23_resize_test.cpp` simulates a resize by calling the hook's exact
code directly and asserts the next-frame projection matrix equals
`glm::perspective(fov, newAspect, near, far)` within 1e-6 (all 16 entries,
plus the closed-form `[0][0] = f/aspect` scalars with `f = sqrt(3)` at fov
60°), that the recomputed projection reaches the compositor's ReView through
`IViewBridge::sync` at a stable ReView address, and that the MPR grid math
re-splits over new dims.

## Samples (T12 + T13)

Each sample is a small executable (`app/re_sample_*`) that loads its data,
builds a scene, and hands an `ISample` to a `SampleHarness` (mesh uses
`re::viz::Engine` + `app::sampleMaxFrames(app::kDefaultFrames)`). All six exit
cleanly (code 0) after `RE_SAMPLE_MAX_FRAMES` frames (default `app::kDefaultFrames = 300`) so the gate
can run them headlessly under Xvfb within a timeout (FR-app.1, T9 bounded
discipline). The harness's `run(maxFrames)` is the sole bounded contract and
`runInteractive()` is opt-in only — a forgotten `RE_SAMPLE_MAX_FRAMES` never
hangs CI because `sampleMaxFrames(kDefaultFrames)` defaults to `300`. Build them with
`RE_BUILD_SAMPLES=ON` (default; also forced on whenever `RE_BUILD_TESTS` is on,
because the T12/T13 gate spawns them).

| Sample | Executable | Demonstrates | Data |
|---|---|---|---|
| Mesh | `re_sample_mesh` | opaque shaded mesh (Phong) | `data/meshes/bunny.obj` (SPEC §7) |
| Plane | `re_sample_plane` | GPU-extracted volume planes (axial + oblique, FR-render.5 extension) | `data/volumes/sample_ct.nrrd` + CT window/level transfer function |
| Volume | `re_sample_volume` | ray-cast volume (front-to-back compositing) | `data/volumes/sample_ct.nrrd` + CT window/level transfer function |
| Slice | `re_sample_slice` | geometry-shader plane clip of a mesh | `data/meshes/teapot.obj` (SPEC §7), clipped by a horizontal midplane |
| OIT | `re_sample_oit` | order-independent transparency over depth-tested opaque meshes (linked-list) | `data/meshes/bunny.obj` (SPEC §7) + procedural boxes (golden opaque box, two alpha-0.5 glass shells) |
| MPR | `re_sample_mpr` | Multi-Planar Reconstruction 2×2 grid (T/C/S + 3D) with scrolling + crosshair | `data/volumes/sample_ct.nrrd` + CT transfer function + golden box `kGoldenBoxMin/Max` |
| CSG | `re_sample_csg` | GPU CSG via Puxel 2-stage SSBO (FR-render.7): Cube(2)−Sphere(0.6) hole + transparent(A α0.5)−B + surrounding Mesh α0.6 k-way merge + paintInterior true/false (V7 T12, 2D/3D) | procedural Cube(2) + Sphere(0.6) + paint cubes 0.3 (closed manifold) |
| Point | `re_sample_point` | PointRenderer impostor: 3D Perspective spheres vs Mesh Sphere oracle `1/255` + 2D ClipPlane circles `1/255` + `worldUnits 10px` constant at two distances `1/255` + fill `Hollow/GridDashed` `1/255` + 10-point cloud, `radius worldUnits` toggle scales with distance (V7 T13, 2D/3D) | procedural 10-point cloud + single markers (worldUnits true/false, Solid/Hollow/GridDashed) |

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
compositing, SPEC §3). **Hotfix T19 (2026-08-30):** the bounded `volume` and `slice` samples previously lost `itemIds`/`plane` because `SceneViewBuilder::syncLive` is intentionally scoped to `rect` + `camera` only — `volume_sample.cpp` and `slice_sample.cpp` now mirror their long-lived peers `volume_long.cpp`/`slice_long.cpp` by assigning `builder_.view() = view_` after init and by preserving `plane`/`itemIds` across each `syncLive` (slice saves/restores `PlaneDesc` and updates `builder_.view()`; volume carries `itemIds` via the builder's stored view), so `syncRenderPresent` receives a view with one item and (for slice) a plane, restoring `FR-render.6`/`FR-render.4` rendering. The OIT sample composes REAL meshes (the shared scene
rig `app/oit_scene.hpp` — the exact arrangement the T19 gate probes): two
OPAQUE meshes, a golden box and the Stanford bunny at different depths, render
first through a `render::View` whose per-view depth-test flag is ON
(`render::View::setDepthTest`, the T18 depth support), so the view target owns
a real depth attachment and the opaques occlude each other by true depth; then
two TRANSPARENT glass boxes (red near, blue far, alpha 0.5) that interleave
both opaque meshes along the view direction are captured by the injected
`render::LinkedListOIT` pipeline — fragments go into a per-pixel linked list,
are sorted by depth, and composite back-to-front over the opaque result
(FR-render.2/3).

### Camera controls (V5 T9 — `scene::CameraController` pure math + `app::GlfwCameraInteractor` adapter)

Interactive camera orbit is driven by the pure-math `scene::CameraController` (`scene/camera_controller.hpp`, V5 T9) and its
windowing adapter `app::GlfwCameraInteractor` (`app/glfw_camera_interactor.hpp`). The controller is GL-free and
render-free — it maps pixel deltas to a `CameraDelta` (`onMouseDrag(dx,dy, button, modifiers) -> CameraDelta`,
`onScroll(delta)`, `onKey`) with linear analytic speeds (`rotateSpeed` degrees per pixel, `panSpeed` world units per
pixel, `zoomSpeed` factor per scroll unit) and exposes `CameraBindings{ rotateButton=LMB, panButton=RMB, zoomButton=MMB/wheel,
modifiers, rotateSpeed, panSpeed, zoomSpeed }` as a plain POD so each view can override it without a new type. The
adapter polls `glfwGetMouseButton`/`glfwGetCursorPos`/`glfwGetScroll` each frame before `renderFrame`, checks
`!ImGui::GetIO().WantCaptureMouse` (the `TASKS.md:126` guard — when the overlay wants the mouse the camera does not move),
forwards the delta into the controller, and then calls `View::setCamera(newCam)` (the `mutateCamera` lambda was deleted in
`T8a` — `setCamera` bumps `cameraGen`/`viewGen` via `SceneStore::bump(FieldId)`) so the per-field `viewGen` bump
propagates to `View::generation` and the broker's `ViewSynchronizer` re-translates only the dirty camera fields per
SPEC §10.4 (no full dump). Dragging 10 px with the default `rotateSpeed 0.5` yields a 5 deg yaw/pitch orbit; the gate
asserts the resulting `viewMatrix` against the analytic `Camera::rotate(5,0)` within `1e-6`,
and the `WantCaptureMouse=true` guard leaves the matrix unchanged (`delta 0` within `1e-6`) versus `false` yielding the
analytic orbit. The plane sample and the three MPR 2D orthographic slice views keep their fixed dataset-extent
orthographic framing and are **not** orbited (the `Wired in mesh/slice/volume/oit/mpr-3D; plane + MPR 2D orthographic skip`
rule); the mesh, slice, volume, OIT, and MPR 3D perspective views are all wired through the interactor.

### Driving each capability (per-sample instructions)

Every sample's ImGui overlay shows a short "How to drive this capability" help
block (from `ISample::instructions()`, T13). Since V5 T9 the perspective samples are interactive (orbit/pan/zoom via the
controller above); the overlay still describes the resize check (T23) and the harness exits cleanly after
`RE_SAMPLE_MAX_FRAMES` bounded frames, so the gate can run them headlessly. "Driving" a capability now means running
the sample, orbiting/panning/zooming the perspective view with the mouse (left-drag orbits yaw/pitch, right-drag pans,
middle-drag or wheel zooms, guarded by `WantCaptureMouse`), optionally resizing the window to verify the live-aspect
reframe, and exiting cleanly:

| Sample | How to drive it |
|---|---|
| `re_sample_mesh` | orbit/pan/zoom the bunny with left/right/middle-drag or wheel (controller `rotateSpeed 0.5 deg/px`, `WantCaptureMouse` guard, `viewGen` bump); resize check: drag an edge — the view reframes, no stretching; close the window to exit. |
| `re_sample_plane` | observe the two GPU-extracted CT planes (axial left, oblique right; fixed orthographic, not orbited per T9 skip); resize check: the split follows the live window halves; close the window to exit. |
| `re_sample_volume` | orbit/pan/zoom the CT chest with left/right/middle-drag or wheel (same controller, `viewGen` bump, broker re-translates only dirty camera); resize check: drag an edge — the view reframes, no stretching; close the window to exit. |
| `re_sample_slice` | orbit/pan/zoom the clipped teapot with left/right/middle-drag or wheel (same controller, `viewGen` bump); resize check: drag an edge — the view reframes, no stretching; close the window to exit. |
| `re_sample_oit` | orbit/pan/zoom the OIT composition with left/right/middle-drag or wheel (same controller, depth-tested target, `viewGen` bump); resize check: ortho extents follow the live aspect; close the window to exit. |
| `re_sample_mpr` | orbit/pan/zoom the 3D crosshair view with left/right/middle-drag or wheel (same controller, only the 3D perspective view is orbited, the three 2D orthographic slice views keep fixed framing per T9 skip); scroll the auto-advancing slices and watch the 3D crosshair view track them (FR-app.2/3); resize check: the 2x2 grid re-splits into four equal quadrants of the live window; close the window to exit. |
| `re_sample_csg` | left (3D perspective, `Camera::perspective` fov 45): Cube(2)−Sphere(0.6) hole shows `B` mat `1/255`, `transparent(A α0.5)−B + surrounding Mesh α0.6` k-way `over()` `1/255`, `paintInterior` true interior vs false strip `1/255`; right (2D `ClipPlane` `Space::World` axial + `Camera::ortho`): same CSG under axial plane; left orbit/pan/zoom via controller, right fixed per plane guard; resize halves reframe. |
| `re_sample_point` | `Engine::addPoint`/`addPointCloud` (V7 T13): left (3D perspective, `Camera::perspective` fov 45): single sphere radius 0.3 `worldUnits` true vs `Mesh Sphere` oracle `1/255` + 10-point cloud `worldUnits` true + `10px` `worldUnits` false marker constant across distances `1/255` `1e-6`; right (2D `ClipPlane` `Space::World` axial + `Camera::ortho`): same points as flat circles `1/255` with `fill Hollow` vs `GridDashed` `1/255`; `worldUnits` true scales with distance, false constant `10px` within `1/255`; resize halves reframe. |

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
| Window-opened marker in log | contains `GL 4.6 core` | `core::Window::create` logs `window: 800x600 (framebuffer 800x600) GL 4.6 core` (requested size + live framebuffer size) only after `glfwCreateWindow` + glad loading + the `glGetIntegerv` 4.6 probe succeed (FR-app.1 "opens a window", SPEC §2/§8) |
| Sanitizer signatures in log | none of `AddressSanitizer`, `UndefinedBehaviorSanitizer`, `runtime error:`, `LeakSanitizer` | FR-app.1 "no sanitizer reports"; address/UB detection stays active in the subprocess (the leak gate remains the unit-test suite on llvmpipe, SPEC §8 — the Xvfb windowing stack's fontconfig/pango allocations are third-party driver noise) |
| Per-sample frame count | `20` | `RE_SAMPLE_MAX_FRAMES=20`: bounded run proving the loop iterated; exit 0 implies all 20 frames rendered |

## Long-lived interactive samples (T10, not for testing)

The bounded samples above exit after `RE_SAMPLE_MAX_FRAMES` (default 300) so the gate can run them headlessly. T10 adds **long-lived interactive** peers that bypass that bound and run until the window is closed:

| Bounded (tested) | Long-lived (interactive, EXCLUDE_FROM_ALL) | Interaction |
|---|---|---|
| `re_sample_mesh` | `re_sample_mesh_long` | left-drag rotate `dx*0.5deg`, right-drag pan `dx*0.01`, scroll/middle-drag zoom `exp(-dy*0.02)` |
| `re_sample_volume` | `re_sample_volume_long` | same |
| `re_sample_plane` | `re_sample_plane_long` | interactor called per view but orthographic/PlaneDesc guard skips (plane + MPR 2D fixed framing) |
| `re_sample_slice` | `re_sample_slice_long` | interactor called but PlaneDesc guard vetoes (perspective clip view stays fixed per plane guard; 2D orthographic skip via plane guard) |
| `re_sample_oit` | `re_sample_oit_long` | same (depth-tested, `DepthConfig{true}` preserved) |
| `re_sample_mpr` | `re_sample_mpr_long` | only the 3D perspective view is orbited; three 2D orthographic slice views keep fixed framing |
| `re_sample_csg` | `re_sample_csg_long` | `CsgSample 2D/3D`: left 3D perspective orbit/pan/zoom via `CameraController`+`GlfwCameraInteractor` (WantCaptureMouse guard), right 2D `ClipPlane` axial fixed per plane guard; paints `paintInterior` true/false `1/255`, hole `B` mat `1/255`, k-way merge `1/255` |
| `re_sample_point` | `re_sample_point_long` | `PointSample 2D/3D`: left 3D perspective `10-cloud worldUnits true` + `10px worldUnits false` marker orbit/pan/zoom via controller (WantCaptureMouse guard), right 2D `ClipPlane` axial flat circles `fill Hollow/GridDashed` fixed per plane guard `1/255`; 3D sphere vs mesh oracle `1/255`, `worldUnits 10px` constant `1/255` |

Each long-lived target mirrors its bounded peer's scene setup but its `renderFrame` calls `interactor.update(view)` (or `interactor.update(builder.view())` / `interactor.update(views_[3])` for MPR) **before** `syncRenderPresent`, respects `ImGui::GetIO().WantCaptureMouse` (no camera change when the overlay wants the mouse) and skips orthographic/PlaneDesc views per V5 T9 guard, and mutates via `view.setCamera(newCam)` (not the deleted `mutateCamera` lambda) so `viewGen`/`cameraGen` + `generation` bump via `SceneStore::bump(FieldId)` lets the broker re-translate only dirty camera fields within the analytic tolerance. They are built via `cmake --build build --target re_samples_long` (or per-target `re_sample_*_long`) and run as `./build/app/re_sample_mesh_long --help` (shows help) or without args until window close via `SampleHarness::runInteractive()` (`app/sample_harness.cpp:111` `until shouldClose()`). They are **EXCLUDE_FROM_ALL** and never invoked by `tools/test.sh` or `ctest` — `ctest` suite count remains two (`re_tests` + `re_tests_death`) and the `re_samples_long` target is separate.

Waiver: `examples/` is intentionally not in `AUDIT_SOURCE_DIRS` per `README.md:33` (sample copies for documentation, not audited production code); `no_secrets` still scans it via the built-in whole-tree scan in `tools/audit.sh:148`, so the waiver is documented here and does not bypass the secret scan.

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