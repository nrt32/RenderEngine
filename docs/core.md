# core/ — GL foundation

`core/` is the **GL foundation** module (SPEC §3). It is the **sole owner of
raw `glXxx(...)` calls** in RenderEngine (guardrail `gpu_api_ownership`):
`render/`, `app/`, and `tests/` consume GL only through these wrappers, while
`io/`, `data/`, and `volume/` are GL-free. This single-dir anchor is what makes
the GL-ownership audit rule mechanically enforceable.

> This page documents the T3 deliverable: the RAII GL objects and the
> `ShaderProgram`. It is part of the `docs/core.md` documentation map (T3).

## Components

### Raw-GL anchors

The offscreen GL context itself moved to `utils/` in V2.1 (SPEC §9):
`utils::OffscreenContext` (T1) creates a hidden GLFW window + GL 4.6 core
context, falling back to an EGL-surfaceless context when no display is
available. What stays in `core/` are the **raw-GL anchors** the context (and
`core::Window`) delegate to:

- `core::loadCoreGl` — loads GL entry points (glad) and probes the
  version/profile via `glGetIntegerv` (not the unreliable `glGetString` text);
- `core::readRgba8` — the raw pixel-readback anchor (SPEC §6,
  `no_production_readback`), surfaced to tests via `utils::PixelReader`.

Unit tests get one shared context via the `tests/` fixture (see
`utils/offscreen_context.hpp`).

### GL error state

`core::queryGlError()` / `core::hasPendingGlError()` expose the current GL error
state (`GL_GET_ERROR`) so tests can assert "no GL errors" without a raw
`glGetError` call. See `gl_error.hpp`.

### RAII GL objects (FR-core.1)

Each wrapper owns a GL object name and deletes it on destruction; all are
**movable but not copyable**. Creation goes through a typed
`static data::Result<T> create()` factory that returns an error if no GL
context is current (glad not loaded). A generated GL object name is never 0
(GL reserves 0), so `id() != 0` / `valid()` is an explainable
"the object was really created" invariant.

| Wrapper | GL object | Key operations |
|---|---|---|
| `core::VertexBuffer` | VBO (`GL_ARRAY_BUFFER`) | `bind()`, `upload(data, bytes, usage)`, `unbind()` |
| `core::ElementBuffer` | EBO (`GL_ELEMENT_ARRAY_BUFFER`) | `bind()`, `upload(u32 indices, count, usage)`, `unbind()` |
| `core::VertexArray` | VAO | `bind()`, `setAttribute(index, components, normalized, stride, offset)`, `unbind()` |
| `core::Texture2D` | 2D texture (`GL_TEXTURE_2D`) | `bind(unit)`, `upload(w, h, rgba8)`, `uploadDepth(w, h)`, `unbind(unit)` |
| `core::Framebuffer` | FBO | `bind()`, `attachColor(texture)`, `attachDepth(texture)`, `isComplete()`, `unbind()` |

Design notes:

- **v1 vertex data is float-only**, so `VertexArray::setAttribute` fixes the
  element type to `GL_FLOAT` and exposes the component count. Attribute
  configuration requires a bound `GL_ARRAY_BUFFER` (captured by the VAO), per
  the GL spec.
- **v1 textures are RGBA8** (4 bytes/pixel). `upload()` sets `GL_LINEAR`
  filtering and `GL_CLAMP_TO_EDGE` wrapping, so an uploaded texture (no mipmaps)
  is complete and directly attachable to a framebuffer. `uploadDepth(w, h)`
  instead allocates **depth storage** (`GL_DEPTH_COMPONENT24`, no client data —
  the rasterizer writes it) with `GL_NEAREST` + clamp; that flavor exists solely
  as the depth attachment of an offscreen target and is never shader-sampled.
