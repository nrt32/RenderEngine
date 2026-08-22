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
  `AUDIT_SOURCE_DIRS="io data volume core render app utils tests"` and
  `LOOP_BUILD_TEST_CMD` into the runner's environment. The T1 gate test
  already in the suite (COMPLETED_TASKS.md T1, gate item 4) makes this
  enforceable: a forgotten source must FAIL the gate loudly, never silently
  audit the wrong dirs.
- Every gate also runs the mechanical audit (`tools/audit.sh`) with
  `AUDIT_SOURCE_DIRS="io data volume core render app utils tests"` (via
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

## V2 documentation map (T-map, R9)

| Task | Docs updated in the same commit |
|---|---|
| T1 | docs/render.md (`IRenderer`, `render/types.hpp`) |
| T2 | docs/render.md (`View`/`ViewRect`/`ViewRenderer`, `core::blit`) |
| T3 | docs/render.md (`AssetRegistry`) |
| T4 | tools/env.sh (`AUDIT_SOURCE_DIRS`), AGENTS.md (Layout + build/test notes), TASKS.md preamble (R15/audit dir lists), docs/spec/modules.md + SPEC.md at-a-glance module list (`utils/`) |
| T5 | docs/spec/env.md (per-OS offscreen backend) |
| T6 | docs/core.md (draw-state cache) |
| T7 | docs/render.md (`.glsl` files + malformed fixture) |
| T8 | docs/render.md (`RE_GLSL_VERSION`), docs/spec/env.md (450/460 ceiling note) |
| T9 | docs/spec/env.md (per-platform driver hook) |
| T10 | docs/spec/env.md (per-OS provisioning tables + generic display requirement) |

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

**T** — gate asserts (explainable): a 2-view layout in a **1280×480** window
renders each view's scene into its own FBO and the final window blit places
each view's content in its pinned `ViewRect` — View A = (0,0,640,480),
View B = (640,0,640,480) — and the center pixel of each view (A: (320,240),
B: (960,240)) matches that view's scene's expected color within 1/255
(readback via the core/ wrapper only, N>=3); MPR sample still green.

**G** — suite green (N>=3 for readback), audit green.

## T3: Asset registry (`AssetHandle`)

**D** — (SPEC §9 V2.5) `render::AssetRegistry::register()` → copyable
`AssetHandle{index,generation}`; one GPU object per individual CPU object
globally (fixes `MeshRenderer`+`SliceRenderer` double-upload of the same
`data::Mesh`). Scene instances store `AssetHandle` instead of raw
`const data::*` pointers; handles are the currency views exchange.

**T** — gate asserts (explainable): registering the same `data::Mesh` twice
(once via `MeshRenderer`, once via `SliceRenderer`) yields one GPU object —
`AssetRegistry::slotCount() == 1` and both handles resolve to the same GL
object id; dangling-handle detection on generation mismatch (a stale
{index,generation} lookup returns a typed error, no crash).

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

**T** — gate asserts (explainable): a test-injectable GL-call spy in
`core::Draw` records call counts — `setClearColor(red); setClearColor(red)`
issues exactly **1** `glClearColor` (the second is a cache hit, no GL call),
and the same holds for `setViewport`/`enable*`/`disable*`; output pixels are
unchanged within 1/255 (readback via the core/ wrapper only, N>=3).

**G** — suite green, audit green.

## T7: Shader externalization to `.glsl` files

**D** — (SPEC §9 V2.6) Replace inline `constexpr char[]` GLSL in render/ with
`.glsl` files loaded by `core::ShaderProgram` (adds syntax highlighting/editor
navigation). Keep the completed-loop t3 malformed-shader golden substring
`ERROR: 0:7` (COMPLETED_TASKS.md T3) reproducible via a fixture file.
Relocation only.

**T** — gate asserts: the completed-loop t3 `ERROR: 0:7` golden substring
still produced; all completed-loop shader-backed gates (t7–t11, t14, t15 in
COMPLETED_TASKS.md) unchanged.

**G** — suite green, audit green.

## T8: GLSL profile macro (`RE_GLSL_VERSION`)

**D** — (SPEC §9 V2.7) Decouple the shader language level from the llvmpipe
ceiling: 450 = portable floor (tests/CI), 460 = hardware floor. Single
`#version` concern now that shaders live in files (T7).

**T** — gate asserts (explainable): in the gate env the macro expands to
`#version 450` — a `static_assert` on the macro's version value plus compiling
a fixture shader whose `#version` line is produced by the macro on llvmpipe.
The 460/hardware compile is a **manual sample verification**, not a gate
assertion (llvmpipe caps at GLSL 4.50, SPEC §8).

**G** — suite green, audit green.

## T9: Backend-agnostic CI driver hook (`tools/env_ci.sh`)

**D** — (SPEC §9 V2.8) Move the `GALLIUM_DRIVER`/`MESA_*` coupling out of
`tests/CMakeLists.txt` into a per-platform hook; keep the leak-gate *principle*
(deterministic software driver, stable LSan attribution) portable, llvmpipe as
one implementation. Pinned per-platform mapping: Linux
`GALLIUM_DRIVER=llvmpipe` (+ existing `MESA_*` vars), Windows WARP, macOS
software CGL renderer.

**T** — gate asserts: `tools/env_ci.sh` contains the literal Linux mapping
`GALLIUM_DRIVER=llvmpipe` (grep-asserted, plus the `MESA_*` vars), sourcing it
reproduces today's test env exactly, and the suite stays green under llvmpipe
on this host.

**G** — suite green, audit green.

---

## T10: Per-OS provisioning tables + generic display requirement

**D** — (SPEC §9 V2.9) Replace the Ubuntu/X11/xvfb-specific §8 package list
with per-OS tables (Linux/Windows/macOS) and a generic "a display server must
be available" sample-gate requirement.

**T** — gate asserts: SPEC §8 contains the literal subsections `## Linux`,
`## Windows`, `## macOS`, each with a per-OS package table, and the
sample-smoke gate wording contains the literal phrase "a display server must
be available".

**G** — suite green, audit green.

---

## Definition of Done (end-of-loop evidence, finalized at T10)

- [ ] All 10 task gates green; full suite green on a clean tree at the last task.
- [ ] GPU/readback tests (T2, T6) verified with **N>=3 consecutive green
      runs** (records in `tools/logs/`).
- [ ] Mechanical audit green (`tools/audit.sh`) with
      `AUDIT_SOURCE_DIRS="io data volume core render app utils tests"`.
- [ ] ASan+UBSan clean on all test binaries (no leaks, no UB).
- [ ] Documentation map complete (table above): docs/render.md, docs/core.md,
      AGENTS.md, docs/spec/env.md, tools/env.sh, TASKS.md preamble — exactly as
      listed per task.
- [ ] Sample smoke set (mesh/plane/volume/slice/oit/mpr) still green under the
      display-server-generic gate wording.