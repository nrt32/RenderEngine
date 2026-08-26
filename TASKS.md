# TASKS — RenderEngine

Active backlog is the **pure-redesign V3 iteration** (no new FRs — OpenGL/C++/CMake and Mesh/Volume/Plane/Slice/OIT/MPR requirements are locked per user direction 2026-08-23; implementation only). The completed sequential loops are archived in `COMPLETED_TASKS.md`: `T1–T16` (V1, 0.1.0) + `V2-T1–V2-T8` (V2 multi-view/asset/platform/maintainability, 8 gates green — archived 2026-08-23, logs purged). See SPEC.md for FRs, §9 for the V2 archive / §9.1 for the V3 roadmap, §6 for guardrails, NAMING_CONVENTIONS.md for style.

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
  `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils tests"` and
  `LOOP_BUILD_TEST_CMD` into the runner's environment. The T1 gate test
  already in the suite (COMPLETED_TASKS.md T1, gate item 4, updated for V3) makes this
  enforceable: a forgotten source must FAIL the gate loudly, never silently
  audit the wrong dirs.
- Every gate also runs the mechanical audit (`tools/audit.sh`) with
  `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils tests"` (via
  `source tools/env.sh`).
- Unit tests build with ASan+UBSan; GL-touching tests use the offscreen GL
  fixture (headless). No exceptions in v1; typed `Result` errors (SPEC §5).
- Logging via spdlog only (no printf/cout). Doxygen on all public API.

> V2 clean: `V2-T1–V2-T8` (IRenderer, multi-view, AssetRegistry, utils/, per-OS backend, draw-cache, .glsl externalization, RE_GLSL_VERSION) were green and archived to `COMPLETED_TASKS.md` 2026-08-23; `tools/logs/` (`run_all.out`, `session_*.lease`, `task_*.{log,gate.log,pass,review}`) purged. Workspace is clean for the pure-redesign iteration.

---

## V3 backlog — pure-redesign `scene`/`broker`/View/List/Persistence overhaul (SPEC §10–§12)

Pure-redesign iteration (no new FRs). Priority order **redesign-first** per `open_questions.md:30 Q30` binding: `T1 scene` → `T2 CompositeKey/TranslateContext/DrawContext` skeletons → `T3 broker` (needs T1+T2) → `T4 Camera` → `T5 View` (needs T1+T2+T3) → `T6 persistence full` (needs T5+T2) → `T7 AssetId` → `T8 Phong` → `T9 RE-minimal` (needs T1+T5+T7+T8) → `T10 stretch` (needs —). Each task is **one session, one reason to change** (SRP) and maps to `SPEC §9.1` V3.x. Accepted standard `T1..Tn` per iteration — V2 `V2-T1..V2-T8` archived, V3 now `T1..T10` (not `T9..T18`). **All 13 ★ `SPEC §13` open questions resolved binding 2026-08-23 Sr. Principal review (Q3/Q9/Q27/Q28/Q32f/Q39-Q47) — `open_questions.md:11` header; no ★ blocks V3 kickoff.**

## V3 documentation map (T-map, R9) — to be filled per task

