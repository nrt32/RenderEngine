# SPEC §8 — Environment requirements

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §8" mean this file.

## 8. Environment requirements

### System packages (not in repo, provisioned by /loop-setup)
- GCC/G++ (>= 12, for C++20), `cmake` (>= 3.24), `ninja` (optional), `git`,
  `clang-format` (T1 ships the config; NAMING_CONVENTIONS §7 enforces it).
- GL dev headers: `libgl1-mesa-dev`, `libegl1-mesa-dev`, `libx11-dev`,
  `libxrandr-dev`, `libxcursor-dev`, `libxi-dev`, `libxinerama-dev`
  (GLFW build deps), plus mesa GL drivers for WSLg (`mesa-utils` for
  `glxinfo` verification).
- Display: WSLg on Windows 11 (native) for interactive samples. For headless
  test runs: no display needed (offscreen GL context). `xvfb` is a **REQUIRED**
  package — the sample smoke gates (T12/T13) run under WSLg when present,
  otherwise under `xvfb`.
- Build tools: `curl`/`wget` (asset fetch at setup), `python3` (conversion
  tooling for NRRD downsample at setup — `tools/convert_nrrd.py`, **stdlib
  only, no pip deps**), `unzip`.
- `ccache` (recommended, not required) — compiler cache so rebuilds after
  `tools/clean.sh` (or spurious recompiles) hit cached objects. If the system
  package is unavailable (no sudo), a user-local static binary under
  `~/.local/bin/ccache` is fine; `tools/configure.sh` auto-detects it via
  PATH and wires it in as the CMake compiler launcher.

### Toolchain
- Compiler: GCC 12+ (Ubuntu default on 22.04+).
- CMake >= 3.24 (FetchContent + GIT_TAG pinning).

### Environment variables
- `DISPLAY` (WSLg auto; X server fallback only if not W11).
- `LOOP_BUILD_TEST_CMD` — must be set to the build+test command when
  launching the loop (runner needs it; default runner logic knows CMake, but
  set explicitly). `tools/env.sh` sets it to `tools/configure.sh && cmake
  --build build -j$(nproc) && ctest --test-dir build --output-on-failure`,
  i.e. conditional configure + incremental build + full suite.
- `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils tests"` — required for
  audit ownership rules to see our non-default layout.

### Convenience scripts (tools/)
The repo ships thin wrappers around the §8 build/test/env contract so manual
sessions never have to reconstruct it. All builds are **incremental/cached**:
`tools/configure.sh` (shared helper) runs the CMake configure step only when
needed — no `build/CMakeCache.txt` yet, or some `CMakeLists.txt`/`*.cmake`
newer than it — and wires `ccache` in as the C/C++ compiler launcher when it
is on PATH (the launcher persists in `CMakeCache.txt`, so later configures
keep it). `cmake --build` then rebuilds only what changed, with recompiles
served from the ccache.

- `tools/build.sh [target...]` — conditional configure + incremental build.
- `tools/test.sh` — conditional configure + incremental build + `ctest
  --output-on-failure`; exactly `eval "$LOOP_BUILD_TEST_CMD"`.
- `tools/run_sample.sh <mesh|plane|volume|slice|oit|mpr>` — build + run one
  sample interactively (requires WSLg/X display).
- `tools/clean.sh [--ccache]` — removes only `build/` (the ccache survives, so
  the next build recompiles from cache); `--ccache` also clears the compiler
  cache + stats.

Each script sources `tools/env.sh` itself and is non-authoritative: the loop
gate still uses `tools/env.sh` + `LOOP_BUILD_TEST_CMD` as its single source of
truth.

### GL/GPU notes
- WSLg exposes OpenGL via Mesa; target GL 4.6 core (the D3D12 gallium driver reports
  core 4.6 natively; llvmpipe caps at 4.5). Verify with `glxinfo -l`.
