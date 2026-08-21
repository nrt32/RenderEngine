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
> (`SliceRenderer`, the geometry-shader plane clip of a mesh, FR-render.4). It
> is part of the `docs/render.md` documentation map (T7/T8/T9/T10/T11; later
> tasks extend it).

## Components

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

### `MeshRenderer` (`render/mesh_renderer.hpp`, `.cpp`)

A **stateless opaque forward-pass** renderer (SPEC §3 "Stateless renderers"):
`render(scene, camera, target)` receives all of its data per call. The renderer
owns only GL resources — its cached opaque shader program and the GPU geometries
of the meshes it has drawn (keyed by mesh pointer). One mesh can be drawn by
several views without duplication.

Public scene structs (defined in `mesh_renderer.hpp`):

| Type | Purpose |
|---|---|
| `Camera` | `view` / `proj` matrices + `position` (eye, for view-direction terms). |
| `RenderTarget` | a color-only `core::Framebuffer` + pixel size + clear color. |
| `MeshInstance` | a `data::Mesh`, its `IMaterial`, and its model matrix. |
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
resources — its cached clip shader program (vertex + geometry + fragment), its
transform-feedback capture program + object, and the GPU geometries of the
meshes it has drawn (keyed by mesh pointer, shared with the mesh family).

Public scene structs (defined in `slice_renderer.hpp`; reuses `MeshInstance` /
`MeshScene`-style `SliceScene` from `mesh_renderer.hpp`):

| Type | Purpose |
|---|---|
| `ClipPlane` | the slice plane in **world space**: a unit `normal` + a `point` on the plane. The kept side is `dot(normal, p - point) >= 0`. |
| `SliceScene` | a vector of `MeshInstance`s to clip (CPU-side; `app/` builds these). |

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
across plane instances and views).

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
reused across instances and views).

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
  (`glViewport`, `glClear`, `glDrawElements`, …) live in `core/draw.cpp`; raw
  readback (`glReadPixels`) lives in `core/read_pixels.cpp`; SSBO creation/
  binding/readback lives in `core/storage_buffer.cpp`; transform-feedback
  creation/capture/readback lives in `core/transform_feedback.cpp`; image
  binding + memory barriers live in `core/draw.cpp` (all under `core/`). The
  only readback consumers are tests (pixel reads,
  `LinkedListOIT::readCapturedFragmentCount`,
  `SliceRenderer::captureCrossSection`) — guardrail `no_production_readback`.
- **Stateless + dependency inversion**: `render()` takes all data per call;
  renderers depend on `IMaterial` / `ITransparencyPipeline` abstractions.
- **Typed diagnostics**: draw/geometry failures return `data::Result` — no
  exceptions, no silent failure.
- **Deterministic / single-threaded**: one render thread; the v1 opaque pass is
  a fixed deterministic lighting configuration.
- **Doxygen** on all public API (SPEC §5).
