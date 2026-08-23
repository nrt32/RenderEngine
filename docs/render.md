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
> SPEC §9 V2.4), the **V2 T3 deliverable** (the generational asset registry
> `AssetRegistry`/`AssetHandle`, SPEC §9 V2.5), the **V2 T7 deliverable**
> (shader externalization to `.glsl` files + malformed fixture, SPEC §9 V2.6),
> the **V2 T8 deliverable** (GLSL profile macro `RE_GLSL_VERSION`, SPEC §9
> V2.7), and the **T4 (V3.3) deliverable** (`scene::Camera` manipulable
> `pan/rotate/zoom/orbit` → `render::Camera{view,proj,pos}` via
> `broker::CameraMapper`; `2D` ortho vs `3D` perspective validated by mapper).
> It is part of the `docs/render.md` documentation map
> (T7/T8/T9/T10/T11 + V2 T1/T2/T3/T7/T8 + T4; later tasks extend it).

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
| `volume_raycast.vert.glsl` | `kRayCastVertexShader` | vertex | `VolumeRenderer` |
| `volume_raycast.frag.glsl` | `kRayCastFragmentShader` | fragment | `VolumeRenderer` |
| `oit_capture.vert.glsl` | `kCaptureVertexShader` | vertex | `LinkedListOIT` (capture) |
| `oit_capture.frag.glsl` | `kCaptureFragmentShader` | fragment | `LinkedListOIT` (capture) |
| `oit_composite.vert.glsl` | `kCompositeVertexShader` | vertex | `LinkedListOIT` (composite) |
| `oit_composite.frag.glsl` | `kCompositeFragmentShader` | fragment | `LinkedListOIT` (composite) |
| `slice.vert.glsl` | `kSliceVertexShader` | vertex | `SliceRenderer` (shared) |
| `slice_clip.geom.glsl` | `kClipGeometryShader` | geometry | `SliceRenderer` (clip) |
| `slice_clip.frag.glsl` | `kClipFragmentShader` | fragment | `SliceRenderer` (clip) |
| `slice_capture.geom.glsl` | `kCaptureGeometryShader` | geometry | `SliceRenderer` (capture) |
| `slice_capture.frag.glsl` | `kCaptureFragmentShader` | fragment | `SliceRenderer` (capture) |

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
| `RenderTarget` | a color-only `core::Framebuffer` + pixel size + clear color (SPEC §3; v1 FBOs are color-only, SPEC §6 / docs/core.md). A null framebuffer means the window's default framebuffer (samples). **Moved here from `mesh_renderer.hpp`**. |
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

### Multi-view composition: `View`/`ViewRect`/`ViewRenderer` + `core::blit` (SPEC §9 V2.4, V2 T2)

The **multi-view workstream's engine side** ("Model B: per-view FBO + engine
blit"). The front-end (app/) shares **per-view window-section handles**
(`render::ViewRect`) and **abstract scene objects** (`render::View`, each
holding a `Scene` dispatch variant); the engine compositor
(`render::ViewRenderer`) dispatches each view's scene through the `IRenderer`
registered for its technique (V2 T1), **renders it into the view's own
`core::Framebuffer`**, then **blits each FBO into its window rect** via the new
`core::blit` wrapper. **No app-side viewport blending**: the app performs no
textured-quad present pass and no per-view compositing — the engine present is
the whole composition. The MPR sample's 2×2 grid (docs/mpr.md) is driven
through this compositor (T14/T15 + V2 T2).

