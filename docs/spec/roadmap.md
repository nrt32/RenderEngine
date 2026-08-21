# SPEC §9 — V2 future scope (roadmap)

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §9" mean this file. The numbered backlog
> mirrors `TASKS.md` (active backlog) — tasks 1–10 below are the same items.

## 9. V2 future scope (roadmap)

Decisions recorded for V2 during post-loop review. None are V1 requirements;
each is independently implementable behind the existing guardrails. Items that
share a dependency (e.g. the multi-view workstream) land together.

**Priority order (approved): product-first.** The multi-view workstream
(V2.3 → V2.4 → V2.5) delivers the product value first, then the low-risk
refactors (V2.1/V2.2/V2.10), then maintainability (V2.6/V2.7), then
portability (V2.8/V2.9).

| P | # | Item | Scope / rationale | Depends on |
|---|---|---|---|---|
| 1 | V2.3 | Narrow `IRenderer` interface + shared `render/types.hpp` | Move `Camera`/`RenderTarget` out of `mesh_renderer.hpp` into a shared header; a pure abstract `render` contract implemented by `Mesh/Plane/Volume/SliceRenderer`. Lands as the dispatch mechanism of the multi-view workstream (industry pattern: per-technique renderers, interfaces only where polymorphism helps). | — (foundation) |
| 2 | V2.4 | Multi-view rendering (Model B: per-view FBO + engine blit) | Front-end shares per-view window-section handles + abstract scene objects; RE dispatches objects to the correct renderer, renders into each view's own `core::Framebuffer`, then blits each FBO into its window rect via a new `core::blit`. No app-side viewport blending. Drives SceneView/MPRView composition. | V2.3, `core::blit` |
| 3 | V2.5 | Asset system: generational `AssetHandle` registry | `render::AssetRegistry` (`register()` → copyable `AssetHandle{index,generation}`); one GPU object per individual CPU object globally (fixes current per-renderer duplication: the same `data::Mesh` uploads once even across `MeshRenderer`+`SliceRenderer`). Handles are the currency views exchange. | V2.3, V2.4 |
| 4 | V2.1 | Move `offscreen_context` + `read_pixels` to a new `utils/` module | Test-support + windowing (GLFW/EGL) are not core rendering; keep the raw-GL anchors (`core::loadCoreGl`, `core::readRgba8`) under `core/` so `gpu_api_ownership` / `no_production_readback` stay intact; `PixelReader` class in `utils/`. **Requires adding `utils` to `AUDIT_SOURCE_DIRS`.** | — |
| 5 | V2.2 | Platform-extensible context-backend factory | `utils::OffscreenContext` selects the no-display backend per-OS: EGL-surfaceless/Mesa on Linux, ANGLE-EGL or WGL on Windows, CGL on macOS (generalizes the Mesa-specific `EGL_PLATFORM_SURFACELESS_MESA` path). | V2.1 |
| 6 | V2.10 | Internal dirty-flag draw-state cache in `core/draw.cpp` | Keep the free-function `core::Draw` API and audit anchors; cache `setViewport`/`setClearColor`/`enable*`/`disable*` values and skip redundant `gl*` calls. Motivator: OIT mid-frame toggles. No API/audit change. | — |
| 7 | V2.6 | Shader externalization to `.glsl` files | Replace inline `constexpr char[]` GLSL in render/ with `.glsl` files loaded by `core::ShaderProgram` (adds syntax highlighting/editor navigation). Keep the t3 malformed-shader golden substring reproducible via a fixture file. | — (relocation only) |
| 8 | V2.7 | GLSL profile macro (`RE_GLSL_VERSION`) | Decouple the shader language level from the llvmpipe ceiling: 450 = portable floor (tests/CI), 460 = hardware floor. Single `#version` concern now that shaders live in files. | V2.6 |
| 9 | V2.8 | Backend-agnostic CI driver hook (`tools/env_ci.sh`) | Move the `GALLIUM_DRIVER`/`MESA_*` coupling out of `tests/CMakeLists.txt` into a per-platform hook; keep the leak-gate *principle* (deterministic software driver, stable LSan attribution) portable, llvmpipe as one implementation. | — |
| 10 | V2.9 | Per-OS provisioning tables + generic display requirement | Replace the Ubuntu/X11/xvfb-specific §8 package list with per-OS tables (Linux/Windows/macOS) and a generic "a display server must be available" sample-gate requirement. | — |