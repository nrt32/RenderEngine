# render/ — rendering techniques

`render/` is the **rendering-techniques module** (SPEC §3): ONE class per
rendering technique behind a unified `IRenderer`-style set of stateless
renderers. It consumes GL only through `core/` wrappers and the `core::Draw`
API (guardrail `gpu_api_ownership`); it depends on `IMaterial` /
`ITransparencyPipeline` abstractions, never concrete material/OIT classes
(dependency inversion, SPEC §3).

> This page documents the **T7 deliverable** (`IMaterial` + `PhongMaterial`,
> the `ITransparencyPipeline` interface, the shared `MeshGeometry`, and the
> `MeshRenderer` opaque forward pass, FR-render.1/3), the **T8 deliverable**
> (`PlaneRenderer` textured quads/planes, FR-render.5), the **T9 deliverable**
> (`VolumeRenderer` ray-cast GL draw pass, FR-render.6), the **T10
> deliverable** (`LinkedListOIT`, the per-pixel linked-list order-independent
> transparency pipeline, FR-render.2/3), and the **T11 deliverable**
> (`SliceRenderer`, the geometry-shader plane clip of a mesh, FR-render.4), and
> the **V2 T1 deliverable** (`render/types.hpp` shared types + the `IRenderer`
> dispatch contract, SPEC §9 V2.3), the **V2 T2 deliverable** (the
> multi-view compositor `View`/`ViewRect`/`ViewRenderer` + the new `core::blit`,
> SPEC §9 V2.4 — superseded by T5), the **V2 T3 deliverable** (the generational asset registry
> `AssetRegistry`/`AssetHandle`, SPEC §9 V2.5), the **V2 T7 deliverable**
> (shader externalization to `.glsl` files + malformed fixture, SPEC §9 V2.6),
> the **V2 T8 deliverable** (GLSL profile macro `RE_GLSL_VERSION`, SPEC §9
> V2.7), the **T4 (V3.3) deliverable** (`scene::Camera` manipulable
> `pan/rotate/zoom/orbit` → `render::Camera{view,proj,pos}` via
> `broker::CameraMapper`; `2D` ortho vs `3D` perspective validated by mapper),
> the **T5 (V3.4) deliverable** (`render::View` (`ReView`) per screen
> section + `ViewTarget{Texture2D+Framebuffer}` + `IRenderable` type-erased
> `drawLayer` + `core::blit`; deletes `ViewRenderer`),
> the **T11 (V3.8b) deliverable** (`ContourRenderer`, the GPU
> geometry-shader plane∩mesh outline for the MPR contour overlay,
> FR-app.3 — replaces the deleted CPU `app/mpr_contour.*`
> `meshPlaneContour`/`overlayContour` path),
> and the **plane-capability review deliverable** (`VolumeSliceRenderer`, GPU
> volume-plane extraction: a plane through a volume is sampled entirely on
> the GPU from the cached 3D texture at the view's clip plane — extends
> FR-render.5/FR-app.2, makes MPR slice scrolling a pure uniform change, and
> retires the frozen CPU slice images from every live sample).
> It is part of the `docs/render.md` documentation map
> (T7/T8/T9/T10/T11 + V2 T1/T2/T3/T7/T8 + T4 + T5 + T11(V3.8b) + T16; later tasks extend it).

## Components

### Shader files (`.glsl`) — externalized GLSL (SPEC §9 V2.6, V2 T7)

The GLSL that was previously inline `constexpr char[]` in `render/*.cpp` now
lives as **`.glsl` files under `render/shaders/`** and is loaded at runtime by
**`core::ShaderProgram`'s file helpers** (`loadSourceFile`,
`createFromFiles`, `createWithGeometryFromFiles`,
`createWithTransformFeedbackFromFiles`). This is a **relocation only** — no
shader logic changed — and it adds syntax highlighting/editor navigation for
the GLSL (SPEC §9 V2.6). The absolute source path is baked at compile time
via `RE_SHADER_DIR` (`render/CMakeLists.txt`) so the renderers resolve the
files regardless of the working directory; the `.glsl` contents are byte-for-byte
the former `constexpr` literals (including the trailing newline), so **line
numbers are preserved** and diagnostics keep their golden `ERROR: 0:N` form.

| Shader file | Former inline symbol | Stage | Used by |
|---|---|---|---|
| `mesh_opaque.vert.glsl` | `kOpaqueVertexShader` | vertex | `MeshRenderer` |
| `mesh_opaque.frag.glsl` | `kOpaqueFragmentShader` | fragment | `MeshRenderer` |
| `plane.vert.glsl` | `kPlaneVertexShader` | vertex | `PlaneRenderer` |
| `plane.frag.glsl` | `kPlaneFragmentShader` | fragment | `PlaneRenderer` |
| `volume_raycast.vert.glsl` | `kRayCastVertexShader` | vertex | `VolumeRenderer` + `VolumeSliceRenderer` (shared NDC-quad passthrough) |
| `volume_raycast.frag.glsl` | `kRayCastFragmentShader` | fragment | `VolumeRenderer` |
| `volume_slice.frag.glsl` | (new, plane-capability review) | fragment | `VolumeSliceRenderer` |
| `oit_capture.vert.glsl` | `kCaptureVertexShader` | vertex | `LinkedListOIT` (capture) |
| `oit_capture.frag.glsl` | `kCaptureFragmentShader` | fragment | `LinkedListOIT` (capture) |
| `oit_composite.vert.glsl` | `kCompositeVertexShader` | vertex | `LinkedListOIT` (composite) |
| `oit_composite.frag.glsl` | `kCompositeFragmentShader` | fragment | `LinkedListOIT` (composite) |
| `slice.vert.glsl` | `kSliceVertexShader` | vertex | `SliceRenderer` (shared) |
| `slice_clip.geom.glsl` | `kClipGeometryShader` | geometry | `SliceRenderer` (clip) |
| `slice_clip.frag.glsl` | `kClipFragmentShader` | fragment | `SliceRenderer` (clip) |
| `slice_capture.geom.glsl` | `kCaptureGeometryShader` | geometry | `SliceRenderer` (capture) |
| `slice_capture.frag.glsl` | `kCaptureFragmentShader` | fragment | `SliceRenderer` (capture) |
| `contour.vert.glsl` | (new, V3.8b T11) | vertex | `ContourRenderer` |
| `contour.geom.glsl` | (new, V3.8b T11) | geometry | `ContourRenderer` |
| `contour.frag.glsl` | (new, V3.8b T11) | fragment | `ContourRenderer` |

**Malformed-shader fixture (T3 golden substring).** The completed-loop T3 gate's
intentionally-malformed shader (`glibberish` on line 7, golden substring
`ERROR: 0:7`) is now reproducible via a **fixture file**
`render/shaders/fixtures/malformed.vert.glsl` (and
`malformed.frag.glsl` for the fragment-stage variant at line 5). Loading that
file through `core::ShaderProgram::loadSourceFile` + `create` yields the same
typed error (`ShaderError::VertexCompile`, code 1, message containing
`glibberish` and `ERROR: 0:7`, no crash) as the former inline literal — the
line number is pinned by the file's preserved newlines. The existing inline
T3 gate (`tests/t3_core_gl_test.cpp`) stays green (it still embeds the same
source for the direct-string path); the fixture is the file-backed reproduction
required by V2.6.

**Guardrail note.** `render/` remains GL-call-free (guardrail
`gpu_api_ownership`): it loads GLSL text via `core::ShaderProgram` and draws
through `core::Draw`/`core::ShaderProgram`; the raw `glCreateShader`/
`glCompileShader`/`glLinkProgram` calls stay under `core/shader_program.cpp`.

### `RE_GLSL_VERSION` — GLSL profile macro (SPEC §9 V2.7, V2 T8)

The shader language level is decoupled from the llvmpipe ceiling via the
single macro **`RE_GLSL_VERSION`** (`core/glsl_version.hpp`): **450 = portable
floor** (tests/CI, llvmpipe caps at GLSL 4.50) and **460 = hardware floor**
(native d3d12 / desktop GL with full 4.6). This is the **single `#version`
concern** now that shaders live in files (T7) — the macro is the authoritative
version; every `.glsl` file's first line is verified to equal the macro's
`RE_GLSL_VERSION_LINE`, so a version bump updates the macro and the files'
first lines in lockstep.

| Macro | Value in gate env | Purpose |
|---|---|---|
| `RE_GLSL_VERSION` | `450` | integer version: `450` (portable, llvmpipe) or `460` (hardware) |
| `RE_GLSL_VERSION_STRING` | `"450"` | stringified version (`RE_GLSL_DETAIL_XSTR`) |
| `RE_GLSL_VERSION_LINE` | `"#version 450 core"` | full `#version` line; every `.glsl` file heads with this line |

**Default + override.** The header defaults to `450` when no override is
supplied, so the gate/CI build is the portable floor without extra flags. For
hardware builds pass `-DRE_GLSL_VERSION=460` or `-DRE_FORCE_GLSL_460`; the
header stringifies the integer into `RE_GLSL_VERSION_LINE` via
`RE_GLSL_DETAIL_XSTR`, so the line stays in sync with the integer by construction.
A `static_assert` in the header rejects any value other than `450` or `460`.

**Single concern in practice.** Every `.glsl` file under `render/shaders/`
(including the `fixtures/` malformed helpers) heads with the line produced by
`RE_GLSL_VERSION_LINE` — `head -n1 render/shaders/*.glsl` is uniformly
`#version 450 core` in the gate tree (verified by the T8 gate). A fixture shader
whose `#version` line is produced by the macro
(`std::string(RE_GLSL_VERSION_LINE) + body`) compiles on the llvmpipe 4.6 core
context (which accepts GLSL 4.50), while `#version 460` is the target for
hardware-driven sample shaders on the native d3d12 path (SPEC §8). The
460/hardware compile is a **manual sample verification**, not a gate assertion,
because llvmpipe caps at GLSL 4.50 (`GLSL 4.60 is not supported`).

**Gate (V2 T8).** In the gate env the macro expands to `#version 450` — the
test `tests/t8_v2_glsl_version_test.cpp` asserts `static_assert(RE_GLSL_VERSION
== 450)`, `RE_GLSL_VERSION_LINE == "#version 450 core"`, compiles the
macro-generated fixture shader on llvmpipe, and checks that every shader file's
first line equals `RE_GLSL_VERSION_LINE`.

### `render/types.hpp` and the `IRenderer` contract (SPEC §9 V2.3, V2 T1)

The shared render-types header: the types every renderer shares plus the narrow
pure-abstract dispatch contract that the multi-view workstream (V2 T2, SPEC §9
V2.4) drives ("RE dispatches objects to the correct renderer via IRenderer").

| Type | Purpose |
|---|---|
| `Camera` | `view` / `proj` matrices + `position` (eye, for view-direction terms). **Moved here from `mesh_renderer.hpp`** so a file that needs only the shared types no longer pulls in the whole mesh renderer. |
| `RenderTarget` | a `core::Framebuffer` + pixel size + clear color (SPEC §3). The framebuffer is color-only by default (the deterministic-gate configuration, SPEC §6 / docs/core.md) but MAY carry an optional depth attachment when it belongs to a depth-enabled `ViewTarget`; direct renders always keep the depth test off. A null framebuffer means the window's default framebuffer (samples). **Moved here from `mesh_renderer.hpp`**. |
| `Scene` | `std::variant<const MeshScene*, const PlaneScene*, const VolumeScene*, const SliceScene*>` — the dispatch payload: a pointer to the scene of any of the four techniques. The variant holds **pointers** because a `std::variant` of forward-declared *value* types does not compile (GCC/libstdc++); the scene structs stay defined in their renderer headers and are forward-declared here. Scenes are owned by `app/` and passed by pointer, matching the stateless-renderer model (SPEC §3). |
| `IRenderer` | the pure abstract contract: `data::Result<void> render(const Scene&, const Camera&, const RenderTarget&) = 0`. Implemented by `MeshRenderer`, `PlaneRenderer`, `VolumeRenderer`, and `SliceRenderer`. |