| Type | Purpose |
|---|---|
| `ViewRect` | a window-section rectangle in GL pixel coordinates (origin bottom-left, matching `core::setViewport`): `x`/`y`/`width`/`height`. The per-view window-section handle the app shares with the engine. |
| `View` | one view of a multi-view window: the abstract `Scene` object (dispatch payload), the per-view `Camera`, the per-view `clearColor`, and the `ViewRect` its FBO is blitted into. |
| `SceneKind` | `enum { Mesh, Plane, Volume, Slice }` mirroring the `Scene` variant's alternative order (static_assert-verified); `ViewRenderer` registers one `IRenderer` per kind. |
| `ViewRenderer` | the engine-side multi-view compositor: owns one color-only `core::Framebuffer` per view (all `viewWidth`×`viewHeight`), `setRenderer(kind, renderer)` registers the dispatch targets, `renderViews(views)` renders every view's scene into its own FBO, `present(views, destination)` blits each FBO into its pinned rect (destination `nullptr` = the window's default framebuffer), `render(views, destination)` = both. |
| `core::blit` | the new `core/` wrapper around `glBlitFramebuffer` (guardrail `gpu_api_ownership`: the raw GL call lives in `core/draw.cpp`). Copies a color pixel rectangle from a source FBO to a destination framebuffer (`nullptr` = default framebuffer 0), GL_NEAREST, scaled to the destination rect. v1 FBOs are color-only, so only `GL_COLOR_BUFFER_BIT` is blitted. |

**Dispatch semantics (engine side).** For each view, `renderViews` uses the
`Scene` variant's alternative index — which equals the `SceneKind` enumerator
value (static_assert in `view_renderer.hpp`) — to pick the registered
`IRenderer` and call its `IRenderer::render(scene, camera, target)` with a
`RenderTarget` over the view's own FBO. A view whose technique has no
registered renderer is rejected with a typed error (code 2); a view-count
mismatch is rejected with a typed error (code 1) (SPEC §5, no exceptions). The
per-view FBOs are created lazily on the first render.

**Blit semantics (exact, pixel-for-pixel).** `core::blit` copies
`(srcX, srcY, srcWidth, srcHeight)` of the source FBO to
`(dstX, dstY, dstWidth, dstHeight)` of the destination with GL_NEAREST. Both
framebuffers share the GL y-up convention, so the copy is a direct 1:1 transfer
when the sizes match — no vertical flip, no filtering — which is what makes the
gate's center-pixel assertions exact: each view's FBO content lands
pixel-for-pixel at its pinned window rect position.

#### Acceptance constants (V2 T2 multi-view gate, docs/render.md)

A 2-view layout in a **1280×480** window; each view's FBO is 640×480 (equal to
its rect, so the blit is 1:1):

| Quantity | Value | Where it comes from |
|---|---|---|
| Window size | `1280×480` | the gate's pinned 2-view window |
| View A rect | `(0, 0, 640, 480)` | pinned (task layout) |
| View B rect | `(640, 0, 640, 480)` | pinned (task layout); the two rects exactly tile the window |
| View A scene | MeshScene: FR-render.1 golden +Z quad, base `{0.2, 0.4, 0.8, 1.0}` | renders `{51, 102, 204}` at the FBO center (front-facing, shade 1) |
| View B scene | PlaneScene: 640×480 solid image `{0.9, 0.1, 0.3, 1.0}` | texel bytes `{round(0.9·255)=230, round(0.1·255)=26, round(0.3·255)=77}`; quad maps 1:1, center samples the solid texel |
| View A FBO center `(320, 240)` | `{51, 102, 204}`, α `255` | view A's scene rendered into its OWN FBO (per-view FBO proof) |
| View B FBO center `(320, 240)` | `{230, 26, 77}`, α `255` | view B's scene rendered into its own FBO |
| Window pixel `(320, 240)` | `{51, 102, 204}` (±1) | the blit places view A's FBO (320,240) at window (320,240) — its pinned rect center |
| Window pixel `(960, 240)` | `{230, 26, 77}` (±1) | view B's FBO (320,240) shifted by rect origin (640,0) |
| Window pixel `(639, 240)` | `{51, 102, 204}` (±1) | last pixel of rect A — the split is pinned exactly at x = 640 |
| Window pixel `(640, 240)` | `{230, 26, 77}` (±1) | first pixel of rect B |
| Unregistered technique view | typed error, code 2 | a VolumeScene with no registered Volume renderer is rejected before any draw (SPEC §5) |
| View-count mismatch | typed error, code 1 | 1 view against a 2-view compositor (SPEC §5) |
| `present()` before the first `renderViews` | typed error, code 3 | the per-view FBOs don't exist yet — rejected instead of blitting unrendered targets (SPEC §5) |

