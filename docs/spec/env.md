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

### Toolchain
- Compiler: GCC 12+ (Ubuntu default on 22.04+).
- CMake >= 3.24 (FetchContent + GIT_TAG pinning).

### Environment variables
- `DISPLAY` (WSLg auto; X server fallback only if not W11).
- `LOOP_BUILD_TEST_CMD` — must be set to the CMake build+test command when
  launching the loop (runner needs it; default runner logic knows CMake, but
  set explicitly).
- `AUDIT_SOURCE_DIRS="io data volume core render app tests"` — required for
  audit ownership rules to see our non-default layout.

### Convenience scripts (tools/)
The repo ships thin wrappers around the §8 build/test/env contract so manual
sessions never have to reconstruct it: `tools/build.sh [target...]`
(configure + build), `tools/test.sh` (configure + build + `ctest
--output-on-failure`, exactly `eval "$LOOP_BUILD_TEST_CMD"`), `tools/run_sample.sh
<mesh|plane|volume|slice|oit|mpr>` (build + run one sample interactively;
requires WSLg/X display), and `tools/clean.sh` (removes only `build/`). Each
sources `tools/env.sh` itself and is non-authoritative: the loop gate still uses
`tools/env.sh` + `LOOP_BUILD_TEST_CMD` as its single source of truth.

### GL/GPU notes
- WSLg exposes OpenGL via Mesa; target GL 4.6 core (the D3D12 gallium driver reports
  core 4.6 natively; llvmpipe caps at 4.5). Verify with `glxinfo -l`.
- **Test GLSL level is GLSL 450, not 460.** The headless gate runs on the
  deterministic software driver (llvmpipe) so LeakSanitizer attribution is stable
  and leak-clean (see the leak-gate note below). llvmpipe's GLSL compiler supports
  up to **4.50** — not 4.60 — and `MESA_GL_VERSION_OVERRIDE=4.6` only raises the
  *reported* context version, never the GLSL level, so `#version 460` shaders
  fail to compile with `GLSL 4.60 is not supported...`. A GL 4.6 core context
  accepts GLSL 4.50 shaders, so **gate/test shaders are written `#version 450`**
  — the portable level that compiles on both llvmpipe and d3d12. GLSL 460 remains
  the target for hardware-driven sample shaders on the native d3d12 path.
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
- Tests create an offscreen GL context (hidden GLFW window; EGL-surfaceless
  fallback) so the gate never needs a display.