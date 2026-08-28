# RenderEngine

A C++20 real-time rendering engine built on **OpenGL 4.6 core** with **CMake**,
developed and run on Ubuntu inside WSL on Windows. See `SPEC.md` for the full
specification and `TASKS.md` for the sequential task plan.

## Dependencies

All third-party dependencies are pinned and fetched at configure time via CMake
`FetchContent` with a `GIT_TAG` (release tag or commit SHA — never a branch):

| Dependency | Version / pin |
|---|---|
| GLFW | `3.4` |
| glad2 | `v2.0.8` (commit `73db193`) |
| GLM | `1.0.1` |
| Dear ImGui | `v1.92.9` |
| GoogleTest | `v1.15.2` |
| spdlog | `v1.14.1` |
| stb | commit `2c980bb5` |

## Launch prerequisite (IMPORTANT)

The loop gate reads two environment variables (SPEC §8, R15). You **must**
source the environment before building/testing:

```sh
source tools/env.sh
```

`tools/env.sh` exports:
- `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` — tells the
  mechanical audit (`tools/audit.sh`) where the project's source lives (see `tools/env.sh:6` and `docs/spec/env.md` §8; `examples/` is intentionally NOT in `AUDIT_SOURCE_DIRS` — `examples/minimal.cpp` is consumer sample, excluded from `comment_tag_context` via waiver; `utils`/`test_utils` host offscreen/PixelReader facades).
- `LOOP_BUILD_TEST_CMD` — the exact CMake build+test command the gate runs.

If you skip `source tools/env.sh`, the R15 gate test fails loudly (it asserts
both variables) instead of silently auditing the wrong directories.

## Build & test

Configure, build, and run the full test suite (with ASan+UBSan on the test
binary) in one command:

```sh
source tools/env.sh
cmake -S . -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

`$LOOP_BUILD_TEST_CMD` expands to exactly the three commands above, so you can
also run `eval "$LOOP_BUILD_TEST_CMD"` from the repository root.

### Convenience scripts (tools/)

The four scripts below source `tools/env.sh` for you, so no manual setup is
needed:

| Script | Purpose |
|---|---|
| `tools/build.sh [target...]` | Configure + build all targets (or pass targets through to `cmake --build`) |
| `tools/test.sh` | Configure, build, and run the full test suite headless (`ctest --output-on-failure`) |
| `tools/run_sample.sh <name>` | Build and run one sample interactively: `mesh plane volume slice oit mpr` |
| `tools/clean.sh` | Remove only `build/` (kept scoped; never touches source or other dirs) |

### Requirements

- CMake >= 3.24
- A C++20 compiler (GCC 12+ / clang)
- Python 3 (used by glad2 to generate the GL loader at configure time)
- GL dev headers: `libgl1-mesa-dev`, `libegl1-mesa-dev`, `libx11-dev`,
  `libxrandr-dev`, `libxcursor-dev`, `libxi-dev`, `libxinerama-dev`
  (GLFW build deps).

## Engine facade (80% visualization, SPEC §3, TASKS T1)

```cpp
#include "render_engine/engine.hpp"
re::viz::Engine engine;
auto id = engine.addMesh("data/meshes/bunny.obj",
                         glm::mat4(1.0f),
                         glm::vec4(0.85f, 0.45f, 0.15f, 1.0f)).value();
re::scene::Camera cam(glm::vec3(0,0,3), glm::vec3(0,0,0), glm::vec3(0,1,0));
cam.setPerspective(60.0f, 800.0f/600.0f, 0.1f, 10.0f);
engine.setView({{0,0,800,600}, cam, {id}});
auto fb = re::core::Framebuffer::create().value(); // + Texture2D attach
engine.render(fb).value(); // sync → renderAll → presentAll, 1/255 vs direct AppContext
// helper: auto view = re::viz::Engine::createView({0,0,800,600}, cam, {id});
```

See `docs/engine.md` for the full facade docs and the `examples/minimal.cpp`
20-line copy-paste. Advanced users keep `engine.appContext()` / `engine.store()`.

## Module layout

```
include/    public facade headers (render_engine/engine.hpp — viz::Engine, SPEC §3)
io/         loaders only, no GL
data/       CPU containers + GL-free typed Result<T,E> (data/result.hpp), no GL
volume/     pure math (sampling, transfer function, ray-cast compositing), no GL
scene/      app-side scene description — GL-free, RE-free (View, Camera, SceneObject, SceneStore)
core/       GL foundation: offscreen context fixture, RAII GL objects — the SOLE
            owner of raw GL calls (core/rhi/gl/ after T10)
broker/     scene → render mediation — one IMapper<AppT,ReT> per file, ViewBridge façade
render/     one class per rendering technique (uses core/ wrappers), RE-minimal handles
app/        compositions + samples (via scene + broker IViewBridge, never render directly)
utils/      offscreen context + PixelReader facades delegating to core/
test_utils/ peer test-support lib (PixelReader via REContext, empty until T18)
tests/      headless unit tests (consume core/ wrappers + the utils/test_utils fixture)
examples/   consumer samples (minimal.cpp via Engine facade) — NOT in AUDIT_SOURCE_DIRS
```

## Testing notes

- Unit tests build with **ASan + UBSan** and run **headless** using an offscreen
  OpenGL 4.6 core context (hidden GLFW window; EGL-surfaceless fallback when no
  display is available). The test environment forces the deterministic Mesa
  **llvmpipe** software driver with `MESA_GL_VERSION_OVERRIDE=4.6` (SPEC §8), so
  the gate asserts the 4.6-core target on a leak-clean software driver; on WSLg
  the native Mesa D3D12 driver exposes GL 4.6 core as well. Test shaders use
  **GLSL 450** (llvmpipe's GLSL compiler caps at 4.50; a 4.6 core context
  accepts 4.50 shaders) — see SPEC §8.
- GL-touching tests consume GL only through `core/` wrappers — raw GL calls stay
  under `core/` (guardrail `gpu_api_ownership`).
- No C++ exceptions in v1; failures are reported via the typed `data::Result`
  type (SPEC §5).