### The asset registry: `AssetHandle` + `AssetRegistry` (SPEC §9 V2.5, V2 T3)

The **asset system of the multi-view workstream**: a single registry owns exactly
**ONE GPU object per individual CPU object registered, globally** across every
mesh-family renderer. Scenes carry **copyable `AssetHandle`s — `{index,
generation}`** — instead of raw `const data::Mesh*` pointers, and **handles are
the currency views exchange**: a `View`'s `Scene` holds handles, and the
renderers resolve them through the shared registry. Registering the same
`data::Mesh` twice (e.g. once through the MeshRenderer path and once through
the SliceRenderer path) yields one GPU object — the registry dedups by
CPU-object identity — fixing the pre-V2 per-renderer double-upload of the same
mesh.

| Type / member | Purpose |
|---|---|
| `AssetHandle` | a copyable `{index, generation}` handle into the registry's slot table. `index` selects the slot, `generation` validates it. Cheap to copy; the default handle `{0, 0}` is the reserved **null handle** (`isNull()`), the "no asset" instance renderers skip (like the pre-V2 null mesh pointer) — real handles always carry `generation >= 1`. |
| `AssetRegistry::registerAsset(mesh)` | registers `mesh` (uploading its GPU geometry once) and returns its handle. Registering the **same CPU object again returns the EXISTING handle** (dedup by identity, `slotCount()` unchanged); two distinct objects — even with identical content — are two GPU objects. Returns a typed error if the upload fails (no GL context). Named `registerAsset`, not `register` — `register` is a C++ reserved keyword. |
| `AssetRegistry::resolve(handle)` | returns the handle's `MeshGeometry*`. A **stale/dangling handle** — out-of-range index (code 1), generation mismatch (code 2, message "stale handle": a freed, reused, or fabricated handle), or a freed slot (code 3) — returns a **typed error, never a crash** (SPEC §5). |
| `AssetRegistry::unregister(handle)` | frees the slot: destroys its GPU object, **bumps the slot's generation** so every outstanding handle to it goes stale immediately, and makes the slot reusable. A later `registerAsset` of a different mesh may **reuse the freed index with a fresh generation** — the old handle stays stale. |
| `AssetRegistry::slotCount()` | the number of currently registered (live) GPU objects: one per distinct individual CPU object (the V2 T3 gate: registering the same mesh twice leaves this at exactly 1). |

**Generational safety.** Every slot's generation starts at 1 and is bumped each
time the slot is freed (and again when a freed slot is reused). A handle is
valid only while its `{index, generation}` exactly matches the slot's — so a
dangling handle (its slot freed or reused) is detected at resolve time and
surfaced as a typed error, never a dereference of freed memory. Resolved
geometry pointers stay valid until the slot is freed (each slot's geometry is
heap-stable); the renderers resolve per draw and never retain pointers across
registrations.

**Renderer integration.** `MeshRenderer` and `SliceRenderer` are constructed
with a shared `AssetRegistry*` (non-null, outliving the renderer) and resolve
every instance's handle through it — the registry, not the renderer, owns the
GPU geometry. This is what makes the dedup global: the same mesh drawn by both
renderers (and by any number of views) is uploaded once.

#### Acceptance constants (V2 T3 asset-registry gate, docs/render.md)