**Dispatch semantics.** Each renderer's `IRenderer::render` extracts its own
scene type from the variant (`std::get_if`, never `std::get` — the
no-exceptions rule, SPEC §5) and forwards to the concrete
`render(scene, camera, target)`; a scene holding a different technique returns
a typed error (code 2, message naming the expected scene type) instead of
throwing or crashing. The output through the interface is therefore
bit-identical to the direct concrete call (regression lock R3; verified by the
V2 T1 gate, constants below). `SliceRenderer`'s dispatch path slices against
the clip plane **carried by the scene itself** (`SliceScene::plane`) — the
narrow contract has no plane parameter — while the concrete 4-argument
`render(scene, camera, plane, target)` is unchanged.

#### Acceptance constants (V2 T1 dispatch gate, docs/render.md)

The four golden scenes dispatched through `IRenderer&` reproduce the exact
center pixels of their direct-call gates (R3):

| Scene dispatched | Renderer | Center pixel | Where it comes from |
|---|---|---|---|
| golden quad (mesh) | `MeshRenderer` | `{51, 102, 204}`, α `255` | FR-render.1 (the +Z-facing quad shades to the base color) |
| solid 64×64 image (plane) | `PlaneRenderer` | `{51, 102, 204, 255}` | FR-render.5 (the quad maps 1:1, sampling the solid texel) |
| 2×2×2 uniform volume, constant-green TF | `VolumeRenderer` | `{0, 239, 0, 239}` | FR-render.6 (`round(0.9375*255) = 239`) |
| golden cube, scene-carried plane z=0 | `SliceRenderer` | `{51, 102, 204}` | FR-render.4 (kept +Z face shades to the base color) |
| golden cube, scene-carried plane z=2 | `SliceRenderer` | `{0, 0, 0, 0}` (clear) | cube max z = 1 < 2, so every triangle is clipped away — proves the dispatch path uses the scene's plane |
| PlaneScene dispatched to `MeshRenderer` | — | typed error, code 2 | a scene of the wrong technique is rejected (SPEC §5, no exceptions) |
| null (default-constructed) `Scene` | every renderer | typed error, code 2 | the documented "no scene" payload is rejected, never a crash (SPEC §5) |
| `Scene` variant size | — | 4 | one alternative per technique |

### `scene::Camera` → `render::Camera` via `broker::CameraMapper` (SPEC §3.1, T4 V3.3)

The **scene-side camera** (`re::scene::Camera`, `scene/camera.hpp`) is the sole owner
of the manipulable camera (`pan`/`rotate`/`zoom`/`orbit`) and of the two factories
`makeOrthoForSlice` / `makePerspectiveCrosshair` (T4 V3.3). It is a **pure value type**,
GL-free and RE-free — only `glm` + standard library — and never includes a `render/`
header (guardrail `disposition_scene`). The renderer never sees `eye`/`center`/`up`
directly; it receives only the **translated matrices + position**:

```
scene::Camera{eye,center,up, FOV/aspect/near/far or ortho bounds, viewGen/projGen}
  → broker::CameraMapper::map(camera, TranslateContext{viewPlane})
  → render::Camera{view, proj, position}
```

| Member | Source | Where it comes from |
|---|---|---|
| `view` (`glm::mat4`) | `scene::Camera::viewMatrix()` = `glm::lookAt(eye, center, up)` | `pan`/`rotate`/`zoom`/`orbit` mutate `eye`/`center`/`up` and bump only `viewGen` |
| `proj` (`glm::mat4`) | `scene::Camera::projMatrix()` — `perspective(FOV,aspect,near,far)` when `isPerspective()` else `ortho(l,r,b,t,near,far)` | `setPerspective` / `setOrtho` / factories bump only `projGen` (per-field `viewGen`/`projGen` split, SPEC §10.4) |
| `position` (`glm::vec3`) | `scene::Camera::eye()` | camera eye (world space) |

**Factories (T4):**

| Factory | Projection | Eye / Up | Gate constant |
|---|---|---|---|
| `makeOrthoForSlice(center, planeNormal, distance)` | `Orthographic` (`ortho(-1,1,-1,1,0.1,100)`) | `eye = center - normalize(planeNormal)*distance`, `up` orthogonal to plane normal (fallback `(1,0,0)` when `planeNormal≈(0,1,0)`) | `proj == glm::ortho(-1,1,-1,1,0.1,100)`; `view == lookAt(eye,center,up)` |
| `makePerspective(center, distance, fovDeg=45, aspect=1)` | `Perspective` (`perspective(fovDeg,aspect,0.1,100)`) | `eye = center + (0,0,distance)`, `up=(0,1,0)` | `proj == glm::perspective(radians(fovDeg),aspect,0.1,100)` |
| `makePerspectiveCrosshair(center, distance, fovDeg=45, aspect=1)` | `Perspective` — alias for MPR 3D crosshair | identical to `makePerspective` | same as above (T4 gate uses crosshair name) |

**Validation (T4):** `broker::CameraMapper::map` checks `TranslateContext::hasPlane()`:

- `hasPlane()==true` (2D slice view) → camera must be `isOrthographic()==true`; otherwise typed error code `4` (`plane present → ortho`).
- `hasPlane()==false` (3D) → camera must be `isPerspective()==true`; otherwise typed error code `4`.

This keeps `2D` ortho vs `3D` perspective deterministic (gate uses one ortho + one perspective case) and enforces that `scene/` never leaks a `render::Camera` type (the mapper is the only place that includes both headers — `broker/` ACL, SPEC §11).

**Per-field generation (T4):** `Camera::orbit(deg,axis)` and `pan`/`rotate`/`zoom` bump only `viewGen`; `setPerspective` / `setOrtho` bump only `projGen`. `CameraMapper` caches per `(viewGen,projGen)` so a pure orbit dirties only the view cache entry (gate asserts `viewGen` +1, `projGen` unchanged).

#### Acceptance constants (T4 gate, docs/render.md)

| Quantity | Value | Where it comes from |
|---|---|---|
| `orbit(90°, (0,1,0))` view matrix | `lookAt((5,0,0),(0,0,0),(0,1,0))` | offset `(0,0,5)` rotated 90° about Y → `(5,0,0)`; within 1e-6 |
| `2D` plane+`makeOrthoForSlice` | `proj == glm::ortho(-1,1,-1,1,0.1,100)` | ortho factory deterministic; mapper with `hasPlane()==true` succeeds |
| `3D` `makePerspectiveCrosshair` | `proj == glm::perspective(radians(45),1,0.1,100)` | perspective factory deterministic; mapper with `hasPlane()==false` succeeds |
| `2D` plane + perspective camera | typed error code `4` | `plane present → ortho` violation |
| `3D` no-plane + ortho camera | typed error code `4` | `no plane → perspective` violation |
| `orbit` gen split | `viewGen` +1, `projGen` unchanged | per-field split invariant |

### View (ReView) — per-screen-section `ViewTarget` + heterogeneous `IRenderable` list + `core::blit` (SPEC §3.2 V3.4 T5)

The **T5 deliverable** (SPEC §3.2, V3.4): `ViewRenderer` is deleted; each screen
section is a `render::View` (`ReView`) that owns **one `ViewTarget`**
(`Texture2D+Framebuffer` sized `rect.w×h`) + a `Camera` +
`optional<ClipPlane>` (`2D` when present, `3D` when `nullopt`) +
`list<IRenderable>` (`VolumeSlice+MeshSlice` for `2D`, `Volume+Mesh` for `3D`).
Each `IRenderable` is type-erased `drawLayer(Camera,DrawContext&)` — `View`
never knows the renderer. Each renderer (`Mesh/Plane/Volume/SliceRenderer`)
gains `drawLayer(SceneT,Camera,DrawContext&)` that assumes `ReView` already
`bind+viewport+clear` via the same `DrawContext`; the single-item
`render(SceneT,Camera,RenderTarget)` keeps its own `clear` for direct tests.
No app-side viewport blending: the engine `View::blitTo(destination)` is the
present. The MPR sample's 2×2 grid (docs/mpr.md) will be driven through this
`ReView`/`ViewTarget` path (T14/T15 + T5).

| Type | Purpose |
|---|---|
| `ViewRect` | a window-section rectangle in GL pixel coordinates (origin bottom-left, matching `core::setViewport`): `x`/`y`/`width`/`height`. The per-view window-section handle the app shares with the engine (still in `render/types.hpp`). |
| `ViewTarget` | per-view FBO: the color-attachment `core::Texture2D` plus the `core::Framebuffer` that renders into it (textures stay alive for framebuffer lifetime), plus an OPTIONAL `DEPTH_COMPONENT24` depth-attachment texture when created with `DepthMode::Enabled` (`hasDepth()`/`depth()`; default `DepthMode::ColorOnly`). Sized `rect.w×h`; an enabled-depth target asserts framebuffer completeness WITH its depth attachment at creation, and `resize()` preserves the mode. `View` delegates FBO lifecycle to it (SRP via composition). |
| `IRenderable` | type-erased draw (`render/i_renderable.hpp`): `virtual Result<void> drawLayer(Camera,DrawContext&)=0`. Each renderer provides `drawLayer(SceneT,Camera,DrawContext&)` assuming already bound+cleared; `View::addItem<SceneT>(SceneT,Renderer*)` wraps it. `View` never knows the renderer (DIP/OCP). |
| `View` (`ReView`) | one per screen section: the `ViewRect`, the per-view `Camera`, the `optional<ClipPlane>` (`2D` vs `3D`), the `clearColor`, the per-view `depthTest` flag (default false — see "Depth support" below), the owned `ViewTarget` (`rect.w×h`), and the heterogeneous `vector<IRenderable>`. `ensureTarget()` creates/recreates the `ViewTarget` when size OR depth mode changed; `render(ctx)` binds the FBO, runs the shared prologue (clear + depth state per the flag) then iterates `drawLayer` without clearing between layers; `blitTo(destination)` copies the FBO into its pinned window rect via `core::blit`. Alias `ReView` kept for grep distinctness where both `scene::View` and `render::View` are in scope. |
| `core::blit` | the `core/` wrapper around `glBlitFramebuffer` (guardrail `gpu_api_ownership`: raw GL call lives in `core/draw.cpp`). Copies a color pixel rectangle from a source FBO to a destination framebuffer (`nullptr` = default framebuffer 0), GL_NEAREST, scaled to the destination rect. v1 FBOs are color-only, so only `GL_COLOR_BUFFER_BIT` is blitted. |

**Compositing semantics (View side).** `View::render(ctx)` uses the `DrawContext`
instance passed by the caller (per-frame, SRP via instance — SPEC §11.6 EOL-5) to
run the shared prologue exactly once — `setViewport(0,0,w,h)` / `setClearColor` /
`clearColor`, then the depth branch (`disableDepthTest` by default; `enableDepthTest`
+ `clearDepth` when this view's `depthTest` flag is true) / `disableBlend` — and then
calls each `IRenderable::drawLayer(camera, ctx)`
without clearing between layers — no second layer clears away the first. The
single-item `Renderer::render(scene,camera,target)` retains its own
`bind+viewport+clear` for direct tests (regression lock). A `View` with zero
items still clears to `clearColor`.

**Blit semantics (exact, pixel-for-pixel).** `core::blit` copies
`(srcX, srcY, srcWidth, srcHeight)` of the source FBO to
`(dstX, dstY, dstWidth, dstHeight)` of the destination with GL_NEAREST. Both
framebuffers share the GL y-up convention, so the copy is a direct 1:1 transfer
when the sizes match — no vertical flip, no filtering — which is what makes the
gate's center-pixel assertions exact: each view's FBO content lands
pixel-for-pixel at its pinned window rect position. `View::blitTo` calls
`core::blit(target.framebuffer(),0,0,w,h,destination,rect.x,rect.y,rect.width,rect.height)`.

#### Acceptance constants (T5 gate via `ReView`/`ViewTarget`, docs/render.md — same as V2 T2, now via `ReView`)

A 2-view layout in a **1280×480** window; each view's `ViewTarget` is 640×480 (equal to
its rect, so the blit is 1:1):

