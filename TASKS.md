# TASKS — RenderEngine

Historical backlogs are **EMPTY/archived** (V6 25/25 green, archived 2026-08-30 — `T1..T20` incl. splits `T3a/b,T8a/b,T11a/b,T14a/b,T15a/b` + `T17/T18` stretch + `T19/T20` hotfixes, 25 tasks). The completed sequential loops are archived in `COMPLETED_TASKS.md`: `T1–T16` (V1, 0.1.0) + `V2-T1–V2-T8` (V2) + `V3 T1..T23` (pure-redesign) + `V4 T1..T19` (review batch) + `V5 T1..T17+T8b/T11b` (19 sessions, extensibility & visualization-reuse) + `V6 T1..T20` (consolidated + hotfixes, 25 tasks) (all green, archived 2026-08-30). **The active backlog is EMPTY** — next iteration will be planned via `/loop-init`. See SPEC.md for FRs, §9 for the V2 archive / §9.1 for the V3 roadmap (`T1..T16` inc. splits `T3a/b,T8a/b,T11a/b,T14a/b,T15a/b` =21 active + `T17/T18` stretch =23 total + `T19/T20` hotfixes =25 total, spec-review gate 2026-08-29 fixed count), §6 for guardrails, NAMING_CONVENTIONS.md for style.

## Generic rules (preamble, binding for every task)

- R2 Gate discipline: runner rebuilds + runs the FULL suite before each task on
  a clean tree. No new work while red.
