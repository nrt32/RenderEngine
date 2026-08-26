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
- **Layout:** `io/` `data/` `volume/` `scene/` `core/` `broker/` `utils/`
  `render/` `app/` `tests/` (+ `test_utils/` peer lib after T18, `AUDIT_SOURCE_DIRS` then `io data volume scene core broker render app utils test_utils tests`). `scene/` is the GL/RE-free app-side scene
  description library (View, Camera, SceneObject, Material/Light descs — no
  `App` prefix, the namespace is the prefix) and `broker/` is the heavily
  abstracted per-type `scene → render` mediation library (`IMapper<AppT,ReT>`
  per file, `ViewBridge` façade — app never holds a mapper handle)
  (SPEC §3/§11). Because this differs from the audit default source dirs, the
  loop MUST be launched with `source tools/env.sh` (sets `AUDIT_SOURCE_DIRS="io
  data volume scene core broker render app utils tests"`) or the ownership/
  disposition audit rules in `tools/audit.rules` will not see the new `scene/`
  + `broker/` files. (audit.sh:42 default is `src include lib engine tests app`.)
- **Guardrails:** see `tools/audit.rules` + `docs/spec/guardrails.md` §6 (source of truth). Hard rules: raw GL calls ONLY under `core/` via RAII (`core/rhi/gl/` after RHI lands) + `core::Draw`/`REContext`; `render`/`app`/`tests`/`broker`/`scene` use `core/` wrappers; `utils/` holds the offscreen context + pixel reader delegating to `core::loadCoreGl`/`core::readRgba8` (and `test_utils/` after T18 via `REContext`); deps pinned via `GIT_TAG` (no branches); no legacy GL; **raw readback ONLY under `core/` (and `test_utils/` façade after T18)**, consumed by tests via `utils::PixelReader`/`test_utils::PixelReader`; **no `recreateAll`/`dumpAll` size-dump sync** (`no_dump_sync`); **RE-minimal `asset_indirection`** — `render/re_scene/` never stores verbatim `data::Mesh::positions`; **broker `app ↛ render`** (`acl_app_render` + `broker_app_reach`); **ownership discipline** — no raw `Type* name` where ownership matters in `scene/`/`broker/`/`app/` unless marked `/*borrow*/` + `@note lifetime:` (audit `ownership_raw_ptr_*`); spdlog not printf/cout; datasets carry a `LICENSE` beside each dataset dir (per-dir, not just global `grep`). Evidence rule (R4): every test asserts an explainable constant — never "non-empty/non-black/>0" (review-gated `evidence_rule` placeholder in `audit.rules`). Regression lock (R3): prior tests are never weakened (review-gated `regression_lock` placeholder). SPEC §6 is the source of truth.
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