| Quantity | Value | Where it comes from |
|---|---|---|
| Window size | `1280×480` | the gate's pinned 2-view window |
| View A rect | `(0, 0, 640, 480)` | pinned (task layout) |
| View B rect | `(640, 0, 640, 480)` | pinned (task layout); the two rects exactly tile the window |
| View A scene | MeshScene: FR-render.1 golden +Z quad, base `{0.2, 0.4, 0.8, 1.0}` | renders `{51, 102, 204}` at the FBO center (front-facing, shade 1) |
| View B scene | PlaneScene: 640×480 solid image `{0.9, 0.1, 0.3, 1.0}` | texel bytes `{round(0.9·255)=230, round(0.1·255)=26, round(0.3·255)=77}`; quad maps 1:1, center samples the solid texel |
| View A `ViewTarget` center `(320, 240)` | `{51, 102, 204}`, α `255` | view A's `ViewTarget` `render(ctx)` + `drawLayer` via `MeshRenderer` into its OWN FBO (per-view FBO proof) |
| View B `ViewTarget` center `(320, 240)` | `{230, 26, 77}`, α `255` | view B's `ViewTarget` via `PlaneRenderer` into its own FBO |
| Window pixel `(320, 240)` | `{51, 102, 204}` (±1) | `View::blitTo` places view A's FBO (320,240) at window (320,240) — its pinned rect center |
| Window pixel `(960, 240)` | `{230, 26, 77}` (±1) | `View::blitTo` places view B's FBO (320,240) shifted by rect origin (640,0) |
| Window pixel `(639, 240)` | `{51, 102, 204}` (±1) | last pixel of rect A — the split is pinned exactly at x = 640 |
| Window pixel `(640, 240)` | `{230, 26, 77}` (±1) | first pixel of rect B |
| `View::render` with empty `ViewTarget` | typed error, code 2 | target not yet `ensureTarget`-ed — rejected instead of drawing into uncreated FBO (SPEC §5) |
| `View::blitTo` before `ensureTarget`/`render` | typed error, code 3 | per-view FBO not created — rejected instead of blitting unrendered target (SPEC §5) |

> **V2 T2 historic note.** The V2 T2 gate (Model B: per-view FBO + engine blit via `ViewRenderer`) used the same 1280×480 / 640×480 constants but dispatched through `IRenderer` + `Scene` variant via `ViewRenderer{setRenderer,renderViews,present}`. T5 replaces that compositor with `ReView`/`ViewTarget` + `IRenderable` type erasure + `drawLayer`; `ViewRenderer` + `render/types.hpp` `Scene` raw-pointer `View` struct are deleted. The `Scene` variant in `render/types.hpp` remains for single-item `render()` direct tests until `AssetId` handles replace it in T7.

### Depth support — opt-in per view (`DepthMode`, `View::setDepthTest`)

Architecture-review finding closed by the depth task: v1 framebuffers were
**color-only everywhere**, which forced painter's-order workarounds (the MPR
box emits its faces so the last-drawn near face wins at each pixel) and blocked
opaque meshes under order-independent transparency. The fix is opt-in and
strictly additive:

- **Color-only stays the default — the deterministic-gate configuration.**
  Every analytic pixel gate of the suite renders through color-only targets
  whose output is painter's-order and therefore reproducible on software GL
  (llvmpipe). A default-constructed view or `ViewTarget::create(w, h)`
  allocates no depth attachment, so no existing gate can drift.
- **`render::ViewTarget` + `DepthMode::Enabled`.** Creating a target with
  `DepthMode::Enabled` additionally owns a `GL_DEPTH_COMPONENT24` texture
  (`core::Texture2D::uploadDepth`) attached at `GL_DEPTH_ATTACHMENT`
  (`core::Framebuffer::attachDepth`). The creation-time completeness check
  then covers BOTH attachments, so an enabled-depth target asserts framebuffer
  completeness WITH its depth attachment — an environment that cannot provide
  it fails loudly at creation instead of silently rendering without occlusion.
  `resize()` preserves the mode; `hasDepth()`/`depth()` expose it.
- **`render::View::setDepthTest(bool)` — per-view flag.** When true,
  `ensureTarget()` creates/recreates the inner target as depth-enabled and the
  pass prologue enables the depth test and clears it to 1.0; when false
  (default) the prologue disables depth exactly as before. Flipping the flag
  recreates only the inner target (the View object persists). Direct
  single-item renders are unaffected: they keep the deterministic depth-off
  pass.
- **OIT is untouched.** Both `LinkedListOIT` passes keep the depth test off
  exactly as they always did, so transparent compositing produces identical
  bytes on color-only AND depth-enabled targets — the depth attachment simply
  sits unused behind them. Mechanism: the capture draws only inside
  `MeshRenderer::render`, immediately after its default depth-off `beginPass`
  prologue (and `View` layers never engage the pipeline), while the composite
  issues its own explicit `core::disableDepthTest()`. The OIT-sample work this
  unblocked has landed (T19): the sample composes the two halves itself — a
  depth-tested opaque View pass plus the depth-off pipeline driven over the
  same target; see "The OIT sample composition" above for that contract.

#### Acceptance constants (depth gate, docs/render.md)

Two full-screen opaque quads facing +Z at different depths, drawn in
ANTI-painter order (nearer FIRST, farther LAST), camera at `(0,0,5)`,
orthographic `[-1,1]²` onto a 64×64 view:

| Quantity | Value | Where it comes from |
|---|---|---|
| Nearer quad (world z=0, distance 5), base `{0, 0.5, 0, 1}` | bytes `{0, 128, 0, 255}` | front-facing flat headlight shades at exactly base color; `round(0.5*255)=128` |
| Farther quad (world z=-1, distance 6), base `{0.5, 0, 0, 1}` | bytes `{128, 0, 0, 255}` | same shading rule |
| Overlap pixel, `depthTest = true` | `{0, 128, 0, 255}` (±1) | true occlusion: the NEARER mesh wins even though it was drawn FIRST (depth test on, cleared to 1.0 per pass) |
| Overlap pixel, default color-only pass | `{128, 0, 0, 255}` (±1) | painter's order: the LATER-DRAWN (farther) mesh wins — proves the probe discriminates the two semantics (the expected colors differ by 128 in R vs G, far outside 1/255) |
| Depth-enabled target | `isComplete() == true` with `GL_DEPTH_ATTACHMENT` bound | completeness asserted WITH the depth attachment at creation (`ViewTarget::create`, `DepthMode::Enabled`) |
| Default target / view | no depth attachment (`hasDepth() == false`) | color-only default untouched — every prior FBO gate keeps its exact configuration |
| Flag flip / resize | `setDepthTest(true)` → next `ensureTarget()` owns a depth attachment; `setRect` resize preserves it | mode-mismatch recreate path; `resize()` preserves `DepthMode` |
| OIT composite through a depth-enabled target | `{56, 56, 28, 179}` (±1); captured fragments `64·64·2 = 8192` | same analytic two-transparent-quad blend as FR-render.2 — both OIT passes run depth-off unchanged, so bytes are identical to the color-only run |

Gate: `tests/t18_depth_test.cpp` (N>=3 consecutive green runs).

### The unified asset store: `AssetRegistry` (SPEC §9 V2.5 mesh kind, SPEC §7 T14 volume/image/material kinds)

The **asset system of the render layer**: one registry instance owns exactly
**ONE GPU object per distinct asset CONTENT, globally** across every renderer
that resolves through it — four kinds share the same generational,
content-hash-deduped, reference-counted contract:

| Kind (CPU → GPU) | Register | Resolve | Release |
|---|---|---|---|
| `data::Mesh → MeshGeometry` | `registerAsset(mesh)` | `resolve(handle)` | `unregister(handle)` |
| `data::VolumeDataset → core::Texture3D` | `registerVolume(shared_ptr)` | `resolveVolume(handle)` | `unregisterVolume(handle)` |
| `data::Image → core::Texture2D` | `registerImage(shared_ptr)` | `resolveImage(handle)` | `unregisterImage(handle)` |
| `PhongMaterial value → canonical IMaterial` | `registerMaterial(shared_ptr)` | `resolveMaterial(handle)` | `unregisterMaterial(handle)` |

Scenes carry **copyable generational handles** instead of raw CPU pointers, and
handles are the currency views exchange. Registering the same content twice —
the same CPU object, or two distinct allocations with identical bytes — yields
ONE GPU object: dedup is by the content hash of stable bytes, never by pointer
identity. This fixes both the pre-V2 MeshRenderer+SliceRenderer double-upload
of a mesh and (T14) the per-renderer double-upload of volumes/images: two
`VolumeRenderer` instances rendering one dataset share one `Texture3D` GL id.
The material kind extends the same dedup to material VALUES (every
`PhongMaterial` field — baseColor RGBA, specular RGB, shininess, ambient,
diffuse — participates in the identity hash), so identical Phong parameters
share one immutable store-owned canonical instance; the scene→RE material
hand-off that consumes it is the §12.2 `MaterialMapper` work tracked with the
broker mapper inventory.

| Type / member | Purpose |
|---|---|
| `AssetHandle` | copyable `{index, generation}` handle into the mesh table. Cheap to copy; `{0, 0}` is the reserved **null handle** (`isNull()`) — real handles carry `generation >= 1`. |
| `VolumeTextureHandle` / `ImageTextureHandle` / `MaterialHandle` | T14 handles for the volume/image/material tables with the same contract plus the slot's `contentHash` (the three-field shape of the app-side `scene::AssetId`) — a fabricated handle with the right index+generation but the wrong hash resolves to a typed error. |
| `AssetRegistry::registerAsset / registerVolume / registerImage / registerMaterial` | upload the GPU object once and take ONE REFERENCE on its slot; registering already-present content returns the EXISTING handle and increments the reference count (`slotCount()` unchanged). Returns a typed error if the upload fails (no GL context) or — volume/image/material kinds — the shared pointer is null (code 4). Named `register*`, not `register` — `register` is a C++ reserved keyword. |
| `AssetRegistry::resolve / resolveVolume / resolveImage / resolveMaterial` | return the handle's live GPU object. A **stale/dangling handle** — out-of-range index (code 1), generation mismatch (code 2: freed, reused, or fabricated), wrong content hash (code 2, volume/image/material kinds) — returns a **typed error, never a crash** (SPEC §5). |
| `AssetRegistry::unregister / unregisterVolume / unregisterImage / unregisterMaterial` | release one reference. At the LAST reference the GPU object is destroyed and the slot's generation bumped, so every outstanding handle goes stale immediately, and the slot becomes reusable (a later registration reuses the index with a fresh generation). With references outstanding, release only decrements — co-owned assets survive other owners' releases. |
| `AssetRegistry::lookupVolume / lookupImage` | the renderers' lazy path: find-or-upload by content hash WITHOUT changing any reference count. A miss leaves the entry store-pinned (zero references) until an owner claims it via register and releases it later; after full invalidation the next lookup re-uploads fresh — stale GPU data can never be served for freed content. |
| `AssetRegistry::volumeRefs / imageRefs / materialRefs` | live reference count of a slot (gate evidence for the ref-counting contract). |
| `AssetRegistry::shared() / resetShared()` | the process-wide default instance behind the volume/plane renderer constructor defaults — two default-constructed renderers therefore share one GPU object per content (the T14 invariant). `resetShared()` destroys it while a GL context is current (test-fixture teardown); the next `shared()` recreates it empty. |

**Generational safety.** Every slot's generation starts at 1 and is bumped each
time the slot is freed (and again when a freed slot is reused). A handle is
valid only while its fields exactly match the slot's — so a dangling handle is
detected at resolve time and surfaced as a typed error, never a dereference of
freed memory. Resolved GPU-object pointers stay valid until the slot's last
reference drops (each slot owns its object); the renderers resolve per draw and
never retain pointers across frames.

**Renderer integration.** All four technique renderers hold a
`std::shared_ptr<AssetRegistry>` (T13 shared ownership, so declaration order
can never dangle it; a null store fails per draw with typed error code 4):
Mesh/Slice/Contour since V2/T5 via explicit injection, Volume/Plane since T14
(defaulting to `AssetRegistry::shared()`). Mesh-family renderers resolve scene
`AssetHandle`s; the volume/plane renderers lazily resolve datasets/images by
content hash through `lookupVolume`/`lookupImage` — the registry, not the
renderer, owns every GPU asset, which is what makes dedup global.

#### Acceptance constants (V2 T3 + T14 asset-store gates, docs/render.md)

