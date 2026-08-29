# SPEC §2 — Tech-stack decisions

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §2" mean this file.

## 2. Tech-stack decisions

| Decision | Choice | Rationale |
|---|---|---|
| Language | **C++20** | Modern, broadly supported; GCC 12+ on Ubuntu/WSL with no extra toolchain setup |
| Compiler | GCC (Ubuntu default toolchain) | Standard on Ubuntu/WSL; selected at setup time |
| Build system | **CMake (>= 3.24)** | Runner's build+test gate supports CMake natively |
| GPU API | **OpenGL 4.6 core** (GLSL 460) | The WSL Mesa D3D12 driver exposes GL 4.6 core natively; provides VAOs, FBOs, modern shaders for OIT + ray casting |
| GL loader | **glad2 v2.0.8** (GL 4.6 core generator) | Generated at configure time via FetchContent; pinned release tag `v2.0.8` (commit `73db193f853e2ee079bf3ca8a64aa2eaf6459043` full 40-char, short `73db193` — iteration 1 #6 pins full SHA, `GIT_TAG v2.0.8` verified `CMakeLists.txt:82` + `git ls-remote` SHA `73db193f853e2ee079bf3ca8a64aa2eaf6459043`) |
| Windowing | **GLFW 3.4** (commit `7b6aead9fb88b3623e3b3725ebb42670cbe4c579`) | Standard; WSLg displays GLFW windows natively; `GIT_TAG 3.4` pinned (commit `7b6aead`, spec-review #10) |
| Math | **GLM 1.0.1** (commit `0af55ccecd98d4e5a8d1fad7de25ba429d60e863`) | De-facto GLSL-compatible math lib; header-only; `GIT_TAG 1.0.1` pinned (commit `0af55cc`, spec-review #10) |
| GUI | **Dear ImGui v1.92.9** (commit `01380c579715e62fb9a8d6ec0502c4ea83bfde6e`) | Immediate-mode, tiny footprint, OpenGL3 backend, ideal for MPR viewport + panels; `GIT_TAG v1.92.9` pinned (commit `01380c5`, spec-review #10) |
| Unit tests | **GoogleTest v1.15.2** (`b514bdc898e2951020cbdca1304b75f5950d1f59` — `GIT_TAG v1.15.2`) | Strong enforcement-style assertions (user requirement) |
| Logging | **spdlog v1.14.1** (commit `27cb4c76708608465c413f6d0e6b8d99a4d84302` via `git ls-remote --tags`, iteration 4 #1) | Lightweight OSS logging: trace/debug/info/warn/error/fatal; console/file sinks |
| Textures | **stb_image** — `https://github.com/nothings/stb` — commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` — Public Domain — `FetchContent GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20` | Single-header; loads textures and can write outputs; pinned commit via FetchContent, verified SHA `2c980bb59875b0d32144a71867fbdebb2f77cd20` |
| Serialization | **nlohmann/json 3.11.3** — `https://github.com/nlohmann/json` — tag `v3.11.3` (commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`) — MIT — `FetchContent GIT_TAG v3.11.3` | JSON text wire for `SceneStore::serialize()` + `LayoutSpec` + `MaterialDesc`/`LightDesc` variant JSON; binary NRRD raw blob + SHA-256 `contentHash` beside it — see `assets.md:64` §7 addendum decision Q47:A (2026-08-23) |
| Dependency acquisition | **CMake FetchContent, pinned GIT_TAG** | Self-contained, reproducible; feeds dependency-lock guardrail |
| Test binary | **ASan + UBSan** enabled | Memory-leak + UB detection in the gate |
| GL-touching tests | **Offscreen GL context** (hidden GLFW window; EGL-surfaceless fallback) | Unit tests exercise real GL paths headless, under sanitizers |

### Platform / environment notes
- Host: **Ubuntu inside WSL on Windows**; display via **WSLg** (Windows 11).
- Samples require a display (WSLg); tests run **headless** (offscreen GL context).
- Environment (packages, toolchain, env vars) enumerated in §8 and executed by the SETUP phase.