| Task | Spec alias | Docs updated in the same commit |
|---|---|---|
| T1 | V3.1 | `docs/spec/modules.md` (`scene/`), `scene/` CMake target, `SPEC.md` at-a-glance modules |
| T2 | V3.2a | `docs/spec/persistence.md` (§10.1 composite key), `docs/spec/broker.md` (§11.4 `TranslateContext`), `docs/core.md` (`DrawContext`), `core/draw.hpp` |
| T3 | V3.2b | `docs/spec/broker.md` (§11 `IMapper`/`ICachedMapper`/`Broker`/`IViewBridge`), `broker/` README, `NAMING_CONVENTIONS.md` (Mapper/Broker/ViewBridge) |
| T4 | V3.3 | `docs/spec/modules.md` (§3.1 `Camera`), `scene/camera.hpp` (Doxygen) + `docs/render.md` (`Camera` → view matrix) |
| T5 | V3.4 | `docs/render.md` (`ReView`/`IRenderable`/`ViewTarget`), `docs/spec/modules.md` (§3.2) — deletes `ViewRenderer` |
| T6 | V3.5 | `docs/spec/persistence.md` (§10 full), `docs/spec/modules.md` (`Layout::resolve`) |
| T7 | V3.6 | `docs/spec/assets.md` (addendum) + `data/README.md` (always) |
| T8 | V3.7 | `docs/spec/materials_lights.md` (§12.2/§12.3 deferred note — Phong-only stays), `docs/render.md` (RE-minimal `Re*` note) |
| T9 | V3.8 | `docs/re_scene_inventory.md` (binding inventory — 6 tables/23 fields) + `render/re_scene/mesh_object.hpp` reference, `tools/audit.rules` (`asset_indirection` active) |
| T10 | V3.9 | `docs/spec/nfr.md` (stretch tags) + `docs/spec/modules.md` (EOL skeletons deferred — stretch, no code) |
| T11 | V3.8b | `render/contour_renderer.*` + `render/shaders/contour.geom.glsl` (`ContourRenderer` GPU), `broker/contour_mapper.*`, `docs/render.md` (contour GPU), `app/mpr_contour.hpp` deleted |
| T12 | V3.4b | `render/plane_renderer.hpp` (audit plane via `PlaneRenderer`), `docs/render.md` (plane GPU), `app/` CPU quad parsing removed |
| T13 | — | `docs/spec/guardrails.md` (ownership ladder), `NAMING_CONVENTIONS.md` (§8b borrow notation), `tools/audit.rules` (`ownership_raw_ptr_*`), `docs/render.md`/`docs/spec/materials_lights.md`/`docs/spec/assets.md`/`docs/re_scene_inventory.md` (ownership-model sync), `render/re_scene/mesh_object.hpp` (shared material) |
| T14 | — | `docs/spec/assets.md` (§7 unified multi-kind store record), `docs/render.md` (asset-store section + Plane/Volume renderer texture paths + guardrails), `broker/README.md` (asset_store ↔ render store split) |
| T15 | — | `tools/audit.sh` (`comment_context` mode), `tools/audit.rules` (`comment_tag_context` bare-tag rule), `tools/audit.rules.example`, comment-hygiene sweep across all modules (self-contained rationale beside every tag) |
| T16 | — | `render/volume_slice_renderer.*` + `render/shaders/volume_slice.frag.glsl` (GPU volume-plane extraction), `docs/render.md` (VolumeSliceRenderer section + shader table + PlaneRenderer scope note), `docs/mpr.md` (GPU-extracted interactive 2D views, display-frame scaffolding), `docs/samples.md` (plane sample = extracted CT planes) |

> **Naming:** active backlog is `T1..T10` for this iteration (resets after archive — `V2-T1..V2-T8` remain in `COMPLETED_TASKS.md`). `V3.x` survives only as Spec alias in `docs/spec/roadmap.md` §9.1 and parentheses below.

---

---

## Active backlog — EMPTY (V3 complete, 23/23 green)

All 23 V3 tasks (T1–T23) have been completed and archived to `COMPLETED_TASKS.md` (V3 section below).
The next engine iteration (V4) will be planned via `/loop-init` / `/loop-spec-review`.

> **Naming:** archived backlog was `T1..T23` for this iteration. `V3.x` survives only as Spec alias in `docs/spec/roadmap.md` §9.1.

---

## Definition of Done — review follow-ups (T13–T23, user-mandated + architecture review)

- [ ] `T13`: no undocumented raw owning-suspect pointers in `scene/ broker/ app/` public APIs; every remaining borrow carries a lifetime note; audit ownership rule green; construction-order hazards removed from all samples.
- [ ] `T14`: unified typed asset store covers mesh + volume + image (+ material slot with real dedup replacing the `material=nullptr` placeholder); per-renderer pointer-keyed texture caches deleted; same-dataset-two-renderers gate proves one GPU texture.
- [ ] `T15`: comment sweep landed — every SPEC/task tag is accompanied by self-contained rationale (incl. the false "deduped RE material handle" claim until T14 makes it true); bare-tag pattern rule green or explicitly allowlisted.
- [ ] `T16`: GPU volume-plane extraction shipped; `plane_sample` demonstrates an extracted volume plane (not a gradient quad); MPR 2D views interactive on the GPU path with CPU oracle retained for tests.
- [ ] `T17` (renderer consolidation): prologue/quad/hash/geometryFor deduplicated; zero pixel drift on all renderer gates; `<glad/gl.h>` gone from render/.
- [ ] `T18` (depth): optional depth attachment + per-view depth flag; near-mesh-wins overlap gate; color-only default untouched.
- [ ] `T19` (needs `T18`): OIT sample = ≥2 opaque + ≥2 transparent real meshes with view-direction overlap, depth-buffer-backed target, analytic composite probes green N>=3.
- [ ] `T20`: all samples route through `IViewBridge`; volume/plane layers are real (no Noop); `PlaneMapper` exists with voxel→world conversion test.
- [ ] `T21`: dirty tracking computed from `dirtyLog_`; tombstoned ids resolve to typed errors; single `StableKey`; no fake parallel code; multi-camera mapper cache.
- [ ] `T22`: domain-tagged error codes; debug-trap on failed `Result` dereference; dead accessors removed.
- [ ] `T23`: resize callback + live-aspect samples; simulated-resize projection gate.