| Quantity | Value | Where it comes from |
|---|---|---|
| Same `data::Mesh` registered twice (via the MeshRenderer path + the SliceRenderer path) | `slotCount() == 1` | the registry dedups by content hash of stable bytes: one GPU object per distinct content (SPEC §9 V2.5 + T7) |
| The two registration handles | equal (`{index, generation}` identical) | `registerAsset` of already-present content returns the existing handle and takes another reference |
| Both handles resolve to | the same **non-zero** GL object id (`MeshGeometry::vaoId()`) | one GPU object behind both handles; GL reserves 0, so a live VAO name is non-zero |
| Two DISTINCT meshes with identical bytes | `slotCount()` stays at 1, same VAO id | content-hash dedup (T7): identical stable bytes alias to one GPU object |
| Stale `{index, generation+1}` lookup | typed error, code 2, message contains `stale` | generation mismatch (fabricated stale handle) |
| `{index, 0}` lookup | typed error | generation 0 is the never-allocated marker (null handle) |
| Handle after `unregister` of its last reference | typed error (code 2) | the freed slot's generation is bumped at free time; unregistering it again is also a typed error |
| Freed-slot reuse for a NEW mesh | same index, **new generation**; old handle still stale, new handle resolves | slot reuse issues a fresh generation — the generational mechanism |
| Same dataset through TWO `VolumeRenderer` instances | one `Texture3D` GL id; center pixel `{0,239,0,239}` ±1 from both | FR-render.6 analytic constant; the T14 gate asserts pointer/id equality through the shared store |
| Identical-content distinct-pointer volumes/images | one slot (`volumeSlotCount()/imageSlotCount() == 1`) | content-hash dedup (T14): voxel/pixel bytes are the key, never the address |
| Volume refs: register ×2 → release → release | refs `1 → 2 → 1`, freed at second release | the reference-counting contract: the last release destroys the GPU object and bumps the generation |
| Stale volume/image handle after full release | typed error, code 2, no crash | generational invalidation at free time |
| Out-of-range index lookup | typed error, code 1 | index beyond the slot table |
| Fabricated wrong-`contentHash` handle (volume/image/material) | typed error, code 2 | handles bind `(index, generation, contentHash)` — the `scene::AssetId` shape |
| Lazy lookups (`lookupVolume`/`lookupImage`) | reference count unchanged | renderer-path find-or-upload never claims ownership |
| Lookup after full release | succeeds with a fresh valid texture; old handle stays dead (code 2) | content-addressed recovery — no stale GPU data for freed content |
| Identical-value distinct-pointer `PhongMaterial`s (base `{0.2,0.4,0.8,1}`, shininess `32`) | one slot (`materialSlotCount() == 1`), equal handles, canonical `baseColor()` bit-exact | material kind value dedup (T14): every Phong field participates in the hash; the canonical is a byte-exact clone |
| Differing shininess or differing alpha | new slot each (`materialSlotCount() == 3` after the two negative controls) | alpha drives `isTransparent()` (FR-render.3 ⇔ a<1), so it must participate in material identity |
| Stale handle inside a rendered mesh scene | `render()` returns the typed error, no crash | the renderer propagates the resolve error (SPEC §5) |
| Mesh + Slice renderers drawing one shared handle | both center pixels `{51, 102, 204}` (±1), `slotCount()` still 1 | FR-render.1/4 analytic center pixels (regression lock R3) — see the FR-render.1/4 tables below |

### `IMaterial` (`render/imaterial.hpp`)

The modular material abstraction (SPEC §1 "Materials", §3). Renderers depend on
this interface, never on a concrete material class, so additional models (PBR,
toon, …) can be added without touching the renderer core (open/closed,
dependency inversion).

**Transparency is a material property.** The interface exposes:

- `isTransparent()` — true when the material has transparency (alpha < 1) and
  therefore must be composited order-independently;
- `baseColor()` — the straight (non-premultiplied) RGBA base/diffuse color;
  its alpha carries the material's opacity (alpha == 1.0 is opaque).

> **V3.7 (T8) — Phong-only stays (deferred, binding).** This iteration keeps
> `render::IMaterial → PhongMaterial` single path (FR non-goal `SPEC §1` —
> PBR deferred) and no `ILight` (fixed headlight `max(dot(n,(0,0,1)),0)` in
> `MeshRenderer` stays). Even hierarchies `IColor/IVolume/ILineMaterial +
> PBR/SliceMaterial/ContourMaterial` (§12.2) and
> `Directional/Point/Spot` (§12.3) are **deferred** — headers not added;
> no new `render/material/` files this iteration (`G` enforces). `TransferFunction`
> stays **beside** `VolumeMaterial` in `VolumePresentation` (already decided
> §12.5) — `VolumeInstance` still carries the `TransferFunction` as its own
> field, strictly separate from the dataset (T13: owned by value — a small
> immutable ramp copied per instance; the separation, not the pointer shape,
> is the §12.5 invariant). `MaterialDesc`/`LightDesc` remain `app`-local free
> structs for the `MPR` sample.

### `PhongMaterial` (`render/phong_material.hpp`, `.cpp`)

The v1 Phong material model. Holds the classic Phong parameters — an ambient
factor, a diffuse factor, a specular color, and a shininess exponent — plus the
straight RGBA base color. `isTransparent()` is derived from the base color's
alpha channel (`alpha < 1.0`), so transparency is a material property carried
by the color itself.

- `PhongMaterial({r,g,b,a})` clamps the base color to `[0,1]`.
- `isTransparent()` returns `baseColor().a < 1.0f` (FR-render.3: an alpha of
  `1.0` is opaque; `0.5` and `0.0` are transparent).

**RE-minimal `Re*` note (T8 V3.7, SPEC §12.4).** `Re*` keeps only RE-direct
values (`AssetHandle`/`ReMaterial*`/`ClipPlane`/`ReLight[]`/`worldBounds`/
`sliceUVW` where derived), never verbatim `app::MaterialDesc`. `ReMeshObject`
carries `AssetHandle`+`model`+`bounds`+`ReMaterial*` only; `ReVolumeObject`
carries `VolumeMaterial*` + `ReTfUniforms` separately (TF not owned by material
— ISP per §12.5). The binding inventory `docs/re_scene_inventory.md` (T9) will
enumerate every `Re*` field with rationale `derived|uniform-ready|handle`
(`asset_indirection` guardrail).

### `ITransparencyPipeline` (`render/itransparency_pipeline.hpp`)

The swappable OIT interface (SPEC §3 "OIT is a characteristic, not a peer
renderer"). `MeshRenderer` auto-engages an injected pipeline **only when some
mesh's material is transparent** (FR-render.3); an opaque-only scene never
engages it. The interface lives here so an injectable spy (test double) can
confirm the pipeline stays off for opaque scenes.

The interface exposes the three lifecycle calls `MeshRenderer` drives:

- `begin(camera, target)` — prepare capture storage for the frame (called
  before the opaque pass);
- `drawTransparent(geometry, baseColor, model, camera)` — capture one
  transparent mesh (called per transparent mesh between begin/end);
- `end(camera, target)` — depth-sort + composite over the target's opaque
  contents;
- `isEngaged()` — true between begin() and end().

The renderer depends only on this abstraction (dependency inversion, SPEC §3):
a stub or spy implementation drives the same `MeshRenderer` unchanged (the T10
gate asserts exactly this, FR-render.3).

**Typed errors (SPEC §5).** Every lifecycle call returns `data::Result<void>`:
a storage-allocation, shader-build, or draw-issue failure surfaces as a typed
error that `MeshRenderer::render` propagates to the caller — never a silent
disengage. On `begin()` failure the pipeline stays un-engaged and
`MeshRenderer::render` aborts the frame (the target is left cleared) so the
caller can handle the error instead of receiving a partial frame.

### `LinkedListOIT` (`render/linked_list_oit.hpp`, `.cpp`) — T10

The **v1 order-independent transparency pipeline** (SPEC §3, FR-render.2/3): a
classic **per-pixel linked list**, implemented as capture → depth-sort →
composite:

1. **Capture pass.** Each transparent mesh is drawn through the capture program.
   The fragment shader
   - premultiplies the material's straight RGBA base color
     (`color.rgb = baseColor.rgb * baseColor.a`, `color.a = baseColor.a`);
   - atomically allocates a node index (`atomicAdd` on a counter SSBO);
   - links the node into the pixel's linked list by swapping the pixel's head
     pointer (`imageAtomicExchange` on an R32UI head-pointer texture);
   - stores the node (premultiplied color, `gl_FragCoord.z`, next pointer) into
     a node SSBO.
   The capture shader writes **no color output**, so the target framebuffer's
   opaque contents are preserved.
2. **Memory barriers.** `core::memoryBarrierShaderStorage()` is issued between
   capture draws and before the composite pass so the atomic SSBO/image writes
   are visible (the gate's llvmpipe driver requires an explicit barrier between
   draws that perform atomic SSBO operations).
3. **Composite pass.** A full-screen pass reads each pixel's linked list from
   the head-pointer texture, collects up to `maxFragmentsPerPixel` nodes,
   **insertion-sorts by depth (near → far)**, then composites **back-to-front**
   with the premultiplied-alpha "over" operator over black:
   ```
   acc.rgb = s.rgb + (1 - s.a) * acc.rgb
   acc.a   = s.a   + (1 - s.a) * acc.a
   ```
   The composite shader outputs the accumulated premultiplied transparent
   color, which is blended over the target's opaque contents with the fixed
   "over" blend state `(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)` — so the transparent
   fragments are composited **over** the opaque geometry in the correct depth
   order, regardless of draw order.

The pipeline owns only GL resources (SPEC §3 "Stateless renderers"): the two
shader programs, a shared full-screen quad VAO, one R32UI head-pointer texture,
and two SSBOs (node buffer sized `width * height * maxFragmentsPerPixel`,
counter buffer). Storage is (re)allocated in `begin()` when the target size
changes. `readCapturedFragmentCount()` is a **test-consumed readback**
(guardrail `no_production_readback`): the render path never reads back from the
GPU; the FR-render.2 gate calls it after `end()` to observe the node-allocator
counter.

**Guardrail note.** The v1 pipeline uses SSBO `atomicAdd` (node allocator +
counter) and `imageAtomicExchange` on the head-pointer texture. Both are
supported by the gate's llvmpipe driver; bare `atomicExchange`/`atomicCompSwap`
on SSBOs are **not** (llvmpipe reports `GL_INVALID_OPERATION`), which is why the
head pointers live in a texture (image atomics), not an SSBO.

**Deterministic shade.** The capture shader does **not** evaluate lighting: it
stores the material's premultiplied base color directly. For a front-facing
quad under the v1 opaque-pass light (head-on, shade factor 1, see `MeshRenderer`
below) this equals the opaque pass's shaded color, so the captured fragment is
exactly the base color premultiplied — keeping the FR-render.2 acceptance fully
analytic.

#### Acceptance constants (FR-render.2/3, docs/render.md)

Two full-screen +Z-facing transparent quads at known depths, orthographic
camera mapping NDC `[-1,1]^2` onto the full 64×64 viewport:

| Quantity | Value | Where it comes from |
|---|---|---|
| Near quad (world z=0) material | `{0.4, 0.2, 0.1, 0.5}` | straight RGBA; premultiplied = `{0.20, 0.10, 0.05, 0.5}` → bytes `{51, 26, 13, 128}` |
| Far quad (world z=-1) material | `{0.1, 0.6, 0.3, 0.4}` | straight RGBA; premultiplied = `{0.04, 0.24, 0.12, 0.4}` → bytes `{10, 61, 31, 102}` |
| Center pixel (near-over-far) | `{56, 56, 28, 179}` (±1) | depth-ordered premult "over": `rgb = {0.2,0.1,0.05} + (1-0.5)*{0.04,0.24,0.12} = {0.22,0.22,0.11}`, `a = 0.5 + 0.5*0.4 = 0.7`; `round(0.22*255)=56`, `round(0.11*255)=28`, `round(0.7*255)=179` (FR-render.2, within 1/255) |
| Wrong order (far-over-near) | `{41, 77, 38, 179}` | `{0.04,0.24,0.12} + (1-0.4)*{0.2,0.1,0.05} = {0.16,0.30,0.15}`, `a=0.7` — outside the 1/255 tolerance, so the test discriminates depth ordering |
| Captured fragments | `64*64*2 = 8192` | both full-screen quads rasterize every pixel; the node allocator counts exactly 2 fragments per pixel |
| Opaque-only center alpha | `255` (== 1.0) | opaque material alpha passed through unchanged; pipeline never engaged (FR-render.3) |
| One transparent quad added | `beginCount == 1`, `drawTransparentCount == 1`, `endCount == 1` | pipeline flips on exactly for the frame (injectable spy, FR-render.3) |
| Stub pipeline drives renderer | same call counts with a no-op stub | interface is swappable; renderer depends only on the abstraction (FR-render.3) |

#### The OIT sample composition — real meshes over a depth-tested opaque pass (`app/oit_sample.cpp` + `app/oit_scene.hpp`, T19)

The sample scene is no longer transparent quads: it interleaves REAL meshes
along the view direction — two OPAQUE (a golden flat-shaded box from
`app::makeBoxMesh`, plus the committed Stanford bunny scaled to a 0.24-unit
longest AABB side) and two TRANSPARENT glass boxes (alpha 0.5, near red at
world z `[+0.72,+0.92]`, far blue at z `[-0.56,-0.36]`) whose footprints
overlap both opaque meshes and each other. The shared rig
(`app/oit_scene.hpp`) defines the arrangement ONCE for both the sample
executable and its gate, so the tested scene IS the shown scene.

Composition contract per frame (`oit_scene::composeFrame`):

1. **Opaque pass with true occlusion** — the opaque layer renders through a
   `render::View` whose `depthTest` flag is ON: the view target owns a depth
   attachment (`DepthMode::Enabled`, the T18 support) and the shared pass
   prologue enables + clears the depth test, so golden box and bunny resolve
   overlaps by depth rather than draw order.
2. **Depth handed back before capture** — `DrawContext::disableDepthTest()`
   runs on the SAME context instance that enabled it (its cache tracks the
   enable, so the raw disable always issues regardless of the global-function
   cache state), keeping both OIT passes in their established depth-off
   configuration.
3. **Capture + depth-sorted composite over the opaque result** — the pipeline
   captures every glass fragment into the per-pixel linked list, sorts by
   depth, and blends back-to-front over the opaque image inside the view
   target; `core::blit` then presents to the window.

v1 capture does not depth-cut: a glass surface BEHIND an opaque mesh still
composites over it (documented limitation — depth-cut capture would need a
depth-mask control). The arrangement therefore keeps every probe column's
glass surfaces strictly IN FRONT of the opaque base surface so the analytic
expectations stay exact; the bunny-in-front-of-the-far-shell relationship
remains part of the scene and is covered by an alpha==255 invariant probe.

Acceptance constants (T19 gate, 64×64 ortho `[-1,1]²`, camera `(0,0,5)`):

| Quantity | Value | Where it comes from |
|---|---|---|
| P1 fully-opaque region (pixel `(8,31)`) | `{217,115,38,255}` (±1) | gold `{0.85,0.45,0.15,1}`, flat +Z face shades at exactly base color: `round(0.85·255)=217`, `round(0.45·255)=115`, `round(0.15·255)=38` |
| P2 near-glass-over-opaque (pixel `(19,31)`) | `{226,67,48,255}` (±1) | near premult `{0.45,0.10,0.10,0.5}` twice (front+back face): acc `{0.675,0.15,0.15}`, `a=0.75`; over gold: `acc + 0.25·{0.85,0.45,0.15} = {0.8875,0.2625,0.1875}`, `a=1` |
| P3 far-over-near-over-opaque (pixel `(40,31)`) | `{195,62,84,255}` (±1) | full 4-fragment chain far→near: `{0.10,0.175,0.45}` → `{0.15,0.2625,0.675}` → `{0.525,0.23125,0.4375}` → `{0.7125,0.215625,0.31875}`, `a=0.9375`; over gold `+ 0.0625·{0.85,0.45,0.15} = {0.765625,0.24375,0.328125}` |
| Wrong sort order at P3 | `{95,84,184}` | reversed accumulation gives `{0.371875,0.328125,0.721875}` — far outside 1/255, so the probe discriminates depth ordering |
| Bunny-interior probe (pixel `(33,16)`) | alpha `255` only | an opaque surface lies under the ray (bunny body or gold box beneath), pinning composited alpha at exactly 1.0 regardless of the smooth-shaded RGB |
| Captured fragments per frame | exactly `5632` | pixel-center rule: each glass shell covers 32×44 = 1408 centers (near x∈[11,42], y∈[10,53]; far identical) and contributes 2 fragments (front +Z face, back −Z face; no culling, capture depth-off); `(1408+1408)·2` |
| Pipeline spy, opaque-only layer | `begin/drawTransparent/end == 0/0/0`, probe alphas `255` | no transparent material → pipeline never engages (FR-render.3) |
| Pipeline spy, full mixed scene | `begin==1`, `drawTransparentCount == 2`, `end==1` | one capture per transparent mesh — spy count equals the number of transparent meshes |
| Depth-enabled view target | `hasDepth() == true`, complete WITH the depth attachment | the opaque pass consumes the T18 `DepthMode::Enabled` support |
| Repeat-frame stability | frame 2 reproduces every probe byte | depth/blend state transitions are exact per frame (instance-cached `disableDepthTest` after the enabled prologue) |

Gate: `tests/t19_oit_sample_test.cpp` (N>=3 consecutive green runs).

### `MeshGeometry` (`render/mesh_geometry.hpp`, `.cpp`)

The **shared mesh geometry handling** used by every mesh-family renderer
(`MeshRenderer` now; `SliceRenderer` in T11). It uploads a `data::Mesh`
(positions + triangle indices) into `core/` RAII buffers (VBO/EBO/VAO) with
interleaved `(position, normal)` vertices, computing **per-vertex normals** as
the area-weighted average of the mesh's normalized face normals (a
smooth-shading approximation; exact for a flat face). A mesh is uploaded to the
GPU once and reused across instances and renderers.