- **FBOs are color-only by default**: `attachColor()` attaches to
  `GL_COLOR_ATTACHMENT0` (default draw/read buffer), so rendering to an FBO
  goes to that attachment. A color-only FBO whose attachment is a complete
  texture is `GL_FRAMEBUFFER_COMPLETE` (`isComplete()`). This remains the
  deterministic-gate default configuration of every analytic pixel test —
  painter's-order output is reproducible on software GL (llvmpipe).
  `attachDepth(texture)` adds an OPT-IN second attachment at
  `GL_DEPTH_ATTACHMENT` from an `uploadDepth()` texture; after that call
  `isComplete()` requires BOTH attachments to be valid, so a depth-enabled
  target asserts completeness WITH its depth attachment (a driver silently
  dropping the depth buffer fails loudly at creation, not silently at render
  time). Consumers: `render::ViewTarget` (`DepthMode::Enabled`) creates
  color+depth targets; everything else stays color-only.
- `BufferUsage` (`StaticDraw`/`DynamicDraw`/`StreamDraw`) maps to
  `GL_STATIC_DRAW` etc.

### ShaderProgram (FR-core.2)

`core::ShaderProgram::create(vertexSource, fragmentSource)` compiles both
stages, links them into a program, and returns a typed
`data::Result<ShaderProgram>`. Failures surface as a typed `data::Error` with:

- an enumerated `core::ShaderError` code (`VertexCompile`, `FragmentCompile`,
  `Link`) — no C++ exceptions (SPEC §5);
- a message built from the driver's info log, with **each diagnostic line
  normalized to the project's golden prefix `ERROR: `** followed by the
  driver's own location + message, e.g.
  `ERROR: 0:7(27): error: \`glibberish' undeclared`. The offending line is
  therefore always reported in the unambiguous `0:N` location form, which the
  FR-core.2 gate asserts as the golden substring `ERROR: 0:7`.

Usage: `use()` installs the program, `unuse()` uninstalls it, and
`setUniformInt` / `setUniformVec3` / `setUniformMat4` update uniforms (the
program must be in use).

**GLSL level.** Gate/test shaders are written **GLSL 450**, not 460 (SPEC §8):
the headless gate runs on llvmpipe, whose GLSL compiler caps at 4.50 — a
`#version 460` shader fails to compile there with `GLSL 4.60 is not
supported...`. A GL 4.6 core context accepts 4.50 shaders, so GLSL 450 is the
portable level that compiles on both llvmpipe and the native d3d12 driver. GLSL
460 remains the target for hardware-driven sample shaders on the native d3d12
path.

### Draw-state cache (SPEC §9 V2.10) & DrawContext instance (V3.2a Q43:B)

`core/draw.cpp` keeps an **internal dirty-flag cache** for the draw-state
wrappers (`setViewport`, `setClearColor`, `enableDepthTest`/`disableDepthTest`,
`enableBlend`/`disableBlend`, `enablePremultipliedOverBlend`). The free-function
`core::Draw` API and the audit anchors (`core::loadCoreGl`, `core::readRgba8`)
are unchanged — the cache is transparent to callers and to the mechanical
audit.

- `setViewport(x,y,w,h)` caches the last viewport rect; an identical rect is a
  cache hit and issues no `glViewport`.
- `setClearColor(r,g,b,a)` caches the last clear color (exact float equality);
  an identical color is a hit and issues no `glClearColor`.
- `enableDepthTest`/`disableDepthTest` and `enableBlend`/`disableBlend` cache
  the last enabled state per capability; a redundant enable/disable is a hit and
  issues no `glEnable`/`glDisable`. `enablePremultipliedOverBlend` caches both
  the `GL_BLEND` enable and the `glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`
  state, so a second identical call issues neither.
- Motivator: OIT mid-frame toggles that would otherwise redundantly re-issue
  `glEnable`/`glDisable`/`glBlendFunc` per transparent draw.

For testing, `core/draw.hpp` exposes a **test-injectable spy**:

- `core::DrawSpyCounts` — per-wrapper raw-GL call counts (only incremented on a
  cache miss);
- `core::getDrawSpyCounts()` / `core::resetDrawSpyCounts()` — inspect/reset the
  spy;
- `core::invalidateDrawCache()` — invalidate the cache (and reset the spy) so
  the next wrapper call always issues its `gl*` call. Tests call this between
  cases to avoid cross-test pollution.

Example gate assertion: `setClearColor(red); setClearColor(red)` issues exactly
**1** `glClearColor`; the same holds for `setViewport` and each
`enable*`/`disable*`.

