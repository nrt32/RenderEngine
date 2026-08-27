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
  `render/` `app/` `tests/` (`test_utils/` peer lib present from start, `AUDIT_SOURCE_DIRS` is `io data volume scene core broker render app utils test_utils tests` — `test_utils` empty until T18, harmless pre-existing). `scene/` is the GL/RE-free app-side scene
  description library (View, Camera, SceneObject, Material/Light descs — no
  `App` prefix, the namespace is the prefix) and `broker/` is the heavily
  abstracted per-type `scene → render` mediation library (`IMapper<AppT,ReT>`
  per file, `ViewBridge` façade — app never holds a mapper handle)
  (SPEC §3/§11). `tools/audit.sh` default is `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` (fixed from legacy `src include lib engine tests app`); `tools/env.sh` exports the same, but the default now passes even if forgotten — `TASKS.md` R15 still enforces `source tools/env.sh` via gate test.
- **Guardrails:** see `tools/audit.rules` + `docs/spec/guardrails.md` §6 (source of truth). Hard rules: raw GL calls ONLY under `core/` via RAII (`core/rhi/gl/` after RHI lands, `gpu_api_ownership` transitional `core|` until T10 (V3.9) — see `audit.rules` + `guardrails.md` §6) + `REContext` (raw `gl*` stays `core`-only; `test_utils/` façade via `REContext::readRgba8`, not raw); `render`/`app`/`tests`/`broker`/`scene` use `core/` wrappers; `utils/` holds the offscreen context delegating to `core::loadCoreGl` + `utils::PixelReader`/`test_utils::PixelReader` via `REContext::readRgba8`/`core::readRgba8` (`utils/offscreen_context.*` stays in `utils/` — T15 owns it, T18 moves only `read_pixels`/`pixel_reader`/capture helpers to `test_utils/`); deps pinned via `GIT_TAG` (no branches — `deps_pinned_no_branch` forbids `master|main|HEAD|develop|feature|release`); no legacy GL; **raw readback ONLY under `core/` (`test_utils/` façade via `REContext`, not raw — present from start)**, consumed by tests via `utils::PixelReader`/`test_utils::PixelReader`; **no `recreateAll`/`dumpAll` size-dump sync** (`no_dump_sync`); **RE-minimal `asset_indirection`** — `render/re_scene/` never stores verbatim `data::Mesh::positions`; **disposition / layer isolation** — `scene/` never includes `render/` (`disposition_scene` `forbid_inside scene|#include.*render/`) + `render/` never includes `scene/` (`disposition_render` `forbid_inside render|#include.*scene/`) — `broker/` is the only lib that may include both; **broker `app ↛ render`** (`acl_app_render` + `broker_app_reach` + `test_window_forbid` — `test_window_forbid` active from T15, review-only until then); **broker mediation** — one `IMapper`/`ICachedMapper` per file (`broker_per_type`) + ISP segregation `IMapper` must not expose `mapCached` (`isp_mapper_forbid`); **render hygiene** — no `<glad/gl.h>` under `render/` (`render_no_glad`), no placeholder `Noop` in `broker/` (`no_noop_broker`); **build hygiene** — no per-target `-fsanitize` flags (`no_per_target_sanitize` — `INTERFACE re_project_sanitizers` only); **ownership discipline** — no raw `Type* name` where ownership matters in `scene/`/`broker/`/`app/` unless marked `/*borrow*/` + `@note lifetime:` (audit `ownership_raw_ptr_*`); `comment_tag_context` waivers in `tools/comment_context.allow` for `render/shaders/` + `test_utils/`; spdlog not printf/cout; datasets carry a `LICENSE` beside each dataset dir (per-dir, not just global `grep`); **evidence rule (R4) mechanical floor `weak_assert_phrase` forbids `non-empty/non-black` + review-gated `evidence_rule` placeholder**. Regression lock (R3): prior tests are never weakened (review-gated `regression_lock` placeholder). SPEC §6 is the source of truth.
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
