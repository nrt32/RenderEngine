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

### FR → T traceability (regression — no new FRs, V3 preserves V1/V2 gates)

V3 has **no new FRs** (2026-08-23 direction) — every active `T1..T19` is a review follow-up preserving the 20 FRs below via regression lock R3. `COMPLETED_TASKS.md` V1 `T1..T16` + V2 `V2-T1..V2-T8` are the original gates; the table below links each FR to its **regression T** (current iteration — last T that touched that path, not sole verifier) and its **original V1/V2 gate** for audit. Suite green = all 20 FR constants still asserted via full-suite regression gate per R3; no T weakens an FR gate. Original V1 gates remain the binding acceptance per `COMPLETED_TASKS.md`. **R4 evidence rule (spec-review #5):** every `T` — even infra `T3` broker pair-key (`broker.get<MeshObject,ReWrongType>()==nullptr` typed miss, `hash_combine(type_index(AppT),type_index(ReT))` distinct entries) — asserts an **explainable analytic count** (typed null vs UB, `grep -c` 0/1, spy 2→1, `640×480=152 MB` via `w*h*16*32`, `sample(0.5)==0.5±1e-6`), never `non-empty/non-black/>0`; `T3` is infra with `FR:none` but its `nullptr` vs type-punning invariant is the analytic evidence.

| FR | Description (tolerance) | Regression T (current) | Original gate | Acceptance constant |
|---|---|---|---|---|
| FR-io.1 | OBJ loader — vertex/index/AABB vs golden | T17 (via T5 infra) | V1 T4 | bunny `v` hand-count, `AABB` exact |
| FR-io.2 | NRRD loader — dims + corner voxels | T15 (via T13) | V1 T5 | dims ≤128³, corner values exact |
| FR-io.3 | Image loader (stb) — dims + corner/center | T17 (via T13 VG) | V1 T4 | `w*h*ch` + probe pixels |
| FR-io.4 | Loaders reject malformed → typed error | T12 (VG5) + T13 (BudgetExceeded) | V1 T4/T5 | `ErrorDomain::Io` code, `errno ERANGE` |
| FR-data.1 | Mesh face normal analytic | T17 (via T7) | V1 T4 | cross-product within 1e-6 |
| FR-data.2 | Mesh AABB exact | T17 (via T7) | V1 T4 | golden `min/max` exact |
| FR-data.3 | VolumeDataset trilinear vs 8 corners | T7 (preserved via T6 helpers) | V1 T5 | interpolant within 1e-6 |
| FR-vol.1 | TransferFunction control points → RGBA | T18 (TF clamp) | V1 T6 | exact at points, ramp 1e-6 |
| FR-vol.2 | Ray-cast compositing front-to-back | T10 (via T8 OIT docs) | V1 T6 | alpha-blend within 1e-6 |
| FR-vol.3 | Ray/AABB step positions analytic | T10 (via T8) | V1 T6 | step positions analytic |
| FR-core.1 | RAII GL objects no errors/leaks | T4/T5 + T11 sanitizers | V1 T3 | `GL_NO_ERROR` + ASan clean |
| FR-core.2 | ShaderProgram diagnostics `ERROR: 0:7` | T12 (VG1) | V1 T3 | golden substring `glibberish` line 7 |
| FR-render.1 | MeshRenderer center pixel vs analytic | T10 (RI5 hoist) | V1 T7 | center pixel within 1/255 |
| FR-render.2 | OIT depth-sorted composite | T8 (cost table) + T17 | V1 T10 | 1/255 at 3 probes, spy count |
| FR-render.3 | OIT auto-engage on transparent | T8 + T14 (variant collapse) | V1 T7/T10 | `isTransparent` + spy |
| FR-render.4 | SliceRenderer verts on plane ε=1e-4 | T10 (RI2 eps) | V1 T11 | distance ≤ ε |
| FR-render.5 | PlaneRenderer textured quad 1/255 | T7 (via T17) | V1 T8 | corner/center within 1/255 |
| FR-render.6 | VolumeRenderer ray-cast synthetic 1/255 | T6/T7 (asset handles) | V1 T9 | center pixel analytic 1/255 |
| FR-app.1 | Samples exit 0 + no sanitizer (smoke) | T15 (GlfwRuntime) + T17 | V1 T12/T13 | exit code 0, timeout |
| FR-app.2 | MPR 2×2 grid 1280×960 / 640×480 + axis convention | T12 (via T13) | V1 T14 | viewport dims exact, per-axis probe |
| FR-app.3 | MPR contour 90% within 2 px + 3D view | T17 (contour GPU, via T12 overlay) + T12 (View/overlay) | V1 T15 | 90% within 2 px, 1/255 at probe (`tests/t*_contour*`) |

---

## V3 backlog — pure-redesign `scene`/`broker`/View/List/Persistence overhaul (SPEC §10–§12)

Pure-redesign iteration (no new FRs). Priority order **dependency-first** (foundations before dependents, review #4 — file order == execution order, spec-review #5 — sanitizers before GL gates): `T1` hierarchy → `T2` REContext → `T3` pair-key → `T4` draw-cache → `T5` header firewall → `T6` helpers → `T11` sanitizers (re_project_sanitizers on all 9 re_* libs) → `T7` owner-driven handles → `T8` OIT docs → `T9` broker polish → `T10` render internals → `T12` validation (sanitized via T11, N>=3 where GL-touching) → `T13` NRRD pre-probe (Depends:none, pure `std::filesystem::file_size` before slurp; may land before T11/T12 but after is sanitized) → `T14` collapse variant → `T15` GlfwRuntime → `T16` TF → `T17` app batch → `T18` test_utils → `T19` View lights *(stretch, deferred — not required for V3 green)*. Each task is **one session, one reason to change** (SRP) and maps to `SPEC §9.1` V3.x. Each task is **one session, one reason to change** (SRP) and maps to `SPEC §9.1` V3.x. Accepted standard `T1..Tn` per iteration — V2 `V2-T1..V2-T8` archived, V3 now `T1..T19` dependency-ordered (foundations→stores→render→infra→validation sanitized), `V3.x` alias retained only in `roadmap.md` §9.1. **All 13 ★ `SPEC §13` open questions resolved binding 2026-08-23 Sr. Principal review (Q3/Q9/Q27/Q28/Q32f/Q39-Q47) — `open_questions.md:11` header; no ★ blocks V3 kickoff.** Roadmaps `P17..23` alias table §9.1 is historical; binding IDs are `TASKS.md T1..T19`. `T13` is `Depends: none` (file-size probe, no sanitizer/validation dep) — file order after `T12` is batch convenience but valid either before or after `T11`/`T12`.

## V3 documentation map (T-map, R9) — regenerated after reorder (dependency-first, 1:1 with D)

| Task | Spec alias | Docs updated in the same commit |
|---|---|---|
| T1 | — | `scene/iscene_object.*` + `scene/store.*` + `broker/*Mapper` registry (hierarchy, open for extension, 3 phases) |
| T2 | — | `core/re_context.*` (REContext per-GL-context) + `data/README.md` + `data/*/LICENSE` (assets + CT downsample reproducibility) |
| T3 | — | `broker/broker.*` (pair-key {AppT,ReT}) |
| T4 | — | `core/re_context.*` + `docs/render.md` (single draw-state cache via REContext) |
| T5 | — | `core/re_context.hpp` + `core/CMakeLists.txt` (header firewall) |
| T6 | — | `tests/test_helpers.*` + `docs/spec/nfr.md` (infra batch IT1-IT5) |
| T11 | — | `core/CMakeLists.txt` + `cmake/*` + `docs/spec/nfr.md` (sanitizers INTERFACE re_project_sanitizers — before all GL gates) |
| T7 | — | `render/asset_registry.*` + `broker/asset_store.*` + `docs/spec/assets.md` (owner-driven handles, sanitized N>=3) |
| T8 | — | `docs/render.md` + `docs/spec/nfr.md` + `render/itransparency_pipeline.hpp` (OIT cost table, sanitized) |
| T9 | — | `broker/*` + `scene/*` (polish: alias/cycle/gens, 3 phases A2+A3/A5+A6/A7+A8, sanitized) |
| T10 | — | `render/*` + `render/shaders/*` (dedup prologue/quad/hash, perf RI1-RI9, 3 phases, sanitized N>=3) |
| T12 | — | `core/*` + `tests/*` + `utils/CMakeLists.txt` (validation VG1-12, 3 phases, sanitized via T11, GL-touching VG1-3 N>=3) |
| T13 | — | `io/nrrd_volume_loader.*` (size pre-probe BudgetExceeded, Depends:none — sanitized if after T11) |
| T14 | — | `render/types.hpp` (collapse variant IRenderer::render(Scene), sanitized N>=3) |
| T15 | — | `core/glfw_runtime.*` + `utils/offscreen_context.*` + `tools/audit.rules` (GlfwRuntime, sanitized) |
| T16 | — | `volume/transfer_function.*` + `app/mpr_slice.cpp` (TF valid default, pinned ramp) |
| T17 | — | `app/*` (CT dedup, harness, AS1-AS7) |
| T18 | — | `test_utils/*` + `core/re_context.*` + `core/read_pixels.*` (test-support extraction — `offscreen_context` stays in `utils/`) |
| T19 | — | `scene/view.*` + `render/view.*` + `broker/light_mapper.*` + `broker/view_mapper.*` (View explicit lights — stretch/deferred) |

> **Note:** Dependency-first ordering (spec-review #4, fixed 2026-08-27; spec-review #5, fixed 2026-08-27 — sanitizers before GL): file order == execution order; `T11` sanitizers (`re_project_sanitizers` on all 9 `re_*` libs) precede every GL/readback/OIT task (`T7` handles, `T8` OIT, `T9` polish, `T10` dedup, `T12` validation, `T14` collapse) so all 1/255 gates run ASan/UBSan-clean (`cmake --build --verbose | grep -c "-fsanitize.*re_" ==9` proven before T7's first ctest); `T6` helpers precede `T7` stores. `T13` NRRD size pre-probe is `Depends: none` (pure `std::filesystem::file_size` before slurp) and could land before `T11`/`T12` — placement after `T12` is batch convenience (if after T11, gate runs sanitized; if before, N>=1); all GL-touching gates after `T11` claim sanitized N>=3.

> **Task-vs-task ownership (spec-review #5 — T7 vs T14 collision on `render/asset_registry.cpp`):** `T7` owns per-frame hash deletion (`grep -c "hashStableBytes" render/asset_registry.cpp==0` — lazy-hash `lookupVolume`/`lookupImage` paths deleted, T7 gate only); `T14` owns per-renderer `textureFor` map deletion (`grep -c "textureFor" render/*_renderer.*==0` — pointer-keyed texture maps deleted, T14 gate only) and `IRenderer::render(Scene)` variant collapse (`grep -c "IRenderer::render\|using Scene =" render/==0`). The two tasks edit overlapping files but their gates are distinct invariants; `git diff --name-only` after T7 must include `render/asset_registry.*` but not `render/types.hpp` (T14), and each gate's `grep -c` analytic count is independent (no conflation). Order `T7` before `T14` already satisfied (dependency-first).

> **Task-vs-task ownership (spec-review #5b — T2/T4/T5 collision on `core/re_context.*`):** `T2` owns `REContext` rename (`DrawContext→REContext`, `thread_local current()` mapping `GLFWwindow*→REContextState`, `grep -c DrawContext docs/spec/*.md==0` outside historical alias); `T4` owns single draw-state cache unification (`grep -c "g_cache\|g_spy\|invalidateDrawCache" core/==0`, legacy `core/draw.*→core/re_context.*`, `LinkedListOIT` now takes `REContext&` from `ViewCompositor`, spy proves `setViewport` duplicate 2→1); `T5` owns header firewall (`grep -R "#include.*glad" core/*.hpp==0`, `core/CMakeLists.txt` `glad` PRIVATE). File `core/re_context.*` is edited by all three but each owns a distinct grep invariant; order `T2→T4→T5` satisfies dependency (skeleton → unify → firewall) and each phase's `grep -c` gate is independent (no hidden partial green).
> **Active iteration is V3b / pure-redesign; archived V3a is `COMPLETED_TASKS.md` V3: T1..T23 — do not cross-reference bare `T15` (archived `T15`=MPR contour vs active `T15`=GlfwRuntime).**

> **Naming:** active backlog is `T1..T19` dependency-ordered (foundations → stores → render → infra), `V3.x` alias retained only in `roadmap.md` §9.1. Size waiver 2026-08-27 (spec-review #2, extended 2026-08-27 #3, enforced spec-review #5): `T1`/`T9`/`T10`/`T12`/`T17` are each one SRP (hierarchy / broker polish / render dedup / validation / app batch) — three incremental commits per task (T17: CT dedup+harness), failure isolation via phase gates within same task gate; reviewer sign-off waives single-session size rule for these five. Mechanical (enforced via runner): each phase must leave `suite green + audit green` with its own `grep -c` analytic count before next phase and post its own `tools/logs/task_T*_phase*.gate.log` (runner `git commit` after each phase proves independence, no hidden partial green). **Phase gate analytics (binding):** `T1` Phase A `variant<MeshObject==0` via `Factory::create` check, Phase B `kindIndex_` `O(kind)` prove, Phase C `Broker::registeredTypes()` Teapot count 1; `T9` Phase A `aliasByApp_==0 && weak_ptr.*ViewCompositor==0`, Phase B `clearColorGen` bump + `GenerationTracker` single impl, Phase C `count()==6` + staleness contract; `T10` Phase A `LazyProgramCache` + `kMaxTfPoints==8` single-sourced, Phase B `inverse(uViewProj)==0 && uInvViewProj>=1` + clip epsilon unify, Phase C `per-frame TF alloc 0` + `glClear` resize; `T12` Phase A `uniform -1` cache hit 1 + `bind(16)` assert, Phase B `ERANGE` + `Result [[nodiscard]]`, Phase C `Aabb` single def + EGL optional; `T17` Phase `makeCtTransferFunction==1` + `runSample` present + hardcoded ids 0. Sizing: single-session rule waived only for these five with explicit phase logs; future tasks must remain one-session.

---

## T1: `ISceneObject` polymorphic hierarchy — 15+ types, open for extension (forced)

**D** — Discontinue `variant<MeshObject, …>` as the canonical family type
(`scene/object.hpp:144` today). With ≥15 object kinds and a growing set, a closed
variant is not feasible — every new kind edits the variant alias + every visitor.
Introduce `scene/iscene_object.hpp` `ISceneObject { virtual ~ISceneObject()=default;
virtual ObjectId id() const=0; virtual const glm::mat4& transform() const=0;
virtual uint64_t generation() const=0; virtual std::unique_ptr<ISceneObject> clone() const=0;
virtual SceneKind kind() const=0; }` + `ObjectBase<Derived>` CRTP mixin enforcing
`Kind`/`clone` at compile time and sharing the duplicated
`ObjectHeader{ObjectId, transform, generation, setTransform}`.
Fifteen concrete `objects/*.hpp` (Mesh/Volume/Plane/Contour + 10 new) each derive from
`ObjectBase<Derived>` and register via `REGISTER_SCENE_OBJECT(D)` static registrar into
`SceneFactory`/`Broker`. `scene/store.hpp` keeps 5 partitioned
`map<Id, unique_ptr<ISceneObject>>` (indirection #1 keeps `O(kind)` iteration —
no `5→1` erased scan) — a secondary `kindIndex_` restores typed iteration without
branch. Broker becomes `map<SceneKind, unique_ptr<ISceneMapper>>` (Strategy per Kind,
one file per mapper — `ISceneObject` data is processed only by its own mapper).
`T17` `AssetRef<T>` shared-ptr co-ownership stays (object is heap-allocated, asset stays
shared) — the extra `new` per object is amortised by a future slab/arena in
`SceneStore` (not this task). Today `MeshObject` copy is `memcpy` of a 64 B value into
the map node (`addMeshObject` copies by value, no heap for the object itself) — after the
move each object is `make_unique<D>` + map node (one heap for the wrapper, plus the
existing shared-ptr control block for the asset). This cost is accepted for open
extension; the alternative — bespoke `variant` visitor updates on every new kind — is
the blocked path. Semantics of `variant` (trivial copy, exhaustive `std::visit` compile
error) are replaced by virtual `clone()` + startup registry completeness check
(`Factory::create(kind)` fails loud if a Kind lacks a mapper) — runtime, not
compile-time exhaustiveness, but loud. Materials cascade only (per user `e`: lights stay
`variant` — `T1` does not cascade to `LightDesc`).

**Phases (one session, three incremental commits — size waived 2026-08-27 spec-review):**
- *Phase A — ISceneObject + ObjectBase + factory proof:* land `ISceneObject`,
  `ObjectBase<Derived>`, `REGISTER_SCENE_OBJECT` registrar, `SceneFactory` and 2–3
  example kinds (Mesh/Volume/Plane) with broker stub.
- *Phase B — Partitioned SceneStore:* migrate to 5 `map<Id, unique_ptr>` + `kindIndex_`
  typed iteration, preserve `O(kind)` guarantee.
- *Phase C — Broker registry migration:* `Broker` becomes
  `map<SceneKind, unique_ptr<IMapper>>` per-file Strategy; `variant<MeshObject` ==0.

**T** — gate: new `TeapotObject` (or any 16th kind not in the variant) renders through the bridge by adding one header + one `registerMapper<TeapotObject>` line, with zero edits to `store`/`ViewSynchronizer`; `grep -c "variant<MeshObject" scene/` == 0; `Broker::registeredTypes()` contains `TeapotObject` count 1; offscreen center pixel within 1/255 of analytic Teapot composite (not >0); suite green.

**G** — suite green, audit green.

## T2: `REContext` — global state mirror per GL context, multi-thread ready (rename `DrawContext`)

**D** — Rename `DrawContext` → `REContext` (`core/re_context.hpp` — not draw-only; also used by tests and readback, per user direction). Make the context **global per GL context** while preserving future multi-context/multi-threaded rendering: `REContext::current()` = `thread_local` pointer set by `loadCoreGl()` / `makeContextCurrent(GLFWwindow*)` mapping `GLFWwindow* → REContextState`; each context owns its mirror (viewport, clearColor, depthTest, blend, blendFunc, cull, FBO bindings, VAO, program, image units). Single-threaded gate stays (SPEC §5), but state is not process-global singleton — worker threads with private contexts get private mirrors with no lock; shared resources noted out-of-scope (GL share groups). Explicit invalidation at boundaries (`SampleHarness` post-ImGui, `invalidate()` public for tests) — no auto-guess. Delete per-frame local `ctx` instances in renderers; drop ignored `(void)ctx` params from `IRenderable::drawLayer`.

**T** — cross-pass dedup spy proves 2 layers sharing state issue 1 `glViewport`; `current()` switches correctly after `makeContextCurrent` to a second offscreen context; regression R3 byte-identical.

**G** — suite green, audit green.

## T3: Broker — pair-key {AppT, ReT} for get/register (A1)

**D** — `Broker::get<AppT,ReT>` keyed only on `AppT` (`broker.hpp:57-65`) then
`static_cast<IMapper<AppT,ReT>*>` — wrong `ReT` is UB. Fix: key by
`hash_combine(type_index(AppT), type_index(ReT))` for both `ownedByApp_` and
lookup/registration (`registerMapper` same). Mismatch → `nullptr` (typed error
downstream) instead of type-punning; same-`AppT`/different-`ReT` registrations
become distinct entries or asserted (no silent overwrite). `get<MapperT>` stays
exact-keyed as today.

**FR:** none (type-safety; no pixel change).

**T** — suite green; gate: `broker.get<MeshObject, ReWrongType>()` returns
`nullptr` (not a mis-typed pointer); registering `MeshObject→ReMeshObject`
then `get<MeshObject,ReMeshObject>` still finds it.

**G** — suite green, audit green.

## T4: Single draw-state cache — analysis then unify on REContext (R3)

**D** — Analysis-first cleanup of the two live regimes: `core/re_context.cpp` global
`g_cache`/`g_spy` + free functions (used by `LinkedListOIT`) vs instance
`core::REContext` (every pass prologue via `REContext::current()`). Do NOT trust the spec's EOL-5 verdict
a priori. Deliverable (1) analysis note: single-writer discipline per cached
state (viewport/clear/depth/blend), ImGui backend save/restore semantics, and
whether `LinkedListOIT` can take `REContext&` from the compositor flow;
(2) implementation: converge on `REContext`-everywhere — port `LinkedListOIT`
call sites (`linked_list_oit.cpp:193,274,276,280,289`) to accept `REContext&`
from `ViewCompositor` (which already creates one per view via `REContext::current()`), keep legacy global
free functions temporarily for regression-lock tests (`t2_skeletons_test.cpp:242`,
`t6_v2_draw_cache_test.cpp`) then migrate those tests and delete `g_cache`/`g_spy`
+ `invalidateDrawCache()` (legacy `core/draw.*` → `core/re_context.*`; T2 rename). No mixed regime remains; `invalidateDrawCache` test-only
discipline ends.

**FR:** no pixel change.

**T** — suite green; mechanical: `grep -c "g_cache\|g_spy\|invalidateDrawCache" core/` == 0 after migration (or allowlisted legacy shim with expiry note); OIT + prologues share one ledger via `REContext` (spy proves `setViewport` duplicate 2→1, analytic count 1 not `>0`); no skipped-glEnable class bugs possible.

**G** — suite green (N>=3 for OIT/compositor), audit green.

## T5: GL header firewall — move REContext body out of re_context.hpp (R4)

**D** — Make every `core/` public header GL-call-free again. Move `REContext`
inline GL calls/constants out of `core/re_context.hpp:23,174,205-270` (formerly `core/draw.hpp`) into
`core/re_context.cpp` (out-of-line), drop `<glad/gl.h>` from the public header.
Privatize `re_core`'s `glad`/`glfw` linkage where downstream includes permit
(`core/CMakeLists.txt:27-34` PUBLIC → PRIVATE with explicit downstream
deps). Add gate test: no `<glad` include in any installed/public `core/` header.

**FR:** none (header hygiene; R9 row).

**T** — suite green; `grep -R "#include.*glad" core/*.hpp` == 0 hits; downstream
targets (`render/`, `app/`, `tests/`) still build without transitive glad leak
(verified by including `core/re_context.hpp` in a minimal TU that forbids `<glad`).

**G** — suite green, audit green.

## T6: Infra/tests batch — helpers, monolithic binary, env coupling (IT1-IT5)

**D** — Batch infra: `IT2` extract `tests/test_helpers.{hpp,cpp}` (`makeQuadMesh`, `readPixel`, `expectPixel`, `WindowTarget`, `makeCamera`) — single source, ~150 lines removed leaving drift; `IT1`/`IT3`/`IT4`/`IT5` **deferred/documented** — monolithic `re_tests` + `tN_` naming + xvfb hard-fail are intentional gate choices (single context, task traceability, config-fail loudness) — add `IT` note in `docs/spec/nfr.md` and `NAMING_CONVENTIONS.md` rather than restructure now. Double-checked. **Ownership split vs T18 (spec-review #5):** `T6` helpers keep `makeQuadMesh`/`makeCamera`/`WindowTarget` only; pixel-read moves entirely to `test_utils/` in `T18` (`test_utils::PixelReader` via `REContext::readRgba8`/`REContext::current().readRgba8`, raw `glReadPixels` stays `core/re_context.cpp` count 1, `test_utils` count 0). After T18, `grep -c "readPixel" tests/test_helpers.*==0` (deprecated helper migrated), `T18` owns `PixelReader` façade migration (`grep -R "glReadPixels" -- core/ ==1` + `-- test_utils/ ==0`).

**FR:** no gate change.

**T** — suite green; gate: `grep -c "makeQuadMesh" tests/*.cpp` == 1 (helper, analytic count 1); suite still single binary (`ctest -V` shows 1 test `re_tests`); monolithic binary intentional (single GL context).

**G** — suite green, audit green.

## T11: ASan+UBSan for all engine libs (R7)

**D** — Instrument all nine `re_*` static libs, not just test/sample TUs.
Define `INTERFACE` target `re_project_sanitizers`
(`-fsanitize=address,undefined -fno-omit-frame-pointer -O1` under Debug, option-gated)
linked by every `re_*` target; delete ad-hoc per-dir flag blocks
(`tests/CMakeLists.txt:84-90`, `app/CMakeLists.txt:97-103`). Verify sample binaries
remain clean (llvmpipe/Mesa false-positive triage if needed via
`ASAN_OPTIONS` suppressions already proven). Keep Release non-instrumented via
`option(RE_ENABLE_SANITIZERS)`.

**FR:** `SPEC §5` sanitizer contract now covers intra-library errors (stack/scope/intra-object).

**T** — suite green with sanitizers on all libs; `cmake --build --verbose` graph shows
`re_core` etc. compile with `-fsanitize` (exact `grep -c "\-fsanitize.*re_" ==9`, `grep -c "add_compile_options.*-fsanitize" ==0`, known driver suppressions documented).

**G** — suite green, audit green.

## T7: Owner-driven AssetHandles for volumes/images — eliminate per-frame content hashing (R1)

**D** — Make volume/image identity owner-driven like meshes (follow the proven
`registerMeshAsset → AssetId → AssetHandle` path). Today `lookupVolume`/
`lookupImage` recompute FNV-1a over every byte per instance per frame
(`render/asset_registry.cpp:404,459`) violating `data/content_hash.hpp:31`
("hashed at load/register time, never per frame"). Work, staged:
(1) broker mappers (`VolumeObjectMapper`, `VolumeSliceObjectMapper`,
`PlaneObjectMapper`/`PlaneMapper`) register volumes/images through
`SceneStore`/`broker::AssetStore` at sync, handing renderers `AssetHandle`
instead of `shared_ptr<const T>`; renderers' `textureFor` becomes O(1) handle
resolve. Volumes first, then images. (2) delete lazy-hash `lookupVolume`/
`lookupImage` insertion paths and contract-violating comment; keep explicit
register→resolve only. Direct-renderer tests register explicitly in fixtures
(or via a shared test helper). This also closes R8a/R8b: pinned refs==0 lazy
slots can no longer appear and the `byObject_` pointer-key shim is deleted —
content-hash IS identity.

**FR:** no pixel change — `FR-render.*`/`FR-app.*` gates unchanged (regression lock).

**T** — gate asserts (explainable): spy counter proves `hashStableBytes`/FNV
executes **zero** times during a steady-state 60-frame loop after warm-up
(volume + plane); registry slot count constant across 1000 distinct-image
frames (no pinned-slot growth); same `VolumeDataset` registered through two
`VolumeRenderer` instances yields one `Texture3D`; suite green N>=3.

**G** — suite green (N>=3), audit green, lazy-hash lookup paths removed
(`grep -c "hashStableBytes" render/asset_registry.cpp ==0` — T7 owns per-frame hash deletion; per-renderer `textureFor` map removal owned by `T14` `grep -c "textureFor" render/*_renderer.* ==0`).

## T8: OIT memory docs — document no-fallback contract (R2)

**D** — No code fallback. Deployment targets are known, so OIT may legitimately
fail when the SSBO budget (`w*h*16*32`, ~1 GB @1080p) cannot be satisfied.
Align documentation with reality: `docs/render.md` OIT section + `docs/spec/nfr.md`
+ `render/itransparency_pipeline.hpp:47-48` comment are corrected — `begin()`
failure typed error aborts the transparent-capable mesh pass (no silent
blend fallback). Document capacity: per-view cost formula, example table
(640×480≈152 MB, 1080p≈1 GB), and that unsupported/over-budget hardware yields
opaque-only rendering for that pass (typed error surfaced via bridge).

**FR:** none (docs-only; R9 row).

**T** — suite green; `grep -c "renders without OIT" render/itransparency_pipeline.hpp` == 0 (fixed); mechanical doc gates: `grep -c "640×480.*152" docs/render.md ==1` and `grep -c "w\*h\*16\*32" docs/spec/nfr.md ==1`; per-view cost `w*h*16*32` verified — `640×480=152 MB` (4915200*32), `1920×1080≈1.03 GB` analytic, not `>0`.

**G** — suite green, audit green.

## T9: Broker polish — alias removal, cycle, dirty granularity, generation dedup (A2, A3, A5, A6, A7, A8, A4 note)

**D** — Batch broker/`scene` polish trivially deduced during review:
`A2` delete `aliasByApp_` (derive from `ownedByMapper_` — fixes stale raw alias);
`A3` remove `ViewSynchronizer::weak_ptr<ViewCompositor>` cycle — `ViewBridge::sync` passes `compositor*` explicitly;
`A5` add `clearColorGen`/`depthTestGen` per-field gens to `scene::View` + `ViewCache`;
`A6` extract shared `detail::GenerationTracker` for `SceneStore`/`ViewStore` (`recordDirty_`, `dirtyFieldsSince`, tombstones);
`A7` template-generate six `SceneStore` method families and add symmetric `count()` for all kinds;
`A8` unify `SceneStore` staleness contract (typed `resolve` + borrowed accessors with `@note lifetime:`);
`A4` doc-only: `presentAll(core::Framebuffer*)` leak acknowledged, deferred to RHI `IRHIFramebuffer` (note in `docs/render.md` + `broker.md`). All double-checked before fixing — concerns flagged inline.

**Phases (one SRP — broker polish — three incremental commits, waived 2026-08-27):**
- *Phase A (A2+A3 — broker wiring):* delete `aliasByApp_`, remove `weak_ptr<ViewCompositor>` cycle; no API change.
- *Phase B (A5+A6 — generations):* add per-field `clearColorGen`/`depthTestGen`, extract shared `GenerationTracker`.
- *Phase C (A7+A8 — store consistency):* template-generate method families, add `count()`, unify staleness contract (A4 doc defer).

**FR:** no pixel change; broker API tightened.

**T** — suite green; gates: `grep -c "aliasByApp_" broker/` == 0; `grep -c "weak_ptr.*ViewCompositor" broker/` == 0; `setClearColor` bumps dedicated gen; `SceneStore`/`ViewStore` share one `GenerationTracker` impl (no hand-copied duplicate); all six `count()` present.

**G** — suite green, audit green.

## T10: Render internals batch — dedup prologue/quad/constants, perf fixes (RI1-RI9)

**D** — Batch render dedup + perf (all trivial, double-checked before fixing):
`RI1` one `LazyProgramCache` for the eight shader loaders; `RI2` unify GLSL clip classifier epsilons or document divergences; `RI3` single `kMaxTfPoints=8` header + `tfSample` shared include, OIT `kNullNode`/stride/cap constants single-sourced; `RI4` remove per-frame TF vector allocs (reuse/stack); `RI5` hoist `inverse(uViewProj)` to CPU uniform; `RI6` Contour `drawLayer` sets `uView/uProj/uViewport` once; `RI7` document `RE_SHADER_DIR` reloc note (or install/copy shaders) — keep baked path but gate warns; `RI8` ViewTarget resize via `glClear` not zero-upload; `RI9` drop dead `aNormal` pipeline. `RI10` `captureCrossSection` worst-case alloc documented as test-only (WONTFIX).

**Phases (one SRP — render dedup/perf — three incremental commits, waived 2026-08-27):**
- *Phase A (RI1/RI3 — constants):* single-source `LazyProgramCache`, `kMaxTfPoints`, `kNullNode`/OIT constants.
- *Phase B (RI2/RI5/RI6 — shader/uniforms):* unify clip epsilons, hoist `inverse(uViewProj)`, Contour uniform hoist, drop `aNormal`.
- *Phase C (RI4/RI8/RI9 — alloc/perf):* remove per-frame TF allocs, `glClear` resize, document `RE_SHADER_DIR` + RI10 WONTFIX.

**FR:** zero pixel drift on all renderer gates within 1/255 (N>=3).

**T** — suite green (N>=3); greps: `kScreenQuadVerts`/`LazyProgram` deduped, `grep -c "inverse(uViewProj)" render/shaders/ ==0 && grep -c "uInvViewProj" render/shaders/ >=1, per-frame TF alloc count == 0; pixel drift 0 within 1/255 N>=3.

**G** — suite green, audit green.

## T12: Validation gaps batch — uniforms, texture/FBO checks, parsing, Result, Aabb, EGL, hygiene (VG1-VG5, VG7-VG12)

**D** — Batch validation hardening trivially deduced: `VG1` `setUniform*` checks `-1` + location cache (no per-call `std::string` alloc); `VG2` texture unit range `0..15` assert; `VG3` FBO attach/isComplete bind-state asserts; `VG4` `read_pixels` overflow check + `PACK_ALIGNMENT` save/restore; `VG5` OBJ `strtol` `ERANGE` check + `errno` reset; `VG7` `Result<T>` `[[nodiscard]]` on type, `Error` embed retained but documented, monadic `map/andThen` helpers retained from T22 as optional; `VG8` single `Aabb` canonical type (or type-alias) with one default; `VG9` `utils/CMakeLists` EGL `REQUIRED` → optional + `AUDIT_SOURCE_DIRS` grey-zone doc; `VG10` anon-namespace internals in `shader_program.cpp`; `VG11` optional `assert(hasPendingGlError())` debug hook in core wrappers; `VG12` retire `pinned_deps_anchor.hpp` shim, audit `queryGlError` usage, Window teardown dedup, logging level knob doc. `VG6` already via T16. Runs **sanitized** via `T11` `re_project_sanitizers` (dependency order — `T11` precedes `T12`).

**Phases (one SRP — validation hardening — three incremental commits, waived 2026-08-27):**
- *Phase A (VG1-3 — GL validation):* `setUniform` -1 check + cache, texture unit 0..15, FBO attach/isComplete, `read_pixels` overflow + `PACK_ALIGNMENT`.
- *Phase B (VG5+VG7 — parsing/Result):* OBJ `strtol ERANGE`, `Result [[nodiscard]]`, monadic helpers.
- *Phase C (VG8-12 — hygiene):* single `Aabb`, EGL optional, anon-namespace, `hasPendingGlError` hook, retire shim, logging knob.

**FR:** no pixel change; loaders become stricter (malformed giant index → typed error, not silent wrong geometry).

**T** — suite green (ASan+UBSan clean via T11); gates: uniform typo → silent no-op gone (logged/warned, location cache hit count exactly 1 not `>0`); `bind(16)` asserts out-of-range (analytic bound 15); malformed OBJ index → typed error `ERANGE`; `Aabb` single definition (count 1); build with EGL missing still configures (0 PkgConfig failures).

**G** — suite green, audit green.

## T13: NRRD loader size pre-probe with typed error (R10)

**D** — Check file size before the whole-file slurp that today precedes budget
validation. Probe `std::filesystem::file_size` (or `stat`) before reading;
compare against derived ceiling (axis limits 128³ × dtype size) and absolute
cap; on exceed return typed `BudgetExceeded` + `spdlog::warn` with actual vs
limit sizes. Keeps the glitch as "silent failure with appropriate logs" per
user call (typed error to caller, warn log for diagnostics). No behavior
change for valid files.

**FR:** `FR-io.3` preserved; hostile-size file fails fast with typed error, not OOM.

**T** — suite green; gate: >128³ or host-file-size > cap input → typed error
code `BudgetExceeded` and no multi-GB allocation observed (mocked large file
or synthetic header with huge dims); valid volume still loads byte-identical.

**G** — suite green, audit green.

## T14: Collapse transparency to one path — delete IRenderer::render(Scene) variant (R5 + A9)

**D** — Make the bug class unrepresentable. Delete the test-only
`IRenderer::render(const Scene&)` variant dispatch (`render/types.hpp:8-17`
"kept ONLY for the direct single-item render() tests") and the `Scene` alias
`variant<const MeshScene*, ...>`. Keep only `IRenderable::drawLayer` (the broker
path), which has one defined transparent-mesh behavior (compositor's
out-of-band capture when `ITransparencyPipeline` is wired; `drawLayer`
otherwise draws with blending off — never the silent-drop of the direct path).
Port the four dispatch-site tests: `t1_v2_ir_dispatch_test.cpp` exercise moves to
`beginPass` + `drawLayer` via a minimal `View`/`REContext`. Removes the four
copies of dispatch boilerplate and the parallel `re_scene/mesh_object.hpp`
vocabulary's second entry point; pairs with `T2` consolidation but is
independently gated. Also closes A9 vestigial dispatch debt.

**FR:** no pixel change for any gate reached via the broker path (regression lock);
direct-renderer tests ported byte-identical within 1/255.

**T** — suite green; `grep -c "IRenderer::render\|using Scene =" render/` == 0;
`grep -c "Noop\|byObject_" broker/` == 0 (Noop already deleted by T20/T7; this gate adds the dispatch-removal proof; `lookupVolume` hash-path proof owned by `T7` `hashStableBytes==0`); no
transparent-mesh silent-drop path remains (`mesh_renderer.cpp` skipTransparent=true
drop only survives if ever reintroduced — asserted absent) + `grep -c "textureFor" render/*_renderer.* ==0` (per-renderer maps deleted, distinct from `T7` hash deletion).

**G** — suite green (N>=3), audit green.

## T15: GLFW global lifecycle — refcounted GlfwRuntime + tests↛window audit (R6)

**D** — Two owners (`core::Window` visible, `utils::OffscreenContext` hidden
for tests) share one process-global `glfwInit/glfwTerminate` pair with
mismatched policies (`Window` always terminates at `window.cpp:66`,
`OffscreenContext` never does at `offscreen_context.cpp:89-92`). Introduce
`core::GlfwRuntime` — `static mutex + int refs` refcounted RAII
(`acquire()` → `shared_ptr` token: 0→1 `glfwInit`, 1→0 `glfwTerminate`);
both `Window` and `OffscreenContext` hold `shared_ptr<GlfwRuntime>`
instead of raw calls. Add mechanical guardrail: `tools/audit.rules` forbids
`tests/` including `core/window.hpp` (enforces "tests use only offscreen").

**FR:** no behavior change beyond correct global teardown; window creation
smoke still passes.

**T** — suite green; order-independent teardown: `OffscreenContext` + `Window`
created in either order and destroyed in either order leaves no UB/leak with
`GlfwRuntime::refCount()==0` after both destroyed and ASan/LSan clean (simulate via
fixture interleaving test); `grep -R "glfwTerminate" -- core/ utils/`
== 1 hit (inside `glfw_runtime.*` only); `grep -R "window\.hpp" tests/` == 0.

**G** — suite green, audit green.

## T16: TransferFunction — valid default + defensive sample + toByte clamp (R9 + VG6)

**D** — Empty `TransferFunction` is UB (`sample()` derefs `front()` on empty
`transfer_function.cpp:16`) yet ctor accepts any vector (`hpp:34-38`). Fix
(a)+(c): default ctor produces a valid degenerate ramp pinned
`vec4(0,0,0,0) → vec4(1,1,1,1)` (transparent black→opaque white) instead of empty;
`sample()` defensively returns transparent black if points empty; `toByte`
(`mpr_slice.cpp:22-24`) clamps with `std::clamp(v,0.f,1.f)` before `uint8_t` cast
(closes the float→int UB for any out-of-range TF color). Factory `(b)`
(`Result`-returning) deferred as optional.

**FR:** no pixel change for existing TFs (regression lock).

**T** — suite green; gate: `TransferFunction{}.sample(0.0f)==(0,0,0,0)`,
`sample(0.5f)==(0.5,0.5,0.5,0.5)` within 1e-6, `sample(1.0f)==(1,1,1,1)`,
`toByte(1.5f)==255` and `toByte(-0.2f)==0` both clamped and defined (no UB); ASan clean.

**G** — suite green, audit green.

## T17: App/samples batch — CT dedup, harness, hardcoded ids, resize gap (AS1-AS7)

**D** — Batch app polish: `AS1` one `makeCtTransferFunction()` in `app/` shared header; `AS2` `runSample()` helper dedups six `sync→renderAll→presentAll + load→window→run` mains + constants; `AS3` `oit_sample` capture returned `ObjectId`s (no `{1,2,3,4}` hardcode); `AS4`/`AS5`/`AS6`/`AS7` noted but **deferred** per T23 scope overlap — `T23` already owns `onResize` + live aspect, so AS4/AS5 feed into T23; AS6 PPM/box/oracle stays library-grade-documented (no move this batch); AS7 background clear stays sample-side until `presentAll` compositing lands. Double-checked — no larger refactor this task.

**FR:** sample smoke still exits 0; OIT ids stable across store policy changes.

**T** — suite green; gate: `grep -c "makeCtTransferFunction" app/` == 1 definition; `oit_sample` no hardcoded `{1u,2u,3u,4u}`; `runSample` present.

**G** — suite green, audit green.

## T18: Test-support extraction to `test_utils/` — keep RE critical code lean, GL via `REContext`

**D** — Move test-consumed surface out of critical RE into a peer lib `test_utils/` (`AUDIT_SOURCE_DIRS += test_utils`). Candidates identified by the architecture review / user direction: `core/read_pixels.{hpp,cpp}` raw `glReadPixels` anchor (`core/read_pixels.hpp:30` — every pixel-gate test's evidence path), `utils/pixel_reader.*`, `render/linked_list_oit::readCapturedFragmentCount()`, `render/slice_renderer::captureCrossSection()` + `TransformFeedback` harness. `utils/offscreen_context.*` **stays in `utils/`** (owned by `T15 GlfwRuntime` — `T15` and `T18` no longer collide; `T15` owns `OffscreenContext` lifetime via `GlfwRuntime`, `T18` owns `PixelReader`/`read_pixels`/capture helpers only). Raw `gl*` stays exclusive to `REContext` (`core/re_context.cpp` is the only `glReadPixels`/`glGetBufferSubData` site; audit `gpu_api_ownership` / `no_production_readback` now allow `core|test_utils` — raw stays `core`, façade in `test_utils`). New peer lib `test_utils/` (`AUDIT_SOURCE_DIRS += test_utils`, `disposition` rules updated). `test_utils::PixelReader` calls `REContext::current().readRgba8(…)`, not a second anchor. **Constraint:** every context-setting GL call still flows through `T2 REContext` — no test helper touches raw GL.

**T** — `grep -R "glReadPixels" -- core/` == 1 hit (inside `re_context.cpp`), `grep -R "glReadPixels" -- test_utils/` == 0; suite still green via `test_utils::PixelReader`; `utils/offscreen_context.*` remains in `utils/` (not moved).

**G** — suite green, audit green.

## T19: `View` explicit lights field — *(stretch, deferred)*

**D** — *(stretch — deferred per SPEC §1 non-goal Phong-only + SPEC §12.1/12.3 `ILight` hierarchy stays spec-only this iteration; promotes to binding only when SPEC §1/§12 promote lights)* `scene::View` and `render::View` gain `vector<Light> lights` (was implicit/absent). App: `Light { Type dir/point/spot; vec3 pos/dir; vec4 color; float intensity; … }` + `setLights()` bumping `lightsGen` (adds to `CompositeKey` per §10). RE: `ReLight` (derived uniform-ready) uploaded per view before `drawLayer` loop; empty vector = unlit (2D). Broker: `LightMapper : IMapper<Light,ReLight>` + `ViewMapper` composes `LightMapper`. Persistence: `lightsGen` participates in `ViewSynchronizer` dirty check (per-field, not whole-view dump). This task is **not required for V3 green**; SPEC §12 `Light` hierarchy remains spec-only until promoted — **gate only enforced when SPEC §1/§12 promote lights; otherwise DoD line waived (stretch)**.

**T** — two lights on one view produce analytic two-light composite distinct from single-light within `1/255` at probe; empty lights = unlit as before — **stretch gate: only enforced when lights promoted per SPEC §1/§12; waived until then**.

**G** — suite green, audit green — **stretch deferred; not required for V3 green while SPEC §1/§12 Phong-only non-goal holds**.

## Definition of Done — review follow-ups (T1–T19 reordered, dependency-first, 1:1 with D)

**Loop artifacts (generic, every T):**
- [ ] `suite green` — `N>=3` for any GL/readback/OIT/spy task (`T4` draw-cache spy, `T7` handles 60-frame hash 0 + slot growth 0, `T8` OIT, `T10` dedup zero drift, `T11` sanitizer N>=3 toolchain prove, `T12` VG1-3 texture/FBO/REContext spy where GL-touching, `T14` collapse, `T15` GlfwRuntime order-independent teardown, plus any `readRgba8`/`glViewport` spy gate; `tools/logs/task_*.gate.log` shows 3 consecutive `ctest Passed`, `GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6`), `N>=1` otherwise (pure CPU/logic tasks `T3` pair-key, `T5` firewall, `T6` helpers, `T9` polish where CPU-only, `T13` NRRD pre-probe, `T16` TF, `T17` app batch where harness smoke, `T18` test_utils façade (0 raw), `T19` stretch lights) — **GL-touching subset of `T12` inherits `N>=3`; pure-CPU subset of `T12` is `N>=1` sanitized via T11**
- [ ] `audit green` with `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` — `tools/audit.sh` PASS incl. `disposition_scene/render` (mechanical `forbid_inside`), `broker_per_type`, `no_dump_sync`, `asset_indirection`, `ownership_raw_ptr_*`, `comment_tag_context`, `render_no_glad`, `no_noop_broker`, `assets_licensed` per-dir (audit floor `require_grep LICENSE` is floor only; per-dataset-dir gate `test -f data/meshes/LICENSE && test -f data/volumes/LICENSE` via T2 enforces completeness, audit.sh complements with `git ls-files` per-dir check where present)
- [ ] `ASan+UBSan clean` on all `re_*` libs (not just tests) + samples exit 0 under `xvfb` (FR-app.1) — `option(RE_ENABLE_SANITIZERS)` ON for Debug, `ASAN_OPTIONS` suppressions documented in `docs/spec/env.md` + `docs/spec/nfr.md`
- [ ] `LICENSE` beside every dataset dir (`data/meshes/LICENSE`, `data/volumes/LICENSE` enumerated) — `test -f data/meshes/LICENSE && test -f data/volumes/LICENSE` + `grep -c LICENSE` floor — T2 gate
- [ ] `R9` doc-map: `git diff --name-only` includes listed docs per T-map row (review checks)

**Per-task gates (reordered T1–T19, 1:1 with D):**
- [ ] `T1` hierarchy: `ISceneObject` base + `ObjectBase<Derived>` — 15+ types open via `REGISTER_SCENE_OBJECT`, `variant<MeshObject` ==0, partitioned store `O(kind)` (5 maps + kindIndex_), Broker `map<SceneKind,unique_ptr<IMapper>>` — suite green (3 phases A/B/C).
- [ ] `T2` REContext: `DrawContext→REContext` global per-GL-context `thread_local current()`, `g_cache` deleted, cross-pass spy 2→1 proves dedup, second offscreen context switches `current()` — suite green.
- [ ] `T3` pair-key: `Broker::get<AppT,ReT>` pair-key `{AppT,ReT}` via `hash_combine(type_index(AppT),type_index(ReT))` — wrong `ReT` returns `nullptr` not UB (typed miss); distinct registrations for same AppT/different ReT — suite green.
- [ ] `T4` draw-cache: single-writer analysis note + unify on `REContext` (port `LinkedListOIT` to `REContext&`, `g_cache/g_spy/invalidateDrawCache==0`, legacy `core/draw.*` → `core/re_context.*`) — spy proves `setViewport` duplicate 2→1 (count 1 not >0) — suite green N>=3.
- [ ] `T5` header firewall: no `<glad` in any `core/*.hpp` (`grep -R "#include.*glad" core/*.hpp ==0`), `core/CMakeLists` `glad` PRIVATE — minimal TU including `core/re_context.hpp` builds without transitive glad leak.
- [ ] `T6` infra batch: `tests/test_helpers.*` single source `makeQuadMesh==1` (helper, analytic count 1), `re_tests` single binary preserved (`ctest -V` shows 1 test) — suite green.
- [ ] `T7` owner-driven handles: volume/image `lookupVolume/Image` lazy-hash deleted, spy hash count 0 over 60-frame loop after warm-up (volume+plane), registry slot growth 0 over 1000 distinct-image frames, same VolumeDataset via two renderers → one Texture3D — suite green N>=3.
- [ ] `T8` OIT docs: per-view cost `w*h*16*32` table verified — `640×480=152 MB` (4915200*32), `1920×1080≈1.03 GB` (analytic ~1037 MB) + `grep "renders without OIT" render/itransparency_pipeline.hpp ==0`; doc mechanical: `grep -c "640×480.*152" docs/render.md ==1` and `grep -c "w\*h\*16\*32" docs/spec/nfr.md ==1`.
- [ ] `T9` broker polish: `aliasByApp_==0`, `weak_ptr<ViewCompositor>==0`, `setClearColor` bumps dedicated `clearColorGen`, `SceneStore/ViewStore` share one `GenerationTracker` impl, all six `count()` present — suite green (3 phases).
- [ ] `T10` render dedup: `kScreenQuadVerts`/`LazyProgram` deduped, `grep -c "inverse(uViewProj)" render/shaders/ ==0 && grep -c "uInvViewProj" render/shaders/ >=1, per-frame TF alloc 0, `kMaxTfPoints==8` single-sourced — zero pixel drift 1/255 N>=3 (3 phases).
- [ ] `T11` sanitizers: `INTERFACE re_project_sanitizers` (`-fsanitize=address,undefined -fno-omit-frame-pointer -O1` Debug, option-gated) linked by all 9 `re_*` libs, `cmake --build --verbose` shows `-fsanitize` on `re_core` etc. (exact `grep -c "\-fsanitize.*re_" ==9` + `grep -c "add_compile_options.*-fsanitize" ==0`), ad-hoc per-dir flag blocks deleted (`tests/CMakeLists.txt:84-90`, `app/CMakeLists.txt:97-103`), known driver suppressions via `ASAN_OPTIONS` documented — suite green.
- [ ] `T12` validation: uniform `-1` checked + location cache hit count exactly 1 (not >0), `bind(16)` asserts out-of-range (bound 15), malformed OBJ index → typed error `ERANGE` (`errno` reset), single `Aabb` definition (count 1), build with EGL missing still configures (0 PkgConfig failures) — suite green (sanitized via T11) — **N>=1 sanitized, N>=3 for GL-touching VG1-3 (`setUniform`/`bind`/`FBO`/`PACK_ALIGNMENT` spy where GL context involved; `T11` sanitizers already prove `N>=3` toolchain, validation CPU gates `N>=1` but GL spy gates `N>=3`)**.
- [ ] `T13` NRRD pre-probe: `std::filesystem::file_size` before slurp; >128³ or >cap → typed error `BudgetExceeded` + `spdlog::warn` with actual vs limit, no multi-GB alloc (mocked large file or synthetic huge header); valid volume loads byte-identical — suite green (Depends:none, sanitized N>=1; if after `T11` then ASan clean via `re_project_sanitizers`).
- [ ] `T14` collapse variant: `IRenderer::render(Scene)` + `using Scene =` variant deleted (`grep -c "IRenderer::render\|using Scene =" render/ ==0`), `drawLayer` only (broker path); 4 dispatch-site tests ported via `View/REContext`, no transparent-mesh silent-drop path remains — suite green N>=3.
- [ ] `T15` GlfwRuntime: `core::GlfwRuntime` refcounted RAII (`static mutex + int refs`, `acquire()->shared_ptr`); order-independent teardown (OffscreenContext+Window either order leaves `refCount()==0` + no UB/leak, ASan/LSan clean, `grep -R "glfwTerminate" -- core/ utils/ ==1` inside `glfw_runtime.*` only, `grep -R "window\.hpp" tests/ ==0`) — suite green.
- [ ] `T16` TransferFunction: default ramp pinned `vec4(0,0,0,0) → vec4(1,1,1,1)` — `sample(0.0)==(0,0,0,0)`, `sample(0.5)==(0.5,0.5,0.5,0.5)` within 1e-6, `sample(1.0)==(1,1,1,1)`; `toByte(1.5f)` clamped 255, `toByte(-0.2f)` clamped 0 — ASan clean.
- [ ] `T17` app batch: `grep -c "makeCtTransferFunction" app/ ==1` definition (shared header), `runSample()` present deduping six mains, `oit_sample` no hardcoded `{1u,2u,3u,4u\}`; sample smoke exits 0, OIT ids stable across store policy — suite green.
- [ ] `T18` test_utils: `grep -R "glReadPixels" -- core/ ==1` inside `re_context.cpp`, `grep -R "glReadPixels" -- test_utils/ ==0`; `test_utils::PixelReader` via `REContext::current().readRgba8` (no second anchor), `AUDIT_SOURCE_DIRS` includes `test_utils`, disposition + gpu ownership allow `test_utils` façade only — suite green.
- [ ] `T19` View lights *(stretch — deferred)*: `scene::View` and `render::View` gain `vector<Light> lights` (`setLights()` bumps `lightsGen` into `CompositeKey`); Broker `LightMapper : IMapper<Light,ReLight>` + `ViewMapper` composes; two lights on one view produce analytic two-light composite distinct from single-light within 1/255 at probe; empty vector = unlit (2D) as before — suite green; not required for V3 green while SPEC §1/§12 Phong-only non-goal holds.
