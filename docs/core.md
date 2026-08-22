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
| `core::Texture2D` | 2D texture (`GL_TEXTURE_2D`) | `bind(unit)`, `upload(w, h, rgba8)`, `unbind(unit)` |
| `core::Framebuffer` | FBO | `bind()`, `attachColor(texture)`, `isComplete()`, `unbind()` |

Design notes:

- **v1 vertex data is float-only**, so `VertexArray::setAttribute` fixes the
  element type to `GL_FLOAT` and exposes the component count. Attribute
  configuration requires a bound `GL_ARRAY_BUFFER` (captured by the VAO), per
  the GL spec.
- **v1 textures are RGBA8** (4 bytes/pixel). `upload()` sets `GL_LINEAR`
  filtering and `GL_CLAMP_TO_EDGE` wrapping, so an uploaded texture (no mipmaps)
  is complete and directly attachable to a framebuffer.
- **v1 FBOs are color-only**: `attachColor()` attaches to
  `GL_COLOR_ATTACHMENT0` (default draw/read buffer), so rendering to an FBO
  goes to that attachment. A color-only FBO whose attachment is a complete
  texture is `GL_FRAMEBUFFER_COMPLETE` (`isComplete()`), which is what the T3
  gate asserts.
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