| Quantity | Value | Where it comes from |
|---|---|---|
| Same `data::Mesh` registered twice (via the MeshRenderer path + the SliceRenderer path) | `slotCount() == 1` | the registry dedups by CPU-object identity: one GPU object per individual CPU object (SPEC §9 V2.5) |
| The two registration handles | equal (`{index, generation}` identical) | `registerAsset` of an already-registered object returns the existing handle |
| Both handles resolve to | the same **non-zero** GL object id (`MeshGeometry::vaoId()`) | one GPU object behind both handles; GL reserves 0, so a live VAO name is non-zero |
| Two DISTINCT meshes with identical content | `slotCount() == 2`, distinct VAO ids | dedup is per individual CPU object; GL object names are unique among live objects of a type |
| Stale `{index, generation+1}` lookup | typed error, code 2, message contains `stale` | generation mismatch (fabricated stale handle) |
| `{index, 0}` lookup | typed error | generation 0 is the never-allocated marker (null handle) |
| Out-of-range index lookup | typed error, code 1 | index beyond the slot table |
| Handle after `unregister` | typed error (code 2) | the freed slot's generation is bumped at free time; unregistering it again is also a typed error |
| Freed-slot reuse for a NEW mesh | same index, **new generation**; old handle still stale, new handle resolves | slot reuse issues a fresh generation — the generational mechanism |
| Stale handle inside a rendered scene | `render()` returns the typed error, no crash | the renderer propagates the resolve error (SPEC §5) |
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

### `PhongMaterial` (`render/phong_material.hpp`, `.cpp`)

The v1 Phong material model. Holds the classic Phong parameters — an ambient
factor, a diffuse factor, a specular color, and a shininess exponent — plus the
straight RGBA base color. `isTransparent()` is derived from the base color's
alpha channel (`alpha < 1.0`), so transparency is a material property carried
by the color itself.

- `PhongMaterial({r,g,b,a})` clamps the base color to `[0,1]`.
- `isTransparent()` returns `baseColor().a < 1.0f` (FR-render.3: an alpha of
  `1.0` is opaque; `0.5` and `0.0` are transparent).

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
| `RenderTarget` | a color-only `core::Framebuffer` + pixel size + clear color — `render/types.hpp`. |
| `MeshInstance` | an `AssetHandle` into the shared `AssetRegistry` (SPEC §9 V2.5), its `IMaterial`, and its model matrix. The scene carries the handle — the currency views exchange — never a raw CPU pointer. |
| `MeshScene` | a vector of `MeshInstance`s (CPU-side; `app/` builds these). |

`MeshRenderer::render(scene, camera, target)`:

1. Binds the target framebuffer, sets the viewport, clears to the target's
   clear color, and leaves the depth test off (v1 FBOs are color-only, SPEC §6 /
   docs/core.md).
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

### `PlaneRenderer` (`render/plane_renderer.hpp`, `.cpp`) — T8

A **stateless textured-plane renderer** (SPEC §3, FR-render.5): it feeds the MPR
slice views (T14). `PlaneRenderer::render(scene, camera, target)` receives all
of its data per call; the renderer owns only GL resources — its cached
textured-plane shader, one shared unit-quad VAO/VBO, and a texture cache keyed
by image pointer (each `data::Image` is uploaded to the GPU once and reused
across plane instances and views). It implements the `IRenderer` dispatch
contract (`render/types.hpp`, SPEC §9 V2.3): its `IRenderer::render` forwards a
`Scene` holding a `PlaneScene` to this concrete method.

Public scene structs (defined in `plane_renderer.hpp`):

| Type | Purpose |
|---|---|
| `PlaneGeometry` | four world-space corners + per-corner UVs + an analytic unit normal. `unitQuadXY()` builds the unit XY square `[-1,1]^2` at z=0 with normal `(0,0,1)` and the UV binding `(0,0)`@c0 … `(1,1)`@c2. |
| `PlaneInstance` | a `PlaneGeometry`, the `data::Image` to texture it with, and a model matrix. |
| `PlaneScene` | a vector of `PlaneInstance`s (CPU-side; `app/` builds these). |

`PlaneRenderer::render(scene, camera, target)`:

1. Binds the target framebuffer, sets the viewport, clears to the clear color,
   and leaves depth/blend off (v1 FBOs are color-only, SPEC §6 / docs/core.md).