- R3 Regression lock: tests from prior tasks are NEVER weakened.
- R4 Evidence rule: every test asserts an explainable constant (analytic value,
  committed golden corpus hit, invariant derived from project numbers). Never
  "non-empty / non-black / >0". Mechanical floor is per-task `grep -c "1/255\|1e-6\|BudgetExceeded\|152 MB"` counts (DoD) + global `evidence_analytic` `1/255|1e-6` anchor (the `grep -c` is the **mechanical floor only**, not a substitute — the primary is the runtime analytic `EXPECT_NEAR(pixel, expected, 1.0/255.0)` / `1e-6` / `BudgetExceeded` code 8 / `152 MB` exact `157286400` assert on the same line; a stray `// 1/255` comment alone does not satisfy the primary; spec-review #4). Generic `require_grep 1/255` would false-positive on docs negative examples — `evidence_rule`/`regression_lock` placeholders `__never_matches_*` intentionally review-gated (see `tools/audit.rules:158-162` + `docs/spec/guardrails.md` §6). **Waiver:** `R15` `test -n "$LOOP_BUILD_TEST_CMD"` is the **sole sanctioned** `non-empty` binary presence gate (spec-review #7) — companion exact assert `test "$AUDIT_SOURCE_DIRS" = "io data volume scene core broker render app utils test_utils tests"` is analytic; see `COMPLETED_TASKS.md:T1` gate item 4.
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
  `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` and
  `LOOP_BUILD_TEST_CMD` into the runner's environment. The T1 gate test
  already in the suite (COMPLETED_TASKS.md T1, gate item 4, updated for V3) makes this
  enforceable: a forgotten source must FAIL the gate loudly, never silently
  audit the wrong dirs.
- Every gate also runs the mechanical audit (`tools/audit.sh`) with
  `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` (via
  `source tools/env.sh`).
- Unit tests build with ASan+UBSan; GL-touching tests use the offscreen GL
  fixture (headless). No exceptions in v1; typed `Result` errors (SPEC §5).
- Logging via spdlog only (no printf/cout). Doxygen on all public API.

> V2 clean: `V2-T1–V2-T8` (IRenderer, multi-view, AssetRegistry, utils/, per-OS backend, draw-cache, .glsl externalization, RE_GLSL_VERSION) were green and archived to `COMPLETED_TASKS.md` 2026-08-23; `tools/logs/` (`run_all.out`, `session_*.lease`, `task_*.{log,gate.log,pass,review}`) purged. Workspace is clean for the pure-redesign iteration.

## V3 backlog — EMPTY (V4 19/19 green, archived 2026-08-28)

All 19 review follow-up tasks (T1–T19, dependency-ordered) have been completed and archived to `COMPLETED_TASKS.md` V4. Next iteration will be planned via `/loop-init`.

> **Naming:** archived backlog was `T1..T19` (foundations → REContext → pair-key → ... → View lights). Next `T1..Tn` will be assigned per new iteration.

## V5 backlog — EMPTY (V5 19/19 green, archived 2026-08-28)

All 19 sessions (T1..T17 + T8b DepthConfig + T11b OIT fallback) have been completed and archived to `COMPLETED_TASKS.md` V5. Next iteration will be planned via `/loop-init`.

> **Naming:** archived backlog was `T1..T17` + `T8b`/`T11b` (19 sessions, `T8→T8+T8b` `T11→T11+T11b` per Q1/Q2). Next `T1..Tn` will be assigned per new iteration.

## Definition of Done — V5 (archived)
- [x] All 19 task gates green; full suite green on a clean tree at T17 (19 sessions)
- [x] `suite green N>=3` where GL-touching, `audit green`, `ASan+UBSan clean`, `LICENSE` per-dir, `R9` doc-map, `R3` regression, `R4` evidence, `comment_tag_context` PASS, `install` reproduces — see `COMPLETED_TASKS.md` V5 for evidence

## FR → T traceability matrix (active backlog, BLOCKER #1)

Every `FR-N.M` in `docs/spec/frs.md` (21 FRs: io4+data3+vol3+core2+render6+app3, lines 15-86) maps to at least one active `T`-gate asserting its explainable constant (1/255, 1e-6, ε=1e-4, 128×128×70, 1280×960/640×480, 90%/2px). Historic `COMPLETED_TASKS.md` coverage is regression-locked (R3) but listed as `hist` — active `T` re-verifies `1/255`/`1e-6`.

| FR | Active T owner | Analytic constant | Type |
|---|---|---|---|
| FR-io.1 | **T11b** `T11b:D` hostile OBJ + `1e-6` AABB + `T1` `utils::loadMeshAsset` 1/255 parity | vertex/index hand-count, AABB 1e-6 | active |
| FR-io.2 | **T11a** `128×128×70 ≤128³` exact + arbitrary dims tiled via `core::Caps maxTexture3DSize` within 1/255; hostile `sizes 4294967296 1 1` → `BudgetExceeded` only on Caps probe fail (not on >128³ alone, per NFR §5 + T11a Caps tiled; supersedes archived COMPLETED_TASKS.md T5 ≤128³ cap) | NRRD dims exact, voxel corner exact, BudgetExceeded code 8 (Caps probe) | active |
| FR-io.3 | **T11b** `loadImageAsset` golden `2×2` RGBA fixture `data/fixtures/golden_rgba.png` (SHA256 `9ccfc2abaa3984dc34c93aee16be0afa8a5e1395f25492b3df67897e6d00df10` pinned at `T11b` — hand-authored `2×2` RGBA `red/green/blue/white` `sha256sum` verified, iteration 3 #2 fix from empty `e3b0...`) — `T11b:T` asserts `EXPECT_EQ(dims,2×2)` + `EXPECT_NEAR(corner, expected, 1.0/255.0)` 1/255 via `PlaneRenderer` `T3b` secondary) + `2GB golden_image.png → BudgetExceeded` `width*height` overflow via `file_size` pre-probe | dimensions + corner/center 1/255 via `loadImageAsset` + `PlaneRenderer` `T3b` | active + `T3b` |
| FR-io.4 | **T11a+T11b** malformed NRRD (`T11a`) + OBJ/Image (`T11b`) → typed `BudgetExceeded`/`Stale`, no partial | error enum, no exception | active |
| FR-data.1 | **T11b** `faceNormal == cross` 1e-6 | cross-product | active |
| FR-data.2 | **T11b** `AABB exact` via `T1` `Mesh::bounds` | exact bounds | active |
| FR-data.3 | **T11a** `sampleTrilinear(NaN)` clamp + 8-corner 1e-6 | trilinear 1e-6 | active |
| FR-vol.1 | **T11a** `TransferFunction duplicate → typed error` + 1e-6 ramp | control-point exact, ramp 1e-6 | active |
| FR-vol.2 | **T11a** `alpha-blend` + `T3b` volume ray-cast 1/255 | compositing 1e-6 | active + `T3b` |
| FR-vol.3 | **T11a** `ray/AABB step` analytic via `volume/` `ray/AABB` oracle `1e-6` + `T3b` `VolumeSlice` `1/255` (active, spec-review #2 fix — `T11a:T` now asserts `1e-6` analytic step positions, not just `hist`) | step positions 1e-6 | active |
| FR-core.1 | **T13** `GL_MAJOR==4 && MINOR==6 && CORE_PROFILE` + `FR-core.1` ASan clean | 4.6 core, no GL errors | active |
| FR-core.2 | **T13** `glibberish` line 7 → `ERROR: 0:7` golden substring (active, `T13:T` asserts `ERROR: 0:7` + `glibberish`, spec-review #1 fix) | ERROR:0:7 | active |
| FR-render.1 | **T3a** center pixel 1/255 `MeshRenderer::drawLayer` | 1/255 | active |
| FR-render.2 | **T3b** two quads depth-ordered blend 1/255 `LinkedListOIT` | 1/255 | active |
| FR-render.3 | **T3b** opaque alpha==1.0 + transparent spy flip | alpha 1.0 | active |
| FR-render.4 | **T3a** `SliceRenderer` vertices `distance ≤ ε=1e-4` | ε | active |
| FR-render.5 | **T3a** `PlaneRenderer` corner/center 1/255 + `T9` smoke | 1/255 | active |
| FR-render.6 | **T3b** `VolumeRenderer` center 1/255 tiny synthetic | 1/255 | active |
| FR-app.1 | **T9** `RE_SAMPLE_MAX_FRAMES=20` `1/255` center-pixel via `View` oracle (primary) + `exit 0` secondary + `T16` `N>=3 1/255` smoke | `1/255` via `T9` oracle + `exit 0` secondary, no ASan | active |
| FR-app.2 | **T9** `1280×960` + `4×640×480` + axis 1/255 per-view | 1280×960/640×480 | active |
| FR-app.3 | **T9** `≥90% within 2px` contour `1/255` + `T3a` `ContourRenderer` | 90%/2px | active |

Deferred `FR` none — `T3a/T3b` + `T9` + `T11a/T11b` + `T13` own all 21 (io4+data3+vol3+core2+render6+app3); `COMPLETED_TASKS.md` hist preserved via R3.

## V6 backlog — EMPTY (V6 25/25 green, archived 2026-08-30)

All 25 tasks (T1..T20 incl. splits T3a/b, T8a/b, T11a/b, T14a/b, T15a/b + T17/T18 stretch + T19/T20 hotfixes) have been completed and archived to `COMPLETED_TASKS.md` V6. Next iteration will be planned via `/loop-init`.

> **Naming:** archived backlog was `T1..T20` (consolidated engine iteration + hotfixes). Next `T1..Tn` will be assigned per new iteration.

## Definition of Done — V6 (archived)
- [x] All 25 task gates green; full suite green on a clean tree at T20 (25 tasks, T1..T20 incl. splits + stretch + hotfixes)
- [x] `suite green N>=3` where GL-touching, `audit green`, `ASan+UBSan clean`, `LICENSE` per-dir, `R9` doc-map, `R3` regression, `R4` evidence, `comment_tag_context` PASS, `install` reproduces — see `COMPLETED_TASKS.md` V6 for evidence