- `MeshGeometry::create(mesh)` builds the GPU buffers (returns a typed error if
  no GL context is current).
- `draw()` issues the indexed triangle draw through `core::drawElements`.
- `vaoId()` returns the GL name of the geometry's vertex array object — the
  identity of the whole GPU-side geometry bundle, used as the "one GPU object"
  the asset registry dedups on (SPEC §9 V2.5).

### `MeshRenderer` (`render/mesh_renderer.hpp`, `.cpp`)

A **stateless opaque-forward-pass** renderer (SPEC §3 "Stateless renderers"):
`render(scene, camera, target)` receives all of its data per call. The renderer
owns only its cached opaque shader program; **GPU geometry lives in the shared
`AssetRegistry`** (SPEC §9 V2.5): scenes carry `AssetHandle`s and the renderer
resolves them through the injected registry, so one GPU object exists per
individual CPU mesh globally — shared with SliceRenderer and every view. It
implements the `IRenderer` dispatch contract (`render/types.hpp`, SPEC §9
V2.3): its `IRenderer::render` forwards a `Scene` holding a `MeshScene` to this
concrete method.

Public scene structs (`Camera`/`RenderTarget` live in `render/types.hpp`;
`MeshInstance`/`MeshScene` are defined in `mesh_renderer.hpp`):

| Type | Purpose |
|---|---|
| `Camera` | `view` / `proj` matrices + `position` (eye, for view-direction terms) — `render/types.hpp`. |
| `RenderTarget` | a `core::Framebuffer` + pixel size + clear color — `render/types.hpp` (color-only by default; an optional depth attachment exists only on depth-enabled `ViewTarget`s, which direct renders never require). |
| `MeshInstance` | an `AssetHandle` into the shared `AssetRegistry` (SPEC §9 V2.5), its `IMaterial`, and its model matrix. The scene carries the handle — the currency views exchange — never a raw CPU pointer. |
| `MeshScene` | a vector of `MeshInstance`s (CPU-side; `app/` builds these). |

`MeshRenderer::render(scene, camera, target)`:

1. Binds the target framebuffer, sets the viewport, clears to the target's
   clear color, and keeps the depth test OFF — direct single-scene renders are
   the deterministic painter's-order pass; true occlusion is a per-view opt-in
   applied by `View::render`'s own prologue call (depth support section above).
2. Determines whether any material in the scene is transparent. If so and a
   pipeline is injected, it **engages** the pipeline (brackets the draw with
   `begin()`/`end()`); an opaque-only scene **never** engages it (FR-render.3).
3. Draws each opaque mesh through the cached shader, using the material's base
   color.
4. When the pipeline is engaged, captures every transparent mesh through the
   pipeline (`drawTransparent` per transparent instance, using the material's
   base color + model transform); `end()` then depth-sorts and composites the
   captured fragments over the opaque pass (FR-render.2).

**The v1 opaque pass is deliberately deterministic** (required by FR-render.1's
analytic acceptance): it evaluates a fixed head-on directional light from world
+Z with ambient = 0, diffuse = 1, specular = 0, so

```
color = baseColor.rgb * max(dot(normalize(N), (0,0,1)), 0)
alpha = baseColor.a
```

A front-facing surface (normal aligned with +Z) renders at exactly the
material's base color; a back-facing surface is black. The `PhongMaterial`
ambient/diffuse/specular/shininess parameters are reserved for the future lit
path (samples, T12); the v1 gate pass is intentionally unlit-flat for
determinism. This keeps the center-pixel acceptance fully explainable.

#### Acceptance constants (FR-render.1/3, docs/render.md)

A golden +Z-facing quad covering `[-1,1]^2` at z=0, camera at `(0,0,5)` with an
orthographic projection mapping NDC `[-1,1]^2` onto the full 64×64 viewport:

| Quantity | Value | Where it comes from |
|---|---|---|
| Base color `{r,g,b,a}` | `{0.2, 0.4, 0.8, 1.0}` | clean RGBA8 bytes: `0.2*255=51`, `0.4*255=102`, `0.8*255=204`, alpha `255` |
| Center-pixel color | `{51, 102, 204}` (±1) | front-facing normal `(0,0,1)` → shade `dot((0,0,1),(0,0,1))=1` → `color = baseColor`; within 1/255 (FR-render.1) |
| Center-pixel alpha | `255` (== 1.0) | opaque material, alpha passed through unchanged (FR-render.3) |
| OIT pipeline engagement | `beginCount == 0` | opaque-only scene never engages the injected spy (FR-render.3) |
| `isTransparent()` | `false` at alpha 1.0; `true` at alpha 0.5 and 0.0 | transparency is `baseColor().a < 1.0` (FR-render.3) |

### `SliceRenderer` (`render/slice_renderer.hpp`, `.cpp`) — T11

A **stateless geometry-shader plane-clip renderer** (SPEC §3, FR-render.4): the
mesh-family technique that slices a mesh against a plane **purely on the GPU**.
It reuses the shared `MeshGeometry` (mesh geometry handling) and the `IMaterial`
interface exactly like `MeshRenderer`, and **does not use OIT in v1** (SPEC §3
"Slicing is geometry, not compositing"). `SliceRenderer::render(scene, camera,
plane, target)` receives all of its data per call; the renderer owns only GL
resources — its cached clip shader program (vertex + geometry + fragment) and
its transform-feedback capture program + object. **GPU geometry lives in the
shared `AssetRegistry`** (SPEC §9 V2.5): scenes carry `AssetHandle`s and the
renderer resolves them through the injected registry, sharing one GPU object
per CPU mesh with `MeshRenderer`. It implements the `IRenderer` dispatch
contract (`render/types.hpp`, SPEC §9 V2.3): its `IRenderer::render` forwards a
`Scene` holding a `SliceScene` to this concrete method, slicing against the
**plane carried by the scene itself** (`SliceScene::plane`).

Public scene structs (defined in `slice_renderer.hpp`; reuses `MeshInstance`
from `mesh_renderer.hpp`):

| Type | Purpose |
|---|---|
| `ClipPlane` | the slice plane in **world space**: a unit `normal` + a `point` on the plane. The kept side is `dot(normal, p - point) >= 0`. |
| `SliceScene` | a vector of `MeshInstance`s to clip (CPU-side; `app/` builds these) plus the `plane` the `IRenderer` dispatch path slices against (the concrete 4-argument render receives the plane explicitly and is unaffected). |

