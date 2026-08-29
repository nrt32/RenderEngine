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
  (SPEC §3/§11). `tools/audit.sh` default is `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` (fixed from legacy `src include lib engine tests app`); `tools/env.sh` exports the same, but the default now passes even if forgotten — `TASKS.md` R15 `T1` gate (spec-review #3) still enforces `source tools/env.sh` loudly via `test "$AUDIT_SOURCE_DIRS" = "io ... tests" && test -n "$LOOP_BUILD_TEST_CMD"` — a forgotten source fails the gate, never silently audits wrong dirs.
- **Guardrails:** see `tools/audit.rules` + `docs/spec/guardrails.md` §6 (source of truth, iteration 2 #3 mirror — `AGENTS.md` now 1:1 mirrors `guardrails.md:8-91` per checklist `each captured in audit.rules AND AGENTS.md`). Hard rules:
  - raw GL ONLY under `core/` via RAII (`core/rhi/gl/` after RHI lands,
    `gpu_api_ownership` transitional `core|` (`\bgl[A-Z].*\( `, `GL_*` at `T15b`, `rhi_ownership` `core/rhi/gl|` at T17) until **(stretch) T17 RHI**
    — see `audit.rules` + `guardrails.md` §6; `rhi_ownership` intentionally commented until T17, audit passes with `core|` anchor — waiver documented in `TASKS.md` DoD stretch vs required split, spec-review #14 + iteration 2 #10) + `REContext`
  - `render`/`app`/`tests`/`broker`/`scene` use `core/` wrappers (`render_no_glad` `forbid_inside render|#include.*glad` + `gpu_api_ownership` `forbid_outside core|\bgl` + `no_production_readback` `forbid_outside core|\bglReadPixels`)
  - `utils/` holds `OffscreenContext` delegating to `core::loadCoreGl`
    (`utils/offscreen_context.*` stays in `utils/`, `test_utils/PixelReader` via `REContext::readRgba8` — `no_production_readback` allows `core|test_utils` façade)
  - `T4` facades `core/offscreen.hpp` + `render/offscreen.hpp`
    `renderOffscreen()` via `utils::OffscreenContext` + `REContext::readRgba8` + `render_no_window` (`forbid_inside render|#include.*window`)
  - deps pinned via `GIT_TAG` (no branches, full 40-char SHA via `git ls-remote` for `glad2 73db193f853e2ee079bf3ca8a64aa2eaf6459043` etc.) + `deps_pinned_no_branch` (denylist `stable|next|latest|trunk|dev|vNext|1.x|refs/heads|origin`) + `deps_pinned_no_find_package` (no `find_package` fallback for `glfw3`/`glm`/`spdlog`/`nlohmann_json`, see `tools/audit.rules:44` `TASKS.md:T16` — spec-review #3 fix, iteration 2 #4 SHA diff)
  - no legacy GL (`no_legacy_api` `glBegin|glMatrixMode`) ; raw readback ONLY under `core/` (`test_utils/` via `REContext` — `no_production_readback`)
  - `no_dump_sync` (no `recreateAll`/`dumpAll` — `CompositeKey{Version,LayoutId,Id,Gen,Hash}` not `id`/`size`)
  - `asset_indirection` (RE-minimal — `forbid_grep data::Mesh::positions|data::VolumeDataset::voxels` in `render/re_scene`)
  - `disposition_scene`/`disposition_render` (`scene`↛`render`, `render`↛`scene` — `forbid_inside`)
  - `acl_app_render` + `broker_app_reach` + `test_window_forbid` (from T15, `forbid_inside app|#include.*render/` + `broker.*IMapper` + `tests|#include.*window`)
  - `broker_per_type` (one `IMapper`/`ICachedMapper` per file — `forbid_grep class.*Mapper.*\n.*class.*Mapper`, `grep -c ==1` per-file at `T15b`) + `no_noop_broker` (`forbid_inside broker|Noop`)
  - `isp_mapper_forbid` (IMapper must not expose `mapCached` — `forbid_grep class[[:space:]]+[A-Za-z0-9_]+Mapper.*IMapper.*mapCached`)
  - `no_per_target_sanitize` (`INTERFACE re_project_sanitizers` only — `forbid_grep add_compile_options.*-fsanitize|target_compile_options.*-fsanitize`)
  - `no_sample_bloat` (`app/mesh_sample.cpp ≤80/==42` + `app/mpr_slice.hpp ≤100/==98` via `audit.sh` `wc -l`, `forbid_grep __never_matches` placeholder) + `no_object_duplicate` (`scene/objects/*.hpp` `==6` `class.*Object.*ObjectBase`, `diff` >10% duplicate)
  - `layer_count`/`layer_mask` (`COUNT=8`, no `LayerMask` — `tools/audit.rules:91-92` `T5` — `COUNT=8` exact, `LayerMask` must stay `0`, commented until `T5` per spec-review #14, iteration 5 #5)
  - `engine_depth_default` (`require_grep DepthConfig\{true` in `include/render_engine/engine.hpp` — `DepthConfig{true}` single-site, `T4`/`T15a`)
  - `render_no_glad` / `render_no_window` (see above)
  - `ownership_raw_ptr_*` (Type* / Type * / void* / auto* via ERE, `Type* /*borrow*/` + `@note lifetime:` per `audit.rules:152` T15a #11 — narrow `Type*` until `T15a`, broad `void*|auto*|Type *` at `T15a`, `GLFWwindow* /*borrow*/` already at iteration 1 #4, iteration 6 #4 sync — 1:1 mirror of `audit.rules:152-153`)
  - `evidence_analytic` (`1/255|1e-6` anchor) + review-gated `evidence_rule`/`regression_lock` (`__never_matches` intentional — mechanical floor is per-task `grep -c "1/255\|1e-6\|BudgetExceeded\|152 MB"` + `evidence_analytic` global, see `audit.rules:158-165`, stretch `T18`)
  - `comment_tag_context` (>=120 chars prose, `comment_context` mode, waivers via `tools/comment_context.allow`)
  - `no_secrets` (`api[_-]?key|password|secret` `forbid_grep` covers `AUDIT_SOURCE_DIRS`; built-in whole-tree scan `audit.sh:148` covers `examples/` etc. with `12+` quote-anchored `=["']` pattern, `build/.git/*.log` excluded, iteration 14 #3 fix) / `no_legacy_api` / `no_raw_diagnostics` (`std::cout|printf`) / `assets licensed` (`require_grep LICENSE` + `audit.sh` per-dir `data/meshes/LICENSE` + `data/volumes/LICENSE` per `assets.md` §7, `T2` gate)
  - `error domain` (`ErrorDomain` tag on `data::Error`, review-gated per `docs/spec/guardrails.md:53` `T22` — `tools/audit.rules` `# error_domain review-gated (T22)` placeholder, see `AGENTS.md` mirror iteration 3 #5)
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
