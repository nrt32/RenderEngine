# TASKS — RenderEngine

Active V2 backlog. The completed sequential loop (T1–T16) is archived in
`COMPLETED_TASKS.md`. See SPEC.md for FRs, §9 for the V2 roadmap, §6 for
guardrails, NAMING_CONVENTIONS.md for style.

## Generic rules (preamble, binding for every task)

- R2 Gate discipline: runner rebuilds + runs the FULL suite before each task on
  a clean tree. No new work while red.
- R3 Regression lock: tests from prior tasks are NEVER weakened.
- R4 Evidence rule: every test asserts an explainable constant (analytic value,
  committed golden corpus hit, invariant derived from project numbers). Never
  "non-empty / non-black / >0".
- R5 Stop-and-report: an infeasible requirement PAUSES and is reported; never
  silently reinterpreted or relaxed.
- R6 One branch (main), meaningful incremental commits, no secrets, no
  binaries outside allowed dirs.
- R7 Build hygiene: warnings-as-errors; build stays clean at every task end.
- R8 Review-before-commit: build clean → full suite green → independent review
  → review gate (findings addressed, green again) → RUNNER commit → push.
- R9 Documentation gate: docs are part of every deliverable; update exactly the
  files this task's documentation-map row lists.
- R10 Verification: after the final full-suite run, no further edits; for
  GPU/readback tests require N>=3 consecutive green runs before declaring green.
- R11 State files under `tools/logs/` are runner-owned; never touch.
- R12 Permission policy: allowlist only; never block on a prompt.
- R13 URL discipline: never cite a web page not fetched; pin URLs that matter.
- R14 Failure taxonomy: infra vs code vs state vs gate; escalate, don't retry
  forever.
- R15 Launch prerequisite (SPEC §8): the loop MUST be launched with
  `source tools/env.sh` first — it exports
  `AUDIT_SOURCE_DIRS="io data volume core render app tests"` and
  `LOOP_BUILD_TEST_CMD` into the runner's environment. T1's gate makes this
  enforceable: a forgotten source must FAIL the gate loudly, never silently
  audit the wrong dirs.
- Every gate also runs the mechanical audit (`tools/audit.sh`) with
  `AUDIT_SOURCE_DIRS="io data volume core render app tests"` (via
  `source tools/env.sh`).
- Unit tests build with ASan+UBSan; GL-touching tests use the offscreen GL
  fixture (headless). No exceptions in v1; typed `Result` errors (SPEC §5).
- Logging via spdlog only (no printf/cout). Doxygen on all public API.

---

## V2 future scope (roadmap)

V2 backlog recorded during post-loop review (SPEC §9). The scripts task (T16)
is complete; this is the backlog for the next engine iteration.

Priority order (approved): **product-first** — the multi-view workstream
(1 → 2 → 3) first, then low-risk refactors (4/5/6), then maintainability
(7/8), then portability (9/10). Each item below maps to its SPEC §9 row.

---

## T1: `IRenderer` interface + shared `render/types.hpp`

**D** — (SPEC §9 V2.3) Move `Camera`/`RenderTarget` out of `mesh_renderer.hpp`
into a shared `render/types.hpp`; define a pure abstract `IRenderer::render`
contract implemented by `Mesh/Plane/Volume/SliceRenderer`. Lands as the dispatch
mechanism of the multi-view workstream (task T2). No behavior change.

**T** — full suite green (all prior gates), audit green; renderers unchanged in
output (regression lock R3).

**G** — suite green, audit green.

## T2: Multi-view rendering (Model B: per-view FBO + engine blit)

**D** — (SPEC §9 V2.4) Per-view `core::Framebuffer` + a new `core::blit`
(`glBlitFramebuffer` under core/); `View`/`ViewRect`/`ViewRenderer`; app shares
per-view window-section handles + abstract scene objects; RE dispatches objects
to the correct renderer via `IRenderer` (T1), renders into each view's own FBO,
then blits each FBO into its window rect. No app-side viewport blending.
Drives SceneView/MPRView composition.

**T** — gate asserts (explainable): a 2-view layout renders each view's scene
into its own FBO and the final window blit places each view's content in the
pinned `ViewRect` (pixel check per view); MPR sample still green.

**G** — suite green (N>=3 for readback), audit green.

## T3: Asset registry (`AssetHandle`)