**Geometry shader clip (pure GPU).** The vertex shader transforms each vertex to
world space; the geometry shader computes each triangle's signed plane distance
`d[i] = dot(uPlaneNormal, P[i] - uPlanePoint)` and clips with the standard
Sutherland–Hodgman half-space clip against `d >= 0`, emitting the resulting 3-
or 4-vertex polygon as a triangle fan. The fragment shader shades the clipped
mesh with the triangle's **geometric face normal** (computed from the
world-space winding `normalize(cross(P1 - P0, P2 - P0))`) under the same
deterministic v1 flat lighting as the `MeshRenderer` opaque pass, so a kept
surface whose geometric normal is +Z renders at exactly the material's base
color.

**Cross-section capture (test-consumed).** `captureCrossSection(scene, plane,
out)` runs a second program whose geometry shader emits ONLY the **on-plane
cross-section polygon** of each triangle — the triangle's vertices exactly on
the plane plus the edge-intersection points where the plane cuts an edge
(`P[i] + t * (P[j] - P[i])` with `t = d[i] / (d[i] - d[j])`, which lies exactly
on the plane) — into a transform-feedback buffer
(`core::TransformFeedback`, a `core/` wrapper) and reads the emitted world-space
positions back. This is a **test-consumed readback path** (guardrail
`no_production_readback`): the render path never reads back from the GPU; the
FR-render.4 gate uses the captured vertices to assert they lie on the clip
plane. A crossing triangle whose intersection is a strict segment emits a
degenerate (zero-area) triangle so the emitted vertex count is deterministic.

#### Acceptance constants (FR-render.4, docs/render.md)

Golden cube `[-1,1]^3` (8 corners, 12 triangles, CCW-outward winding so the
geometric face normals point outward) clipped by the plane z=0 (normal `(0,0,1)`
through the origin; kept side z >= 0), identity model:

| Quantity | Value | Where it comes from |
|---|---|---|
| Crossing triangles | `8` | the 4 vertical faces (x=±1, y=±1) contribute 2 triangles each; the z=±1 faces are entirely on one side and contribute none |
| Emitted cross-section vertices | `24` | 8 crossing triangles × 3 vertices per degenerate cross-section triangle (a strict segment emits a zero-area triangle) |
| Plane distance of every emitted vertex | `<= 1e-4 * 2 = 2e-4` | `|dot((0,0,1), v - (0,0,0))| = |v.z|`; each vertex is an edge intersection computed at `t = d[i]/(d[i]-d[j])` and lies on z=0 up to float rounding; relative tolerance 1e-4 × mesh extent 2 (SPEC §4 plane-geometry tolerance) |
| Clipped-mesh center pixel | `{51, 102, 204}` (±1) | kept z=+1 face's geometric normal is exactly +Z → shade `dot((0,0,1),(0,0,1)) = 1` → `color = baseColor {0.2, 0.4, 0.8}` → bytes `{51,102,204}` (FR-render.4, within 1/255) |
| Base color | `{0.2, 0.4, 0.8, 1.0}` | clean RGBA8 bytes: `0.2*255=51`, `0.4*255=102`, `0.8*255=204` |

### `ContourRenderer` (`render/contour_renderer.hpp`, `.cpp`) — V3.8b T11

The **outline-only peer of `SliceRenderer`** (FR-app.3): where the slice clip
pass renders the KEPT side of a plane-clipped mesh (and captures its
on-plane cross-section for FR-render.4), `ContourRenderer` renders ONLY the
**plane∩mesh outline**, computed entirely **on the GPU** by
`render/shaders/contour.geom.glsl`. It replaces the deleted CPU path
(`app/mpr_contour.*`: `meshPlaneContour` triangle-plane edge test +
`overlayContour` CPU rasterization): the MPR slice views now layer a
`render::ContourObject` over the slice image as a second `ReView` item, and
the contour pixels come from the geometry shader at draw time.

**Geometry shader outline (pure GPU).** The vertex shader transforms each
vertex to world space (`uModel`); the geometry shader classifies each triangle
against the world-space clip plane with the same signed-distance pattern as
`slice_clip.geom.glsl` (`d[i] = dot(uPlaneNormal, P[i] - uPlanePoint)`, sign
classes `-1/0/+1`). A triangle strictly straddling the plane contributes ONE
outline segment: the segment between its two distinct edge crossing points
(`P[i] + t * (P[j] - P[i])`, `t = d[i]/(d[i]-d[j])`, near-coincident
crossings deduplicated). Fully-on-one-side, tangent and coplanar triangles
contribute nothing — the emitted primitive set is exactly the intersection
outline.

**Screen-space thick lines (why quads, not `line_strip`).** The natural
emission would be a `line_strip` of the two crossing points, but OpenGL 4.6
core caps `glLineWidth` at 1.0 — deprecated wide lines are unavailable — and
a 1-px stroke covers only ~±0.5 px around the analytic curve, far below the
FR-app.3 requirement that ≥ 90 % of the pixels within ±2 px of the curve
match the contour color (~35 % coverage). The geometry shader therefore emits
the standard GPU thick-line primitive: one **quad per segment**
(`triangle_strip`, 4 vertices), projected to continuous viewport pixel
coordinates, expanded perpendicular to the segment by `uHalfWidthPx` and
extended beyond each endpoint by square caps of the same length. Every pixel
whose center lies within `uHalfWidthPx` of an analytic segment then lies
inside its quad (closed-form containment), so `uHalfWidthPx = 2.0` fills the
FR-app.3 ±2 px band exactly. Degenerate projections (segment behind the eye,
zero projected length) are skipped deterministically. The fragment shader
writes the flat uniform `uColor`; blending stays disabled so every stroke
pixel is exactly the stroke color.

**Layer semantics + viewport plumbing.** `drawLayer(object|scene, camera,
ctx)` assumes ReView already bind+viewport+clear via the same
`core::DrawContext` and draws without clearing (View composes layers). The
thick-line expansion needs the viewport pixel size; it is read from the
DrawContext's cached viewport (`ctx.viewportRect()`, a new pure-cache
accessor in `core/draw.hpp`) rather than a raw GL query, keeping render/
GL-call-free. A cold context (no `setViewport` yet) is a typed error. The
direct single-scene `render(scene, camera, target)` keeps its own
bind+viewport+clear+disable for tests (regression lock).

**Camera enclosure requirement (T11 review, user-verified defect
2026-08-24).** The emitted quads live AT the clip plane in the object's
display frame — for an MPR slice view that is display z = held voxel-layer
coordinate + 0.5 (e.g. 35.5), far from the slice quad's z = 0. The drawing
camera's near/far must enclose those z values: geometry outside the clip
volume is discarded silently by the fixed-function clipper (no GL error, no
failed `Result`), which is exactly how the live MPR sample lost its contours
while the direct-render gate kept passing under its own wider camera. The
sample's shared 2D-view camera (`app::makeSliceCamera`, docs/mpr.md) now
guarantees enclosure (eye z = 512, far = 1024 → display z ∈ [-512, +511.9]),
and `tests/t15_mpr_test.cpp` composes the contour through that exact camera +
`render::View` path so a sample-vs-test wiring divergence fails the gate.

