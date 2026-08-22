# AGENTS.md

<!-- loop-framework -->
## Loop framework (installed via loop-framework)
- Load the `loop-protocol` skill for the binding loop rules: roles, gates,
  evidence rule, regression lock, verification protocol, permission policy,
  state-file ownership, URL discipline, failure taxonomy.
- `tools/run_task.sh` is the RUNNER: it owns supervision, gates, commits,
  pushes, and every file under `tools/logs/`.
- Orchestrator (user-facing session): kickoff + escalation ONLY — never
  supervise/poll/patch code/commit yourself. That is the runner's job.
- Headless: never block on a permission prompt; follow the allow/deny policy.
<!-- /loop-framework -->

## Project: RenderEngine

- **Stack:** C++20, CMake (>= 3.24), OpenGL 4.6 core (glad2), GLFW 3.4, GLM,
  Dear ImGui, GoogleTest, spdlog, stb_image — all pinned via FetchContent
  `GIT_TAG` (SPEC §2). Build+test gate uses CMake; the loop MUST be launched
  with `source tools/env.sh` first (exports `LOOP_BUILD_TEST_CMD` +
  `AUDIT_SOURCE_DIRS`, SPEC §8).
- **Layout:** `io/` `data/` `volume/` `core/` `utils/` `render/` `app/`
  `tests/`. Because this differs from the audit default source dirs, the loop
  MUST be launched with `source tools/env.sh` (sets `AUDIT_SOURCE_DIRS="io
  data volume core render app utils tests"`) or the ownership/forbidden audit
  rules in `tools/audit.rules` will not see the source files. (audit.sh:42
  default is `src include lib engine tests app`.)
- **Guardrails:** see `tools/audit.rules` (raw GL calls ONLY under core/ via
  RAII objects + core::Draw API; render/app/tests use core/ wrappers; `utils/`
  holds the offscreen context + pixel reader and delegates to the core/ raw-GL
  anchors `core::loadCoreGl` / `core::readRgba8`; deps pinned; no legacy GL;
  **raw readback calls ONLY under core/, consumed by tests via
  utils::PixelReader** — never in render/, app/, tests/, or utils/; spdlog not
  printf/cout; datasets carry a LICENSE beside each dataset dir). Evidence rule
  (R4): every test asserts an explainable constant — never
  "non-empty/non-black/>0". Regression lock (R3): prior tests are never
  weakened. SPEC §6 is the source of truth.
- **GL/display:** samples need WSLg display; unit tests run headless with an
  offscreen GL context and are built with ASan+UBSan.
- **Build & test:** always `source tools/env.sh` first (exports
  `LOOP_BUILD_TEST_CMD` and `AUDIT_SOURCE_DIRS`, SPEC §8) — the R15 gate test
  fails loudly if those env vars are missing. Then run
  `eval "$LOOP_BUILD_TEST_CMD"` from the repo root (or the equivalent
  `tools/configure.sh && cmake --build build -j$(nproc) && ctest
  --test-dir build --output-on-failure`). Builds are incremental/cached
  (SPEC §5/§8): `tools/configure.sh` skips cmake when nothing changed and
  wires ccache in as the compiler launcher when installed. This matches
  `tools/env.sh`, the single source of truth for the loop's build/test
  command.
- **Docs:** public APIs carry Doxygen comments (SPEC §5).