- **GLSL 450/460 ceiling and the `RE_GLSL_VERSION` profile macro (SPEC §9 V2.7).**
  The headless gate runs on the deterministic software driver (llvmpipe) so
  LeakSanitizer attribution is stable and leak-clean (see the leak-gate note
  below). llvmpipe's GLSL compiler supports up to **4.50** — not 4.60 — and
  `MESA_GL_VERSION_OVERRIDE=4.6` only raises the *reported* context version,
  never the GLSL level, so `#version 460` shaders fail to compile with
  `GLSL 4.60 is not supported...`. A GL 4.6 core context accepts GLSL 4.50
  shaders, so **gate/test shaders are written `#version 450`** — the portable
  level that compiles on both llvmpipe and d3d12. GLSL 460 remains the target
  for hardware-driven sample shaders on the native d3d12 path.
  The shader language level is controlled by the single macro **`RE_GLSL_VERSION`**
  (`core/glsl_version.hpp`): **450 = portable floor (tests/CI, llvmpipe)**, **460
  = hardware floor (native d3d12 / desktop GL)**. In the gate env the macro
  expands to `#version 450 core` (static_assert + fixture-shader compile on
  llvmpipe); the 460/hardware compile is a manual sample verification, not a
  gate assertion, because llvmpipe caps at GLSL 4.50. Every `.glsl` file under
  `render/shaders/` heads with the line produced by `RE_GLSL_VERSION_LINE`, so
  the version is a single `#version` concern (V2 T7 → V2 T8).
- **Leak-gate driver is llvmpipe, not d3d12.** The native d3d12 driver
   `dlclose()`s its closed DSOs (`libd3d12core`, `libdxcore`, `libnvwgf2umx`,
   `/dev/zero` pages) before LeakSanitizer attributes leaks, so attribution is
   nondeterministic and its ~66 KB driver pool surfaces as `<unknown module>`,
   which cannot be suppressed without also masking real engine leaks (an
   allocation made from an `lp::` frame is still reported). The leak gate
   therefore runs on `GALLIUM_DRIVER=llvmpipe` (driver DSOs stay mapped;
   attribution is stable), where only third-party windowing/runtime allocations
   are suppressed. The T1 gate asserts the 4.6-core target on that llvmpipe env
   via `MESA_GL_VERSION_OVERRIDE=4.6`.
- Tests create an offscreen GL context (hidden GLFW window; per-OS no-display
   fallback — see below) so the gate never needs a display.

### Offscreen context backend selection (utils::OffscreenContext, SPEC §9 V2.2)

`utils::OffscreenContext::create()` always tries the hidden GLFW window first
(primary on every OS). When no display server is available the no-display
fallback is chosen **deterministically at compile time via platform macros**
(SPEC §9 V2.2; replaces the former Mesa-only `EGL_PLATFORM_SURFACELESS_MESA`
hardcode). The selection is exposed for testing through
`OffscreenContext::platformNoDisplayBackend()` — the same constant controls the
actual fallback path in `utils/offscreen_context.cpp`.

| OS | Primary (always tried) | No-display fallback (when GLFW fails) |
|---|---|---|
| **Linux** | GLFW hidden window (1×1, GL 4.6 core) | **EGL surfaceless** via `EGL_PLATFORM_SURFACELESS_MESA` (Mesa/llvmpipe) |
| **Windows** | GLFW hidden window (1×1, GL 4.6 core) | **ANGLE-EGL** (EGL via ANGLE) or **WGL** no-display (native) — deterministic primary is `Wgl` |
| **macOS** | GLFW hidden window (1×1, GL 4.6 core) | **CGL** (Core OpenGL) no-display |

On Linux the path is unchanged: the EGL-surfaceless backend creates a GL 4.6
core context (the llvmpipe `MESA_GL_VERSION_OVERRIDE=4.6` override still reports
4.6; the probe via `core::loadCoreGl` asserts `major==4 && minor==6 && core
profile`). The per-OS factory keeps the `EGL_PLATFORM_SURFACELESS_MESA` token
scoped to the Linux build, while Windows and macOS builds compile their
respective WGL/CGL/ANGLE-EGL paths. The gate `T5` asserts that
`platformNoDisplayBackend()` equals the expected enum for the macros active on
the build host and that the llvmpipe context is still GL 4.6 core.