A second T11 review finding, same defect family: the Sagittal axis-permutation
model was built transposed (glm's column-major constructor read as row-major),
which the cube-symmetric direct-render gate cannot detect but which put the
live sample's non-cubic Sagittal outline half off-screen.
`AxisDisplayModelsPinPermutationNotTranspose` pins each view's display mapping
on an asymmetric probe vector so a transposition always fails the gate.

Public scene structs (defined in `contour_renderer.hpp`):

| Type | Purpose |
|---|---|
| `ContourObject` | one GPU contour layer: the contoured mesh's `AssetHandle` (RE-minimal — handle only, SPEC §12.4), the world/local-frame `ClipPlane`, the flat RGBA stroke color (default opaque red), the model matrix, and `halfWidthPx` (default 2.0 = the FR-app.3 band half-width). |
| `ContourScene` | a vector of `ContourObject`s (CPU-side; broker/app build these). |

**Broker translation.** `broker::ContourMapper`
(`: IMapper<scene::ContourObject, render::ContourObject>`, SPEC §11) performs
the scene→render translation: it registers the scene object's `data::Mesh` in
the shared `AssetRegistry` (dedup by content hash of stable bytes) and produces the RE-minimal
render object. Typed errors: null mesh pointer (code 1), null registry (code
2), `Space::VoxelIndex` planes (code 3 — the voxel→world conversion needs the
volume context and is deliberately not silently identity-mapped, SPEC §5).

#### Acceptance constants (FR-app.3 via GPU readback, docs/render.md)

Golden box `[16,48]^3`, one contour per axis with the axis-permutation display
model (Transverse identity / Coronal swaps Y/Z / Sagittal maps
`(x,y,z)->(y,z,x)`), clip plane constant Z at 32.5 in the display frame,
shared ortho down-Z camera mapping `[0,64]^2` 1:1 onto a 64×64 target:

| Quantity | Value | Where it comes from |
|---|---|---|
| Geometry-shader outline segments | `8` | hand-counted: the box's 4 side faces contribute 2 crossing triangles each; faces perpendicular to the held axis never cross |
| Analytic cross-section | rectangle `[16,48]^2` in pixel space | plane through voxel centers at 32.5 cuts all 4 side faces; every view shares the same display frame |
| Pixels within 2 px of the boundary | `508` of 64×64 | closed-form band count (perimeter 128 × 4-unit band ≈ 512 minus corner overlaps); no pixel center sits at exactly 2.0 |
| In-band match fraction | `>= 0.90` (measured ~1.00) | FR-app.3 SPEC threshold; every in-band pixel center lies inside some emitted thick-line quad (square caps included) |
| Contour color bytes | `(255, 0, 0, 255)` within 1/255 | `app::kContourColor` = pure red straight RGBA; blending disabled → exact bytes |
| Far-field spot pixel `(0,0)` | `(0, 0, 0, 0)` exact | ~21.9 px from the boundary — outside any stroke — so the clear color survives untouched |
| Registry dedup across 3 view translations | `slotCount() == 1` | same CPU box mapped three times through `ContourMapper` → one GPU object (SPEC §9 V2.5) |
| Mapper errors: null mesh / null registry / VoxelIndex plane | typed codes `1` / `2` / `3` | SPEC §5 — typed errors, no crashes, no silent reinterpretation |

### `PlaneRenderer` (`render/plane_renderer.hpp`, `.cpp`) — T8, V3.4b audit

A **stateless textured-plane renderer** (SPEC §3, FR-render.5): it is the
display primitive for IMAGE-BACKED quads — any `data::Image` textured onto a
plane reaches the GPU only through this renderer. Since the plane-capability
review deliverable (`VolumeSliceRenderer` below) the MPR slice views extract
their slices directly from the volume on the GPU, and the plane sample shows
GPU-extracted CT planes instead of a procedural gradient quad; image-backed
textured planes (e.g. future overlay images on top of an extracted slice)
remain this renderer's contract. Since V3.4b (T12) it is the **sole
owner of every textured-plane draw**: all displays reach the GPU only through
this renderer — `drawLayer(PlaneScene, Camera, DrawContext&)` inside a ReView's
IRenderable list (samples), or `render(scene, camera, target)` for direct
single-target tests. There is no CPU quad parsing anywhere outside `render/`:
the app side sends only `scene::PlaneObject{image asset ref, transform,
presentation}`, which `broker::PlaneMapper` translates into the
`render::PlaneInstance` values consumed here; the unit-quad VAO is built and
owned by `quadGeometry()` below, and `data::Image → core::Texture2D` upload
stays inside `textureFor()` (the `imageToRgba8` row-flip feeds that GPU upload;
it is not an app-side quad path).

**Broker-mediated display path (V3.4b T12):**

```
scene::PlaneObject{shared image asset ref, transform, presentation} (app/scene)
  └─ broker::PlaneMapper : IMapper<scene::PlaneObject, render::PlaneInstance>
       binds its ONE program-duration shared unit quad as geometry
     = render::PlaneInstance{shared quad, shared image, model}
        └─ render::View::addItem(PlaneScene, &PlaneRenderer)
             └─ View::render(): bind FBO + viewport + clear (DrawContext),
                then PlaneRenderer::drawLayer per layer (no clear between)
                  └─ plane.vert.glsl / plane.frag.glsl (RE_GLSL_VERSION 450)
             └─ View::blitTo(dst): engine present via core::blit (GL_NEAREST,
                exact when target size == rect size)
```

`broker::PlaneMapper` is a pure translator (ISP `IMapper`, no cache — texture
dedup lives in the shared asset store, SPEC §7 T14): it carries image +
transform across and binds the shared analytic unit quad; `presentation`
deliberately has no RE counterpart because the textured-plane path is an
unlit texture display by design (FR-render.5's quad must reproduce source
texels exactly). The mapped instance SHARES (T13 `shared_ptr`) the two things
it references: the mapper's program-duration static unit quad, and the scene
object's `data::Image` asset — nothing to outlive, nothing to dangle.

`PlaneRenderer::render(scene, camera, target)` receives all of its data per
call; the renderer owns only GL resources — its cached textured-plane shader
and one shared unit-quad VAO/VBO. GPU textures live in the shared
`AssetRegistry` (SPEC §7 T14): each `data::Image` is content-hash-deduped into
one `core::Texture2D` per store (uploaded once, reused across plane instances,
renderers, and views), so no per-renderer texture map exists. It implements
the `IRenderer` dispatch contract (`render/types.hpp`, SPEC §9 V2.3): its
`IRenderer::render` forwards a `Scene` holding a `PlaneScene` to this concrete
method.

Public scene structs (defined in `plane_renderer.hpp`):

| Type | Purpose |
|---|---|
| `PlaneGeometry` | four world-space corners + per-corner UVs + an analytic unit normal. `unitQuadXY()` builds the unit XY square `[-1,1]^2` at z=0 with normal `(0,0,1)` and the UV binding `(0,0)`@c0 … `(1,1)`@c2. A `render/`-internal detail since V3.4b: callers receive it only through `broker::PlaneMapper`. |
| `PlaneInstance` | a `PlaneGeometry` (shared with the mapper's static unit quad), the `data::Image` to texture it with (shared with the scene object's asset — T13 co-ownership), and a model matrix. Produced by `broker::PlaneMapper`. |
| `PlaneScene` | a vector of `PlaneInstance`s (CPU-side; built by mapping `scene::PlaneObject`s through `broker::PlaneMapper`). |

`PlaneRenderer::render(scene, camera, target)`:

1. Binds the target framebuffer, sets the viewport, clears to the clear color,
   and leaves depth/blend off (v1 FBOs are color-only, SPEC §6 / docs/core.md).
2. For each plane, resolves (or lazily uploads) its image as an RGBA8
   `core::Texture2D` in the shared `AssetRegistry` — the store's conversion is
   **vertically flipped** to GL bottom-up rows during upload (see below) — binds
   it to texture unit 0, and draws the shared unit quad through the cached
   textured shader.

**Geometry mapping.** Every plane is drawn from one shared unit-quad VAO whose
local corners are `(-1,-1,0)`, `(1,-1,0)`, `(1,1,0)`, `(-1,1,0)`. The renderer
builds an affine map onto the instance's corner box and applies the instance's
model matrix:

```
uScale = (corner1 - corner0) / 2
vScale = (corner3 - corner0) / 2
normal = normalize(cross(uScale, vScale))
model  = instance.model * [uScale | vScale | normal | corner0 + uScale + vScale]
```

The fourth column places the map so that local `(-1,-1,0)` lands on `corner0`
(exactly); for the default `unitQuadXY()` geometry this matrix is the identity,
so the quad covers the viewport 1:1 under the T8 orthographic camera.

**UV mapping & orientation (analytic, FR-render.5).** The UV space maps the
source image exactly once across the quad with `(u,v)=(0,0)` at corner0 and
`(1,1)` at corner2. The shared store's RGBA8 conversion flips the image's rows
(data::Image is top-left origin; core::Texture2D is bottom-up), so **the
image's top row renders at the quad's top when viewed from the normal's side**.
Textures are sampled with GL_LINEAR and CLAMP_TO_EDGE (core::Texture2D
defaults).

#### Acceptance constants (FR-render.5, docs/render.md)

A 64×64 image texturing the unit XY quad, orthographic camera mapping NDC
`[-1,1]^2` onto the full 64×64 viewport. Because the texture and viewport are
the same size, pixel center `(px,py)` samples the exact texel: `s = (px+0.5)/64
* 64 - 0.5 = px` (integer; frac = 0 under GL_LINEAR). With the vertical flip,
viewport pixel `(px,py)` (py=0 is the bottom) samples image pixel
`(px, H-1-py)`.

| Quantity | Value | Where it comes from |
|---|---|---|
| Solid image, center pixel | `{51,102,204,255}` (±1) | center samples the solid color exactly; `0.2*255=51`, `0.4*255=102`, `0.8*255=204` |
| Gradient `(4x,4y,128,255)`, center | `{128,124,128,255}` (±1) | viewport `(32,32)` → image `(32, 63-32=31)` = `(128,124,128)` |
| Gradient, bottom-left `(0,0)` | `{0,252,128,255}` (±1) | image `(0,63)` = `(0,252,128)` (image bottom) |
| Gradient, bottom-right `(63,0)` | `{252,252,128,255}` (±1) | image `(63,63)` |
| Gradient, top-left `(0,63)` | `{0,0,128,255}` (±1) | image `(0,0)` (image top) |
| Gradient, top-right `(63,63)` | `{252,0,128,255}` (±1) | image `(63,0)` |
| `unitQuadXY()` normal | `(0,0,1)` | `normalize(cross(c1-c0, c3-c0))` = `cross((2,0,0),(0,2,0))` / 4 |
| `unitQuadXY()` UV binding | `uv[0]=(0,0)`, `uv[2]=(1,1)` | image maps once across the quad |
| 90° Z rotation, bottom-left pixel | `G == 0` (±1) | `(x,y)→(-y,x)`: viewport `(-1,-1)` is local corner3 = UV `(0,1)` → image `(0,0)` = `(0,0,128)` (was `G=252` unrotated) |
| Same image through ReView + Broker (T12 gate): center `(32,32)` | `{128,124,128,255}` (±1) | identical constants — the broker route maps to the same unit quad + model; `drawLayer` inside a `View` changes only who binds/clears the FBO, not the sampled texel |
| T12 gate: four corners via ReView + Broker | BL `{0,252,…}`, BR `{252,252,…}`, TL `{0,0,…}`, TR `{252,0,…}` B=`128`, A=`255` (±1) | corner probes pin the row-flip orientation through the composed path |
| MPR slice layer via PlaneMapper (`makeSliceModel`+`makeSliceCamera`) | solid bytes at center + all four corners (±1) | ortho `[0,img]²` ↔ viewport 1:1, quad covers the viewport edge-to-edge |
| `broker::PlaneMapper` null-image map | typed error code 1 | SPEC §5: no exceptions, no silently-empty instance |
| Shared unit quad across mappings | one geometry pointer | mapper owns exactly one program-duration static `PlaneGeometry`; instances co-own a reference (T13 shared ownership) |

### `VolumeRenderer` (`render/volume_renderer.hpp`, `.cpp`) — T9

A **stateless ray-cast volume renderer** (SPEC §3, FR-render.6) that consumes the
pure `volume/` math (SPEC §3: "VolumeRenderer (ray-cast GL draw; volume/ provides
the pure math)"). `VolumeRenderer::render(scene, camera, target)` receives all of
its data per call; the renderer owns only GL resources — its cached ray-cast
shader program and one shared full-screen quad VAO/VBO. GPU 3D textures live in
the shared `AssetRegistry` (SPEC §7 T14): each `data::VolumeDataset` is
content-hash-deduped into one `core::Texture3D` per store (uploaded once,
reused across instances, renderers, and views — two renderer instances share
one GL texture id), so no per-renderer texture map exists. It implements the
`IRenderer` dispatch contract (`render/types.hpp`, SPEC §9 V2.3): its
`IRenderer::render` forwards a `Scene` holding a `VolumeScene` to this concrete
method.

Public scene structs and constant (defined in `volume_renderer.hpp`):

| Type | Purpose |
|---|---|
| `VolumeInstance` | a shared `data::VolumeDataset` reference (T13 co-ownership), an owned by-value `volume::TransferFunction` mapping its scalar values to RGBA (T13: small immutable ramp copied per instance — the null-pointer case is impossible), and a model matrix. The dataset occupies the unit cube `[0,1]^3` in **model space**; the model matrix places/orients it in world space. |
| `VolumeScene` | a vector of `VolumeInstance`s (CPU-side; `app/` builds these). |
| `kDefaultStepLength` | the default ray-cast sampling step length in world units (`0.25`); the shader samples `floor(span / stepLength)` steps at their centers (FR-vol.3). |

`VolumeRenderer::render(scene, camera, target)`:

1. Binds the target framebuffer, sets the viewport, clears to the clear color,
   and leaves depth/blend off (v1 FBOs are color-only, SPEC §6 / docs/core.md).
2. For each volume, resolves (or lazily uploads) its dataset as a
   `core::Texture3D` in the shared `AssetRegistry` (`GL_R32F`, `GL_LINEAR`
   trilinear filtering, `GL_CLAMP_TO_EDGE`), uploads the transfer function's
   control points as uniforms, and draws the shared full-screen quad through
   the cached ray-cast shader.

**The ray-cast fragment shader** reconstructs the pixel's world ray by
unprojecting its NDC near/far points through `uViewProj = proj * view` (works for
ortho and perspective), intersects it against the volume's **world AABB**
(closed-form slab method, FR-vol.3), then steps along the segment at center
positions (FR-vol.3) sampling the density texture and evaluating the
piecewise-linear transfer function (FR-vol.1), accumulating front-to-back with
premultiplied alpha (FR-vol.2). This mirrors the pure `volume/` math exactly:

| Shader step | volume/ counterpart |
|---|---|
| slab AABB intersection | `volume::intersectRayAabb` |
| `floor(span/step)`, `t[k] = tEntry + (k+0.5)*step` | `volume::computeRaySampleSteps` |
| piecewise-linear ramp | `volume::TransferFunction::sample` |
| `w=(1-a)*tf.a; rgb+=w*tf.rgb; a+=w` | `volume::compositeFrontToBack` |

**Texture-coordinate mapping (trilinear, exact).** The dataset's index space
`[0, dim-1]` is normalized to model space `[0,1]^3`; a model-space position `p`
maps to a continuous index `idx = p * (dim-1)`. The shader samples at texture
coordinate `u = (idx + 0.5) / dim`, so with GL_LINEAR each texel center (`idx`
integer) reproduces the voxel value and an interior `idx` reproduces the CPU
trilinear interpolant (`data::VolumeDataset::sampleTrilinear`, FR-data.3). The
world AABB used for the slab intersection is computed on the CPU by transforming
the 8 corners of `[0,1]^3` by the model matrix and taking the min/max — exact for
axis-aligned scaling/translation (the v1 case), conservative for rotated models.

**Transfer function on GPU.** The TF's control points are uploaded as fixed-size
uniform arrays (`uTfValues[8]`, `uTfColors[8]`, `uTfCount`); the shader evaluates
the same clamped piecewise-linear ramp as `TransferFunction::sample`. A TF with
more than 8 control points is rejected with a typed error (the shader's fixed
array size). Control-point colors are straight RGBA and are premultiplied during
compositing, exactly as `compositeFrontToBack` does (FR-vol.2).

#### Acceptance constants (FR-render.6, docs/render.md)

A tiny synthetic volume (2×2×2, every voxel `0.5`) occupies `[0,1]^3` in world
space (identity model). Orthographic camera at `(0.5,0.5,5)` looking straight
down `-Z` maps NDC `[-1,1]^2` onto the full 64×64 viewport. For any pixel the
reconstructed ray is exactly parallel to `-Z`, so its world-AABB intersection
spans exactly `1.0` in z; with `kDefaultStepLength = 0.25`,
`floor(1.0/0.25) = 4` steps at their centers. Every sample's density is the
uniform `0.5`; the constant-green transfer function (control points at 0 and 1,
both `{0,1,0,0.5}`) maps it to straight RGBA `{0,1,0,0.5}`; compositing four such
samples front-to-back (FR-vol.2) gives the premultiplied result `{0, 0.9375, 0,
0.9375}`:

```
out.a   = 1 - (1-0.5)^4                    = 0.9375
out.rgb = 0.5*(1 + 0.5 + 0.25 + 0.125)     = 0.9375  (along green)
```

| Quantity | Value | Where it comes from |
|---|---|---|
| Center-pixel RGBA | `{0, 239, 0, 239}` (±1) | `round(0.9375*255) = 239`; the T9 gate compares against the analytic CPU ray-cast from the same volume/ math within 1/255 (FR-render.6) |
| Analytic CPU ray-cast | `{0, 0.9375, 0, 0.9375}` (±1e-6) | `volume::computeRaySampleSteps` + `sampleTrilinear` + TF + `compositeFrontToBack` on the center-pixel ray |
| Identity-model world AABB | `[0,1]^3` | transform of the 8 corners of `[0,1]^3` by the identity model |
| `model = T(1,2,3)*S(0.5)` world AABB | `[1,1.5] × [2,2.5] × [3,3.5]` | `S` first then `T`; corner `(0,0,0)` → `(1,2,3)`, corner `(1,1,1)` → `(1.5,2.5,3.5)` |
| Too many TF control points | typed error containing `more than 8 control points` | the shader's fixed uniform array size is 8 |

### `VolumeSliceRenderer` (`render/volume_slice_renderer.hpp`, `.cpp`) — plane-capability review deliverable

The **GPU volume-plane extraction renderer** (extends FR-render.5/FR-app.2):
where `VolumeRenderer` ray-casts the whole slab and `PlaneRenderer` displays
CPU-computed images, `VolumeSliceRenderer` produces a plane through a volume
**entirely on the GPU** — a "plane" in this engine semantically means a slice
extracted from volume data, and nothing demonstrates (or gates) extraction
until this renderer exists. It draws one full-screen quad per instance; the
fragment shader (`volume_slice.frag.glsl`, sharing
`volume_raycast.vert.glsl`'s NDC-quad passthrough):

1. reconstructs the pixel's world ray by unprojecting its NDC near/far points
   through `uViewProj = proj * view` (identical to the ray-cast entry);
2. intersects the ray with the instance's **world-space `ClipPlane`**
   (`dot(n, ro + t·rd − p0) = 0`; parallel rays and behind-eye hits write
   transparent black);
3. converts the hit into the dataset's model space via `uInvModel` and rejects
   hits outside the unit cube `[0,1]^3` (with a 1e-4 tolerance band so float
   rounding cannot punch holes into the outermost voxel ring;
   CLAMP_TO_EDGE makes the band invisible in the output bytes);
4. samples the cached R32F `core::Texture3D` at texel coordinate
   `(idx + 0.5) / dim` with `idx = modelPos · (dim − 1)` — the **same mapping
   as the ray-cast shader**, so GL_LINEAR reproduces the CPU sampler
   `data::VolumeDataset::sampleTrilinear` voxel-for-voxel;
5. writes the transfer-function color as **straight RGBA**
   (piecewise-linear ramp identical to `volume_raycast.frag.glsl`'s).

Because a slice index enters only through uniforms (the clip-plane point and
the model matrix), scrolling to another slice is a uniform/state change:
there is **no CPU voxel loop and no intermediate image anywhere on this
path**, which is what makes MPR slice scrolling interactive. The CPU oracle
(`app::makeSliceImage`) is retained solely as the gate tests' reference
implementation.

Like `ContourRenderer`, this renderer deliberately does **not** join the
`render::Scene` IRenderer dispatch variant (whose 4-alternative size is pinned
by its own dispatch gate): ReView composes it through the type-erased
`drawLayer(VolumeSliceScene, Camera, DrawContext&)` path and direct tests call
the concrete typed overload. GPU 3D textures resolve through the shared
asset store (`lookupVolume`, content-hash dedup), so an extracted slice and a
ray-cast of one dataset share a single upload.

Public scene structs (defined in `volume_slice_renderer.hpp`):

| Type | Purpose |
|---|---|
| `VolumeSliceInstance` | a shared dataset reference, an owned by-value transfer function, the model matrix placing the unit cube in world space, and the world-space extraction plane. A null dataset reference is rejected with typed error code 1 (a silently empty layer would be indistinguishable from an empty viewport). |
| `VolumeSliceScene` | a vector of instances (CPU-side; app builds these). |
| `kMaxVolumeSliceTfPoints` | `8` — the shader's fixed TF uniform array size; more control points are a typed error, mirroring `VolumeRenderer`. |

**Display-frame convention for axis-aligned MPR views** (shared scaffolding,
see docs/mpr.md): with the model matrix mapping voxel-center index *i* to
display coordinate *i + 0.5* on every axis (`app::sliceVolumeModel`: scale by
`max(dim−1, 1)`, translate by 0.5, then the per-view axis permutation) and an
orthographic camera over the free-axis rectangle (`app::makeSliceCamera(freeW,
freeH)`), pixel center `(px, py)` back-projects to continuous index
`(px, py, heldIndex)` exactly — so extracted output equals the CPU oracle's
bytes within 1/255 everywhere on the slice.

#### Acceptance constants (plane-capability review gate, docs/render.md)

Synthetic 2×2×2 volume, field `value(x,y,z) = x + 2y + 4z`, axis-probe TF
(integer v → RGBA `{v/255, (255−v)/255, 0, 1}`, exact at breakpoints),
Transverse display frame, ortho camera over `[0,2]²`:

| Quantity | Value | Where it comes from |
|---|---|---|
| Mid-plane (z between the two layers, continuous held index 0.5) | column density `x + 2y + 2 ∈ {2,3,4,5}` | trilinear weights are exactly (0.5, 0.5) between layers 0 and 1 |
| Extracted pixel vs oracle | `tf.sample(dataset.sampleTrilinear(...))` within 1/255 at EVERY probe | FR extension; dense sweep over the whole target |
| Pixels inside the footprint (64×64 target) | `1024` | centers `(px+0.5)/32 ∈ [0.5, 1.5] ⇔ px ∈ [16, 47]` (closed form; none lands on a boundary) |
| Pixels outside the footprint | exact `(0,0,0,0)` | rays missing the volume slab write transparent black |
| Layer state change (plane z: 0.5 → 1.5, same renderer) | red byte shifts exactly `+4` per column | closed-form layer delta `v(x,y,1) − v(x,y,0) = 4`; readback after state change proves re-extraction through uniforms only |
| Oblique plane `x + z = 1` (normal `normalize(1,0,1)`, identity model) | matches analytic ray-plane oracle within 1/255; corner rays miss → zeros | fully general intersection; no special-casing for oblique planes |
| MPR axes T/C/S on an asymmetric 8×6×4 volume | whole frame == `app::makeSliceImage` oracle within 1/255 per axis | Transverse holds Z, Coronal holds Y, Sagittal holds X; asymmetric dims make any permutation error fail |
| Typed errors: null dataset / >8 TF points / 0-size target | codes `1` / `1` / `1` | SPEC §5 diagnostics, never crashes or silent empty output |

### Shared renderer internals — one prologue, one quad, one hash, one geometryFor

The technique renderers used to carry four families of copy-paste. Each now
has exactly ONE definition (Sr. review "single internal implementation per
renderer" rule, SPEC §6):

- **Pass prologue** — binding the target framebuffer, setting the viewport,
  clearing to the clear color, and setting depth test + blending state existed
  verbatim in every direct `render()` entry point. It is now
  **`core::DrawContext::beginPass(framebuffer, width, height, r, g, b, a, depthTest = false)`**
  (`core/draw.hpp`), called once by each of the six direct-render entry points
  (`Mesh/Plane/Volume/Slice/Contour/VolumeSliceRenderer::render`) and by
  `View::render` — the composition owner whose single clear replaces per-layer
  clears; the View passes its per-view `depthTest` flag there (all direct
  renderers keep the defaulted `false`). Call order
  is fixed: bind (null framebuffer = window default FB) → viewport → clear
  color + clear → depth state (disabled by default; enabled + depth-cleared on
  the per-view opt-in) → blend off. The OIT composite sequence
  (`LinkedListOIT::end`) deliberately does NOT call it: compositing must blend
  OVER the already-drawn opaque contents, so nothing may be cleared there — it
  issues its own narrower sequence with the depth test explicitly disabled.
- **Full-screen NDC quad** — the position-only two-triangle quad (corners
  (-1,-1), (1,-1), (1,1), (-1,1); index pattern `{0,1,2, 0,2,3}`) lived twice
  as `kScreenQuadVerts` (VolumeRenderer, LinkedListOIT) plus a third
  byte-identical twin `kSliceQuadVerts` (VolumeSliceRenderer). All three now
  own a **`render::ScreenQuad`** instance built by the single provider
  `render/screen_quad.{hpp,cpp}`, which also owns the shared index-pattern
  constant `kQuadTriangleIndices`. PlaneRenderer keeps its own interleaved
  unit quad (position+UV+normal from `PlaneGeometry::unitQuadXY` — its shader
  samples per-vertex UVs, an attribute schema the NDC quad does not carry) but
  draws with the same shared index pattern.
- **Mesh-geometry resolution** — the identical `geometryFor(handle)` members
  of Mesh/Slice/ContourRenderer collapsed into ONE helper over the registry:
  **`resolveMeshGeometry(registry, handle, rendererName)`**
  (`render/asset_registry.hpp`). Null store → typed error code 4 naming the
  renderer; otherwise the handle resolves to the one registry-owned
  `MeshGeometry`.
- **Content hash** — the FNV-1a byte-hash twins (`meshContentHash`,
  `volumeContentHash`, `imageContentHash`) were deleted from
  `render/asset_registry.cpp`; both layers resolve identity through the single
  GL-free definition **`data/content_hash.hpp`**
  (`data::computeContentHash(Mesh|VolumeDataset|Image)`, which
  `scene::computeContentHash` forwards to). Only the PhongMaterial VALUE hash
  stays local to the registry — material identity is defined on the RE-side
  value (§12.4).
- **GL constants via core/** — `SliceRenderer`'s capture pass began transform
  feedback with the raw glad enum; it now passes
  **`core::PrimitiveMode::Triangles`**, a core-owned enum mapped internally by
  `core::TransformFeedback::begin`. `<glad/gl.h>` appears nowhere under
  `render/`.

Each renderer's direct `render()` and View-composited `drawLayer()` share one
private body (per-renderer `drawInstances`/`clipInstances`; mesh takes a
`skipTransparent` flag because the OIT split is owned by the direct path
only). Zero pixel change is regression-locked by the FR-render gates.

## Guardrails observed

- **GL ownership**: `render/` is GL-call-free — no raw `glXxx` call and no
  `<glad/gl.h>` include appears under `render/` (mechanically pinned by the
  T17 gate). Raw draw-state calls
  (`glViewport`, `glClear`, `glDrawElements`, `glBlitFramebuffer`, …) live in
  `core/draw.cpp`; raw
  readback (`glReadPixels`) lives in `core/read_pixels.cpp`; SSBO creation/
  binding/readback lives in `core/storage_buffer.cpp`; transform-feedback
  creation/capture/readback lives in `core/transform_feedback.cpp` (its
  primitive mode is named through core-owned `core::PrimitiveMode`); image
  binding + memory barriers live in `core/draw.cpp` (all under `core/`). The
  only readback consumers are tests (pixel reads,
  `LinkedListOIT::readCapturedFragmentCount`,
  `SliceRenderer::captureCrossSection`) — guardrail `no_production_readback`.
- **Stateless + dependency inversion**: `render()` takes all data per call;
  renderers depend on `IMaterial` / `ITransparencyPipeline` abstractions and
  expose the `IRenderer` dispatch contract (SPEC §9 V2.3). GPU assets are
  injected: every technique renderer takes a shared `AssetRegistry` handle
  (`std::shared_ptr`, T13 co-ownership; the volume/plane renderers default to
  the process-wide store) and resolves scene handles or asset content through
  it (SPEC §9 V2.5 / §7 T14) — never a raw GL call, never a per-renderer
  duplicate upload, no per-renderer pointer-keyed caches anywhere.
- **Typed diagnostics**: draw/geometry failures return `data::Result` — no
  exceptions, no silent failure.
- **Deterministic / single-threaded**: one render thread; the v1 opaque pass is
  a fixed deterministic lighting configuration.
- **Doxygen** on all public API (SPEC §5).