2. For each plane, uploads (or reuses) its image as an RGBA8 `core::Texture2D`
   (converted and **vertically flipped** by `imageToRgba8`, see below), binds it
   to texture unit 0, and draws the shared unit quad through the cached
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
`(1,1)` at corner2. `imageToRgba8` flips the image's rows (data::Image is
top-left origin; core::Texture2D is bottom-up), so **the image's top row
renders at the quad's top when viewed from the normal's side**. Textures are
sampled with GL_LINEAR and CLAMP_TO_EDGE (core::Texture2D defaults).

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

### `VolumeRenderer` (`render/volume_renderer.hpp`, `.cpp`) — T9

A **stateless ray-cast volume renderer** (SPEC §3, FR-render.6) that consumes the
pure `volume/` math (SPEC §3: "VolumeRenderer (ray-cast GL draw; volume/ provides
the pure math)"). `VolumeRenderer::render(scene, camera, target)` receives all of
its data per call; the renderer owns only GL resources — its cached ray-cast
shader program, one shared full-screen quad VAO/VBO, and a 3D-texture cache keyed
by dataset pointer (each `data::VolumeDataset` is uploaded to the GPU once and
reused across instances and views). It implements the `IRenderer` dispatch
contract (`render/types.hpp`, SPEC §9 V2.3): its `IRenderer::render` forwards a
`Scene` holding a `VolumeScene` to this concrete method.

Public scene structs and constant (defined in `volume_renderer.hpp`):

| Type | Purpose |
|---|---|
| `VolumeInstance` | a `data::VolumeDataset`, the `volume::TransferFunction` mapping its scalar values to RGBA, and a model matrix. The dataset occupies the unit cube `[0,1]^3` in **model space**; the model matrix places/orients it in world space. |
| `VolumeScene` | a vector of `VolumeInstance`s (CPU-side; `app/` builds these). |
| `kDefaultStepLength` | the default ray-cast sampling step length in world units (`0.25`); the shader samples `floor(span / stepLength)` steps at their centers (FR-vol.3). |

`VolumeRenderer::render(scene, camera, target)`:

1. Binds the target framebuffer, sets the viewport, clears to the clear color,
   and leaves depth/blend off (v1 FBOs are color-only, SPEC §6 / docs/core.md).
2. For each volume, uploads (or reuses) its dataset as a `core::Texture3D`
   (`GL_R32F`, `GL_LINEAR` trilinear filtering, `GL_CLAMP_TO_EDGE`), uploads the
   transfer function's control points as uniforms, and draws the shared
   full-screen quad through the cached ray-cast shader.

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

## Guardrails observed

- **GL ownership**: `render/` is GL-call-free. Raw draw-state calls
  (`glViewport`, `glClear`, `glDrawElements`, `glBlitFramebuffer`, …) live in
  `core/draw.cpp`; raw
  readback (`glReadPixels`) lives in `core/read_pixels.cpp`; SSBO creation/
  binding/readback lives in `core/storage_buffer.cpp`; transform-feedback
  creation/capture/readback lives in `core/transform_feedback.cpp`; image
  binding + memory barriers live in `core/draw.cpp` (all under `core/`). The
  only readback consumers are tests (pixel reads,
  `LinkedListOIT::readCapturedFragmentCount`,
  `SliceRenderer::captureCrossSection`) — guardrail `no_production_readback`.
- **Stateless + dependency inversion**: `render()` takes all data per call;
  renderers depend on `IMaterial` / `ITransparencyPipeline` abstractions and
  expose the `IRenderer` dispatch contract (SPEC §9 V2.3). GPU geometry is
  injected: the mesh-family renderers take a shared `AssetRegistry*` and resolve
  scene `AssetHandle`s through it (SPEC §9 V2.5) — never a raw GL call, never a
  per-renderer duplicate upload.
- **Typed diagnostics**: draw/geometry failures return `data::Result` — no
  exceptions, no silent failure.
- **Deterministic / single-threaded**: one render thread; the v1 opaque pass is
  a fixed deterministic lighting configuration.
- **Doxygen** on all public API (SPEC §5).
