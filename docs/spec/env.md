# SPEC §8 — Environment requirements

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §8" mean this file.

## 8. Environment requirements

### System packages (not in repo, provisioned by /loop-setup)
- GCC/G++ (>= 12, for C++20), `cmake` (>= 3.24), `ninja` (optional), `git`,
  `clang-format` (T1 ships the config; NAMING_CONVENTIONS §7 enforces it),
  `xvfb` (required for T12/T13 sample smoke under WSLg fallback — headless gate).
  Host verified on **Ubuntu 22.04/WSL2, GCC 12.3**; `libgl1-mesa-dev` provides
  `libglx-dev` alias (either package satisfies `glxinfo` verification).
    Provision (spec-review #12, iteration 1 #9, iteration 6 #8): `sudo apt update && sudo apt install -y gcc g++ cmake ninja-build git clang-format xvfb libgl1-mesa-dev libegl1-mesa-dev libx11-dev libxrandr-dev libxcursor-dev libxi-dev libxinerama-dev mesa-utils curl wget python3 python3-jinja2 pkg-config ccache` (`grep` must be GNU grep with `-P` PCRE for `tools/audit.sh:141` `DEPS_PZO_OK`, not BusyBox — iteration 6 #8) (or `~/.local/bin/ccache` static binary if no sudo; `tools/configure.sh` auto-detects; `python3-jinja2` required for `glad` `Jinja` templating — iteration 1 #9). Mesa pinned `mesa-utils >=22` (spec-review #12: `glxinfo | grep "OpenGL core profile version"` must show 4.6 core on `llvmpipe` with `MESA_GL_VERSION_OVERRIDE=4.6`); llvmpipe 22 vs 23 difference affects `152 MB` OIT budget calc `157286400 B` (`640×480×16×32`), so setup verifies `glxinfo | grep "OpenGL core profile version string: 4.6"` after `tools/env.sh` source and documents `152 MB` tolerance `MESA_GL_VERSION_OVERRIDE=4.6 GALLIUM_DRIVER=llvmpipe` deterministic path — iteration 1 #9.
- GL dev headers: `libgl1-mesa-dev` (alias `libglx-dev`), `libegl1-mesa-dev`, `libx11-dev`,
  `libxrandr-dev`, `libxcursor-dev`, `libxi-dev`, `libxinerama-dev`
  (GLFW build deps), plus mesa GL drivers for WSLg (`mesa-utils` for
  `glxinfo` verification).
- Display: WSLg on Windows 11 (native) for interactive samples. For headless
  test runs: no display needed (offscreen GL context). `xvfb` is a **REQUIRED**
  package — the sample smoke gates (T12/T13) run under WSLg when present,
  otherwise under `xvfb`.
- Build tools: `curl`/`wget` (asset fetch at setup), `grep` (GNU, with `-P` PCRE for `tools/audit.sh:141` `DEPS_PZO_OK` `grep -PRzo` multiline hard floor — BusyBox grep would fail, iteration 5 #6), `python3 >=3.10` (conversion
   tooling for NRRD downsample at setup — `tools/convert_nrrd.py`, **stdlib
   only, no pip deps**; `python3 --version` ≥3.10 required, verified stdlib-only
   via `python3 -c "import sys; print(sys.version)"`), `python3-jinja2` (required for `glad_add_library` `Jinja` templating at `CMakeLists.txt:147-150` — `glad` cmake probes `python3` + `Jinja`, stdlib alone insufficient — iteration 1 #9), `unzip`.
  - **Verification (iteration 2 #8, iteration 13 #4 fix — `grep -q "3\.1[0-9]\."` would miss `3.20+` or `3.10` without trailing dot; `python3 --version` on 3.13 outputs `Python 3.13.2`):** `python3 -c "import sys; assert sys.version_info>=(3,10)"` (≥3.10, stdlib-only, portable) + `sha256sum -c <<<"cec7d6356631cbeaff1139f6ebcdbbad52b1d349dec2a74ad8b55c62b7668b56  tools/convert_nrrd.py"` (reproducibility gate — canonical SHA in `assets.md:59`, `env.md:27` cites `assets.md:59` per iteration 14 #4 DRY, not duplicating hex) + `glxinfo | grep "OpenGL core profile version string: 4.6"` under `MESA_GL_VERSION_OVERRIDE=4.6 GALLIUM_DRIVER=llvmpipe` (llvmpipe 22 vs 23 `152 MB` budget `157286400 B` deterministic path).
- `pkg-config` (for `glfw` fallback probe, not used — `FetchContent` `GIT_TAG 3.4` is source), `libglfw3-dev` system alternative not used; **system `libglm-dev` / `nlohmann-json3-dev` not used — `glm`/`json` are `FetchContent` `GIT_TAG` pinned per SPEC §2 (`pkg-config` json/glm fallback not used, see `docs/spec/techstack.md` §2)**
- `ccache` (recommended, not required) — compiler cache so rebuilds after
  `tools/clean.sh` (or spurious recompiles) hit cached objects. If the system
  package is unavailable (no sudo), a user-local static binary under
  `~/.local/bin/ccache` is fine; `tools/configure.sh` auto-detects it via
  PATH and wires it in as the CMake compiler launcher.
- **Leak-gate env:** `MESA_GL_VERSION_OVERRIDE=4.6` and `GALLIUM_DRIVER=llvmpipe`
  are set in `tools/env.sh` for `ctest` leak gate — do not override; running
  the gate on `d3d12` path yields nondeterministic LSAN attribution per `nfr.md:5`.
  `ASAN_OPTIONS`/`LSAN_OPTIONS` suppressions for llvmpipe/d3d12 are documented in `docs/spec/nfr.md` T12 and `tools/env.sh` (suppression file `tools/lsan.supp` when present).
  LSAN suppressions for llvmpipe/Wayland driver noise live in `tools/lsan.supp` (not for project code): `leak:<unknown module>` plus `libwayland`, `libdecor`, `libfontconfig`, `libpango`, `libcairo`, `libgtk-3`, `_glfwInitEGL`, `eglInitialize`, `glfwCreateWindow`, `libxkbcommon`, `libX11` — at least 8 `^leak:` lines covering `<unknown module>`, Wayland, decor, fontconfig, pango, cairo, gtk and EGL; `tools/env.sh:15` exports `LSAN_OPTIONS=suppressions=$ROOT/tools/lsan.supp` when the file exists (absolute `$ROOT` path, fix #10). Genuine project leaks (e.g. missed `glDeleteTextures`) remain unsuppressed and abort under `ASAN_OPTIONS=detect_leaks=1:abort_on_error=1`.

### Toolchain
- Compiler: GCC 12+ (Ubuntu default on 22.04+).
- CMake >= 3.24 (FetchContent + GIT_TAG pinning).

### Environment variables
- `DISPLAY` (WSLg auto; X server fallback only if not W11).
- `RE_GLSL_VERSION` — shader language level (`450` portable floor, `460` hardware, see `RE_GLSL_VERSION` ceiling note below; not an env var but a macro in `core/glsl_version.hpp`)
- `LOOP_BUILD_TEST_CMD` — must be set to the build+test command when
  launching the loop (runner needs it; default runner logic knows CMake, but
  set explicitly). `tools/env.sh` sets it to `tools/configure.sh && cmake
  --build build -j$(nproc) && ctest --test-dir build --output-on-failure`,
  i.e. conditional configure + incremental build + full suite.
- `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` — required for
  audit ownership rules to see our non-default layout.
- `MESA_GL_VERSION_OVERRIDE=4.6` + `GALLIUM_DRIVER=llvmpipe` — required leak-gate driver (per `nfr.md:33` + `tools/env.sh`; deterministic LLVMPipe, not d3d12). Do not override.
- `ASAN_OPTIONS` / `LSAN_OPTIONS` + suppression file `tools/lsan.supp` absolute (`$ROOT/tools/lsan.supp` via `ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"` in `tools/env.sh:15` — not `$(pwd)/tools/lsan.supp` brittle when sourced from `build/` subdir, fix #10).
- `ccache` — optional compiler cache; `tools/configure.sh` auto-detects `ccache` on `PATH` and wires as CMake launcher (see `nfr.md` incremental builds).

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
