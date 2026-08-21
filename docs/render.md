# render/ — rendering techniques

`render/` is the **rendering-techniques module** (SPEC §3): ONE class per
rendering technique behind a unified `IRenderer`-style set of stateless
renderers. It consumes GL only through `core/` wrappers and the `core::Draw`
API (guardrail `gpu_api_ownership`); it depends on `IMaterial` /
`ITransparencyPipeline` abstractions, never concrete material/OIT classes
(dependency inversion, SPEC §3).

> This page documents the **T7 deliverable** (`IMaterial` + `PhongMaterial`,
> the `ITransparencyPipeline` interface, the shared `MeshGeometry`, and the
> `MeshRenderer` opaque forward pass, FR-render.1/3) and the **T8 deliverable**
> (`PlaneRenderer` textured quads/planes, FR-render.5). It is part of the
> `docs/render.md` documentation map (T7/T8; later tasks extend it).

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
confirm the pipeline stays off for opaque scenes. `LinkedListOIT` (the v1
implementation) arrives in T10.

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
   color. Transparent meshes are skipped by the opaque pass (their compositing
   is the pipeline's job, T10).

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

## Guardrails observed

- **GL ownership**: `render/` is GL-call-free. Raw draw-state calls
  (`glViewport`, `glClear`, `glDrawElements`, …) live in `core/draw.cpp`; raw
  readback (`glReadPixels`) lives in `core/read_pixels.cpp` (both under `core/`).
- **Stateless + dependency inversion**: `render()` takes all data per call;
  renderers depend on `IMaterial` / `ITransparencyPipeline` abstractions.
- **Typed diagnostics**: draw/geometry failures return `data::Result` — no
  exceptions, no silent failure.
- **Deterministic / single-threaded**: one render thread; the v1 opaque pass is
  a fixed deterministic lighting configuration.
- **Doxygen** on all public API (SPEC §5).