**D** — (SPEC §9 V2.5) `render::AssetRegistry::register()` → copyable
`AssetHandle{index,generation}`; one GPU object per individual CPU object
globally (fixes `MeshRenderer`+`SliceRenderer` double-upload of the same
`data::Mesh`). Scene instances store `AssetHandle` instead of raw
`const data::*` pointers; handles are the currency views exchange.

**T** — gate asserts (explainable): registering the same `data::Mesh` twice
(once via `MeshRenderer`, once via `SliceRenderer`) yields one GPU object —
verified via the registry's slot count / a shared GL object id; dangling-handle
detection on generation mismatch.

**G** — suite green, audit green.

## T4: `utils/` module (offscreen context + pixel reader)

**D** — (SPEC §9 V2.1) Move `offscreen_context` + `read_pixels` to `utils/`
(`re::utils::OffscreenContext`, `re::utils::PixelReader`); `core/` keeps the
raw-GL anchors `loadCoreGl` / `readRgba8`. **Add `utils` to
`AUDIT_SOURCE_DIRS`** so the audit still scans them (guardrail
`gpu_api_ownership` / `no_production_readback` stay intact).

**T** — gate asserts: `tools/env.sh` sets `AUDIT_SOURCE_DIRS` including
`utils`; audit green with raw GL only under core/; tests still pass via
`re::utils::*`.

**G** — suite green, audit green.

## T5: Platform-extensible context-backend factory

**D** — (SPEC §9 V2.2) `utils::OffscreenContext` picks the no-display backend
per-OS (EGL-surfaceless/Mesa on Linux, ANGLE-EGL or WGL on Windows, CGL on
macOS), replacing the Mesa-only `EGL_PLATFORM_SURFACELESS_MESA` hardcode.

**T** — gate asserts: backend selection is deterministic per platform macro;
Linux path unchanged (llvmpipe context still GL 4.6 core).

**G** — suite green, audit green.

## T6: Internal dirty-flag draw-state cache

**D** — (SPEC §9 V2.10) Internal dirty-flag cache in `core/draw.cpp`: cache
`setViewport`/`setClearColor`/`enable*`/`disable*` values and skip redundant
`gl*` calls. Free-function `core::Draw` API + audit anchors unchanged.
Motivator: OIT mid-frame toggles.

**T** — gate asserts (explainable): repeated identical state sets issue no
extra GL state changes (driver-agnostic counter via a test-injectable hook or
no-behavior-change proxy); output unchanged.

**G** — suite green, audit green.

## T7: Shader externalization to `.glsl` files

**D** — (SPEC §9 V2.6) Replace inline `constexpr char[]` GLSL in render/ with
`.glsl` files loaded by `core::ShaderProgram` (adds syntax highlighting/editor
navigation). Keep the t3 malformed-shader golden substring `ERROR: 0:7`
reproducible via a fixture file. Relocation only.

**T** — gate asserts: t3 `ERROR: 0:7` golden substring still produced; all
shader-backed gates (T7–T11, T14, T15) unchanged.

**G** — suite green, audit green.

## T8: GLSL profile macro (`RE_GLSL_VERSION`)

**D** — (SPEC §9 V2.7) Decouple the shader language level from the llvmpipe
ceiling: 450 = portable floor (tests/CI), 460 = hardware floor. Single
`#version` concern now that shaders live in files (T7).

**T** — gate asserts: the macro selects 450 in the llvmpipe test env; a
hardware build compiles 460.

**G** — suite green, audit green.

## T9: Backend-agnostic CI driver hook (`tools/env_ci.sh`)

**D** — (SPEC §9 V2.8) Move the `GALLIUM_DRIVER`/`MESA_*` coupling out of
`tests/CMakeLists.txt` into a per-platform hook; keep the leak-gate *principle*
(deterministic software driver, stable LSan attribution) portable, llvmpipe as
one implementation.

**T** — gate asserts: the hook sets the deterministic software driver per
platform; tests still green under llvmpipe on this host.

**G** — suite green, audit green.

## T10: Per-OS provisioning tables + generic display requirement

**D** — (SPEC §9 V2.9) Replace the Ubuntu/X11/xvfb-specific §8 package list
with per-OS tables (Linux/Windows/macOS) and a generic "a display server must
be available" sample-gate requirement.

**T** — gate asserts: SPEC §8 documents per-OS package tables; the sample-smoke
gate wording is display-server-generic.

**G** — suite green, audit green.