#### DrawContext — instance per FrameContext (V3.2a, Q43:B SRP via instance)

`core::DrawContext` (`core/draw.hpp`, header-only value type) **replaces** the
global `static Cache g_cache` / `invalidateDrawCache()` with an **instance per
`FrameContext`** — SRP via instance (one reason to change per frame, not one
global mutable). The instance owns its own dirty-flag cache + spy:

- `DrawContext{Viewport,ClearColor,Depth,Blend,spy}` — value type, one per frame;
  duplicate `ctx.setViewport(0,0,640,480); ctx.setViewport(0,0,640,480)` issues
  exactly **1** `glViewport` (cache hit on the same instance), while a fresh
  `DrawContext` starts cold (no cross-frame bleed — instance isolation).
- Spy is per-instance (`ctx.getSpyCounts()`, `ctx.resetSpyCounts()`, `ctx.invalidate()`),
  not global — test determinism via spy per context (not global `getDrawSpyCounts()`).
- **`beginPass(framebuffer, width, height, r, g, b, a, depthTest = false)`** —
  THE pass prologue (renderer-consolidation deliverable): binds the target
  (null = window default FB), sets the viewport, installs + applies the clear
  color, then sets the DEPTH state and leaves blending disabled — in that
  fixed order. Every direct renderer `render()` entry point and the View
  composition owner (`View::render` — one clear per frame, layers never clear)
  begin their pass through this ONE method (the sequence exists exactly once,
  here). Passes that must not clear (OIT composite over opaque contents) do
  not call it. State transitions go through the instance cache: a fresh
  context issues each state call exactly once; identical repeat passes hit the
  cache.
- **Depth branch of the prologue.** The default `depthTest = false` disables
  the depth test — the deterministic painter's-order configuration all
  analytic pixel gates assert against on software GL. With `depthTest = true`
  (the per-view opt-in `render::View::setDepthTest`) the prologue instead
  ENABLES the depth test and clears the depth buffer to 1.0
  (`DrawContext::clearDepth`, `GL_DEPTH_BUFFER_BIT`), so a frame drawn into a
  color+depth target starts from a defined far depth and overlapping opaque
  geometry resolves by true occlusion (nearer fragment wins regardless of draw
  order). Clearing depth on an attachment-less FBO is silently ignored by GL,
  so the flag is safe to set unconditionally per view.
- Future `FrameContext{ DrawContext draw; Viewport viewport; ClearColor clearCol; }`
  (see `modules.md` RHI) will thread `DrawContext&` through `renderAll`/`drawLayer`
  so `core::Draw` façade delegates to `FrameContext::draw` (DIP). The global free-function
  API remains for V2 regression lock; new code migrates to `DrawContext` instance.

Gate: `DrawContext` duplicate `setViewport` → exactly 1 `glViewport` (`t2_skeletons_test.cpp`);
`invalidate()` resets cache+spy; two instances are independent (N>=1 consecutive green);
T17: exactly one `beginPass` definition in the tree, zero clear-prologue repeats under
`render/`, `<glad/gl.h>` under `render/` == 0 (`t17_renderer_consolidation_test.cpp`).
T18: the depth branch of the prologue drives the near-mesh-wins overlap gate — a view
with `depthTest = true` renders into a color+depth target and the nearer of two
anti-painter-ordered opaque meshes wins the overlap pixel within 1/255, while the same
arrangement on the default color-only pass keeps the later-drawn mesh
(`t18_depth_test.cpp`, N>=3).

### Logging

`core::initLogging()` configures the shared spdlog console logger (SPEC §5). It
is GL-free and lives in `core/` so every module shares one logging setup. See
`logging.hpp`.

## Guardrails observed

- **GL ownership**: every raw `glXxx` call sits in this module's `.cpp` files;
  headers and higher layers are GL-call-free.
- **Typed diagnostics**: creation, compile, and link failures return
  `data::Result` — never thrown exceptions, never silent.
- **Determinism / single-threaded**: all wrappers assume a current GL context
  on the calling thread (no concurrency in v1, SPEC §5).
