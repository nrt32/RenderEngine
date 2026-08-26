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

### FR → T traceability (regression — no new FRs, V3 preserves V1/V2 gates)

V3 has **no new FRs** (2026-08-23 direction) — every active `T1..T19` is a review follow-up preserving the 20 FRs below via regression lock R3. `COMPLETED_TASKS.md` V1 `T1..T16` + V2 `V2-T1..V2-T8` are the original gates; the table below links each FR to its **regression T** (current iteration) and its **original V1/V2 gate** for audit.

| FR | Description (tolerance) | Regression T (current) | Original gate | Acceptance constant |
|---|---|---|---|---|
| FR-io.1 | OBJ loader — vertex/index/AABB vs golden | T17 (via T5 infra) | V1 T4 | bunny `v` hand-count, `AABB` exact |
| FR-io.2 | NRRD loader — dims + corner voxels | T15 (via T13) | V1 T5 | dims ≤128³, corner values exact |
| FR-io.3 | Image loader (stb) — dims + corner/center | T17 (via T13 VG) | V1 T4 | `w*h*ch` + probe pixels |
| FR-io.4 | Loaders reject malformed → typed error | T15 + T13 (VG5) | V1 T4/T5 | `ErrorDomain::Io` code, `errno ERANGE` |
| FR-data.1 | Mesh face normal analytic | T17 (via T7) | V1 T4 | cross-product within 1e-6 |
| FR-data.2 | Mesh AABB exact | T17 (via T7) | V1 T4 | golden `min/max` exact |
| FR-data.3 | VolumeDataset trilinear vs 8 corners | T6 (via T7) | V1 T5 | interpolant within 1e-6 |
| FR-vol.1 | TransferFunction control points → RGBA | T18 (TF clamp) | V1 T6 | exact at points, ramp 1e-6 |
| FR-vol.2 | Ray-cast compositing front-to-back | T10 (via T8 OIT docs) | V1 T6 | alpha-blend within 1e-6 |
| FR-vol.3 | Ray/AABB step positions analytic | T10 (via T8) | V1 T6 | step positions analytic |
| FR-core.1 | RAII GL objects no errors/leaks | T4/T5 + T14 sanitizers | V1 T3 | `GL_NO_ERROR` + ASan clean |
| FR-core.2 | ShaderProgram diagnostics `ERROR: 0:7` | T13 (VG1) | V1 T3 | golden substring `glibberish` line 7 |
| FR-render.1 | MeshRenderer center pixel vs analytic | T10/T12 (RI5 hoist) | V1 T7 | center pixel within 1/255 |
| FR-render.2 | OIT depth-sorted composite | T8 (cost table) + T17 | V1 T10 | 1/255 at 3 probes, spy count |
| FR-render.3 | OIT auto-engage on transparent | T8 + T14 (variant collapse) | V1 T7/T10 | `isTransparent` + spy |
| FR-render.4 | SliceRenderer verts on plane ε=1e-4 | T10 (RI2 eps) | V1 T11 | distance ≤ ε |
| FR-render.5 | PlaneRenderer textured quad 1/255 | T7 (via T17) | V1 T8 | corner/center within 1/255 |
| FR-render.6 | VolumeRenderer ray-cast synthetic 1/255 | T6/T7 (asset handles) | V1 T9 | center pixel analytic 1/255 |
| FR-app.1 | Samples exit 0 + no sanitizer (smoke) | T19 + T17 (GlfwRuntime) | V1 T12/T13 | exit code 0, timeout |
| FR-app.2 | MPR 2×2 grid 1280×960 / 640×480 + axis convention | T11 (via T13) | V1 T14 | viewport dims exact, per-axis probe |
| FR-app.3 | MPR contour 90% within 2 px + 3D view | T2 (hierarchy) + T11 | V1 T15 | 90% band, 1/255 |

---

## V3 backlog — pure-redesign `scene`/`broker`/View/List/Persistence overhaul (SPEC §10–§12)

Pure-redesign iteration (no new FRs). Priority order **dependency-first** (foundations before dependents, review #4): `T1` hierarchy → `T2` REContext → `T3` pair-key → `T4` draw-cache → `T5` header firewall → `T6`/`T7` asset stores → `T8` OIT docs → `T9` broker polish → `T10` render internals → app/infra/validation → `T18` test_utils → `T19` View lights. Each task is **one session, one reason to change** (SRP) and maps to `SPEC §9.1` V3.x. Accepted standard `T1..Tn` per iteration — V2 `V2-T1..V2-T8` archived, V3 now `T1..T10` (not `T9..T18`). **All 13 ★ `SPEC §13` open questions resolved binding 2026-08-23 Sr. Principal review (Q3/Q9/Q27/Q28/Q32f/Q39-Q47) — `open_questions.md:11` header; no ★ blocks V3 kickoff.**

## V3 documentation map (T-map, R9) — regenerated after reorder (dependency-first)

| Task | Spec alias | Docs updated in the same commit |
|---|---|---|
| T1 | — | `scene/iscene_object.*` + `scene/store.*` (hierarchy, open for extension) |
| T2 | — | `core/re_context.*` (REContext per-GL-context) |
| T3 | — | `broker/broker.*` (pair-key {AppT,ReT}) |
| T4 | — | `core/draw.*` + `docs/render.md` (single draw-state cache) |
| T5 | — | `core/draw.hpp` + `core/CMakeLists.txt` (header firewall) |
| T6 | — | `docs/spec/assets.md` + `broker/asset_store.*` (unified multi-kind store) |
| T7 | — | `render/asset_registry.*` (owner-driven handles) |
| T8 | — | `docs/render.md` + `docs/spec/nfr.md` (OIT cost table) |
| T9 | — | `broker/*` + `scene/*` (polish: alias/cycle/gens) |
| T10 | — | `render/*` + `render/shaders/*` (dedup prologue/quad/hash, perf) |
| T11 | — | `app/*` (CT dedup, harness) |
| T12 | — | `tests/test_helpers.*` (infra batch) |
| T13 | — | `core/*` + `tests/*` (validation VG1-12) |
| T14 | — | `core/CMakeLists.txt` (sanitizers) |
| T15 | — | `io/nrrd_volume_loader.*` (size pre-probe) |
| T16 | — | `render/types.hpp` (collapse variant) |
| T17 | — | `core/glfw_runtime.*` + `tools/audit.rules` (GlfwRuntime) |
| T18 | — | `volume/transfer_function.*` + `app/mpr_slice.cpp` (TF valid default) |
| T19 | — | `test_utils/*` + `core/re_context.*` (test-support extraction) |

> **Naming:** active backlog is `T1..T19` reordered by dependency (foundations → asset stores → render/app → infra). `V3.x` survives only as Spec alias in `docs/spec/roadmap.md` §9.1.

---

## T1: `ISceneObject` polymorphic hierarchy — 15+ types, open for extension (forced)

**D** — Discontinue `variant<MeshObject, …>` as the canonical family type (`scene/object.hpp:144` today). With ≥15 object kinds and a growing set, a closed variant is not feasible — every new kind edits the variant alias + every visitor. Introduce `scene/iscene_object.hpp` `ISceneObject { virtual ~ISceneObject()=default; virtual ObjectId id() const=0; virtual const glm::mat4& transform() const=0; virtual uint64_t generation() const=0; virtual std::unique_ptr<ISceneObject> clone() const=0; virtual SceneKind kind() const=0; }` + `ObjectBase<Derived>` CRTP mixin enforcing `Kind`/`clone` at compile time and sharing the duplicated `ObjectHeader{ObjectId, transform, generation, setTransform}`. Fifteen concrete `objects/*.hpp` (Mesh/Volume/Plane/Contour + 10 new) each derive from `ObjectBase<Derived>` and register via `REGISTER_SCENE_OBJECT(D)` static registrar into `SceneFactory`/`Broker`. `scene/store.hpp` keeps 5 partitioned `map<Id, unique_ptr<ISceneObject>>` (indirection #1 keeps `O(kind)` iteration — no `5→1` erased scan) — a secondary `kindIndex_` restores typed iteration without branch. Broker becomes `map<SceneKind, unique_ptr<ISceneMapper>>` (Strategy per Kind, one file per mapper — `ISceneObject` data is processed only by its own mapper). `T17` `AssetRef<T>` shared-ptr co-ownership stays (object is heap-allocated, asset stays shared) — the extra `new` per object is amortised by a future slab/arena in `SceneStore` (not this task). Today `MeshObject` copy is `memcpy` of a 64 B value into the map node (`addMeshObject` copies by value, no heap for the object itself) — after the move each object is `make_unique<D>` + map node (one heap for the wrapper, plus the existing shared-ptr control block for the asset). This cost is accepted for open extension; the alternative — bespoke `variant` visitor updates on every new kind — is the blocked path. Semantics of `variant` (trivial copy, exhaustive `std::visit` compile error) are replaced by virtual `clone()` + startup registry completeness check (`Factory::create(kind)` fails loud if a Kind lacks a mapper) — runtime, not compile-time exhaustiveness, but loud. Materials cascade only (per user `e`: lights stay `variant` — `T1` does not cascade to `LightDesc`).

**T** — gate: new `TeapotObject` (or any 16th kind not in the variant) renders through the bridge by adding one header + one `registerMapper<TeapotObject>` line, with zero edits to `store`/`ViewSynchronizer`; `grep -c "variant<MeshObject" scene/` == 0; suite green.

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

## T4: Single draw-state cache — analysis then unify on DrawContext (R3)

**D** — Analysis-first cleanup of the two live regimes: `core/draw.cpp` global
`g_cache`/`g_spy` + free functions (used by `LinkedListOIT`) vs instance
`core::DrawContext` (every pass prologue). Do NOT trust the spec's EOL-5 verdict
a priori. Deliverable (1) analysis note: single-writer discipline per cached
state (viewport/clear/depth/blend), ImGui backend save/restore semantics, and
whether `LinkedListOIT` can take `DrawContext&` from the compositor flow;
(2) implementation: converge on `DrawContext`-everywhere — port `LinkedListOIT`
call sites (`linked_list_oit.cpp:193,274,276,280,289`) to accept `DrawContext&`
from `ViewCompositor` (which already creates one per view), keep legacy global
free functions temporarily for regression-lock tests (`t2_skeletons_test.cpp:242`,
`t6_v2_draw_cache_test.cpp`) then migrate those tests and delete `g_cache`/`g_spy`
+ `invalidateDrawCache()`. No mixed regime remains; `invalidateDrawCache` test-only
discipline ends.

**FR:** no pixel change.

**T** — suite green; mechanical: `grep -c "g_cache\|g_spy\|invalidateDrawCache" core/` == 0 after migration (or allowlisted legacy shim with expiry note); OIT + prologues share one ledger (spy proves `setViewport` duplicate 2→1, analytic count 1 not `>0`); no skipped-glEnable class bugs possible.

**G** — suite green (N>=3 for OIT/compositor), audit green.

## T5: GL header firewall — move DrawContext body out of draw.hpp (R4)

**D** — Make every `core/` public header GL-call-free again. Move `DrawContext`
inline GL calls/constants out of `core/draw.hpp:23,174,205-270` into
`core/draw.cpp` (out-of-line), drop `<glad/gl.h>` from the public header.
Privatize `re_core`'s `glad`/`glfw` linkage where downstream includes permit
(`core/CMakeLists.txt:27-34` PUBLIC → PRIVATE with explicit downstream
deps). Add gate test: no `<glad` include in any installed/public `core/` header.

**FR:** none (header hygiene; R9 row).

**T** — suite green; `grep -R "#include.*glad" core/*.hpp` == 0 hits; downstream
targets (`render/`, `app/`, `tests/`) still build without transitive glad leak
(verified by including `core/draw.hpp` in a minimal TU that forbids `<glad`).

**G** — suite green, audit green.

## T6: Infra/tests batch — helpers, monolithic binary, env coupling (IT1-IT5)

**D** — Batch infra: `IT2` extract `tests/test_helpers.{hpp,cpp}` (`makeQuadMesh`, `readPixel`, `expectPixel`, `WindowTarget`, `makeCamera`) — single source, ~150 lines removed leaving drift; `IT1`/`IT3`/`IT4`/`IT5` **deferred/documented** — monolithic `re_tests` + `tN_` naming + xvfb hard-fail are intentional gate choices (single context, task traceability, config-fail loudness) — add `IT` note in `docs/spec/nfr.md` and `NAMING_CONVENTIONS.md` rather than restructure now. Double-checked.

**FR:** no gate change.

**T** — suite green; gate: `grep -c "makeQuadMesh" tests/*.cpp` == 1 (helper, analytic count 1); suite still single binary (`ctest -V` shows 1 test `re_tests`); monolithic binary intentional (single GL context).

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
(`grep -c "lookupVolume\|lookupImage" render/` reports only the new explicit
register/resolve names).

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

**T** — suite green; `grep -c "renders without OIT" render/itransparency_pipeline.hpp` == 0 (fixed); doc review gate with explainable constants: per-view cost `w*h*16*32` verified — `640×480=152 MB`, `1920×1080≈1.03 GB` (analytic, not `>0`).

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

**FR:** no pixel change; broker API tightened.

**T** — suite green; gates: `grep -c "aliasByApp_" broker/` == 0; `grep -c "weak_ptr.*ViewCompositor" broker/` == 0; `setClearColor` bumps dedicated gen; `SceneStore`/`ViewStore` share one `GenerationTracker` impl (no hand-copied duplicate); all six `count()` present.

**G** — suite green, audit green.

## T10: Render internals batch — dedup prologue/quad/constants, perf fixes (RI1-RI9)

**D** — Batch render dedup + perf (all trivial, double-checked before fixing):
`RI1` one `LazyProgramCache` for the eight shader loaders; `RI2` unify GLSL clip classifier epsilons or document divergences; `RI3` single `kMaxTfPoints=8` header + `tfSample` shared include, OIT `kNullNode`/stride/cap constants single-sourced; `RI4` remove per-frame TF vector allocs (reuse/stack); `RI5` hoist `inverse(uViewProj)` to CPU uniform; `RI6` Contour `drawLayer` sets `uView/uProj/uViewport` once; `RI7` document `RE_SHADER_DIR` reloc note (or install/copy shaders) — keep baked path but gate warns; `RI8` ViewTarget resize via `glClear` not zero-upload; `RI9` drop dead `aNormal` pipeline. `RI10` `captureCrossSection` worst-case alloc documented as test-only (WONTFIX).

**FR:** zero pixel drift on all renderer gates within 1/255 (N>=3).

**T** — suite green (N>=3); greps: `kScreenQuadVerts`/`LazyProgram` deduped, `grep -c "inverse(uViewProj)" render/shaders` == 0 (or uniform exists), per-frame TF alloc count == 0.

**G** — suite green, audit green.

## T11: Validation gaps batch — uniforms, texture/FBO checks, parsing, Result, Aabb, EGL, hygiene (VG1-VG5, VG7-VG12)

**D** — Batch validation hardening trivially deduced: `VG1` `setUniform*` checks `-1` + location cache (no per-call `std::string` alloc); `VG2` texture unit range `0..15` assert; `VG3` FBO attach/isComplete bind-state asserts; `VG4` `read_pixels` overflow check + `PACK_ALIGNMENT` save/restore; `VG5` OBJ `strtol` `ERANGE` check + `errno` reset; `VG7` `Result<T>` `[[nodiscard]]` on type, `Error` embed retained but documented, monadic `map/andThen` helpers retained from T22 as optional; `VG8` single `Aabb` canonical type (or type-alias) with one default; `VG9` `utils/CMakeLists` EGL `REQUIRED` → optional + `AUDIT_SOURCE_DIRS` grey-zone doc; `VG10` anon-namespace internals in `shader_program.cpp`; `VG11` optional `assert(hasPendingGlError())` debug hook in core wrappers; `VG12` retire `pinned_deps_anchor.hpp` shim, audit `queryGlError` usage, Window teardown dedup, logging level knob doc. `VG6` already via T16.

**FR:** no pixel change; loaders become stricter (malformed giant index → typed error, not silent wrong geometry).

**T** — suite green; gates: uniform typo → silent no-op gone (logged/warned, location cache hit count exactly 1 not `>0`); `bind(16)` asserts out-of-range (analytic bound 15); malformed OBJ index → typed error `ERANGE`; `Aabb` single definition (count 1); build with EGL missing still configures (0 PkgConfig failures).

**G** — suite green, audit green.

## T12: ASan+UBSan for all engine libs (R7)

**D** — Instrument all nine `re_*` static libs, not just test/sample TUs.
Define `INTERFACE` target `re_project_sanitizers`
(`-fsanitize=address,undefined -fno-omit-frame-pointer -O1` under Debug, option-gated)
linked by every `re_*` target; delete ad-hoc per-dir flag blocks
(`tests/CMakeLists.txt:84-90`, `app/CMakeLists.txt:97-103`). Verify sample binaries
remain clean (llvmpipe/Mesa false-positive triage if needed via
`ASAN_OPTIONS` suppressions already proven). Keep Release non-instrumented via
`option(RE_ENABLE_SANITIZERS)`.

**FR:** `SPEC §5` sanitizer contract now covers intra-library errors (stack/scope/intra-object).

**T** — suite green with sanitizers on all libs; `cmake --build` graph shows
`re_core` etc. compile with `-fsanitize`; known driver suppressions documented.

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
`beginPass` + `drawLayer` via a minimal `View`/`DrawContext`. Removes the four
copies of dispatch boilerplate and the parallel `re_scene/mesh_object.hpp`
vocabulary's second entry point; pairs with `T2` consolidation but is
independently gated. Also closes A9 vestigial dispatch debt.

**FR:** no pixel change for any gate reached via the broker path (regression lock);
direct-renderer tests ported byte-identical within 1/255.

**T** — suite green; `grep -c "IRenderer::render\|using Scene =" render/` == 0;
`grep -c "Noop\|byObject_\|lookupVolume\|lookupImage" broker/` == 0 (Noop already
deleted by T20/T7; this gate adds the dispatch-removal proof); no
transparent-mesh silent-drop path remains (`mesh_renderer.cpp` skipTransparent=true
drop only survives if ever reintroduced — asserted absent).

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
created in either order and destroyed in either order leaves no UB/leak
(simulate via fixture interleaving test); `grep -R "glfwTerminate" -- core/ utils/`
== 1 hit (inside `glfw_runtime.*` only); `grep -R "window\.hpp" tests/` == 0.

**G** — suite green, audit green.

## T16: TransferFunction — valid default + defensive sample + toByte clamp (R9 + VG6)

**D** — Empty `TransferFunction` is UB (`sample()` derefs `front()` on empty
`transfer_function.cpp:16`) yet ctor accepts any vector (`hpp:34-38`). Fix
(a)+(c): default ctor produces a valid degenerate ramp (transparent black→opaque white
or black→white) instead of empty; `sample()` defensively returns transparent black
if points empty; `toByte` (`mpr_slice.cpp:22-24`) clamps with `std::clamp(v,0.f,1.f)`
before `uint8_t` cast (closes the float→int UB for any out-of-range TF color).
Factory `(b)` (`Result`-returning) deferred as optional.

**FR:** no pixel change for existing TFs (regression lock).

**T** — suite green; gate: `TransferFunction{}.sample(0.5f)` is defined (no UB,
returns degenerate ramp color); `toByte(1.5f)` and `toByte(-0.2f)` both defined
(clamped); ASan clean.

**G** — suite green, audit green.

## T17: App/samples batch — CT dedup, harness, hardcoded ids, resize gap (AS1-AS7)

**D** — Batch app polish: `AS1` one `makeCtTransferFunction()` in `app/` shared header; `AS2` `runSample()` helper dedups six `sync→renderAll→presentAll + load→window→run` mains + constants; `AS3` `oit_sample` capture returned `ObjectId`s (no `{1,2,3,4}` hardcode); `AS4`/`AS5`/`AS6`/`AS7` noted but **deferred** per T23 scope overlap — `T23` already owns `onResize` + live aspect, so AS4/AS5 feed into T23; AS6 PPM/box/oracle stays library-grade-documented (no move this batch); AS7 background clear stays sample-side until `presentAll` compositing lands. Double-checked — no larger refactor this task.

**FR:** sample smoke still exits 0; OIT ids stable across store policy changes.

**T** — suite green; gate: `grep -c "makeCtTransferFunction" app/` == 1 definition; `oit_sample` no hardcoded `{1u,2u,3u,4u}`; `runSample` present.

**G** — suite green, audit green.

## T18: Test-support extraction to `test_utils/` — keep RE critical code lean, GL via `REContext`

**D** — Move test-consumed surface out of critical RE into a peer lib `test_utils/` (`AUDIT_SOURCE_DIRS += test_utils`). Candidates identified by the architecture review / user direction: `core/read_pixels.{hpp,cpp}` raw `glReadPixels` anchor (`core/read_pixels.hpp:30` — every pixel-gate test's evidence path), `utils/pixel_reader.*`, `utils/offscreen_context.*`, `render/linked_list_oit::readCapturedFragmentCount()`, `render/slice_renderer::captureCrossSection()` + `TransformFeedback` harness. Raw `gl*` stays exclusive to `REContext` (`core/re_context.cpp` is the only `glReadPixels`/`glGetBufferSubData` site; audit `gpu_api_ownership` / `no_production_readback` now allow `core|test_utils` — raw stays `core`, façade in `test_utils`). New peer lib `test_utils/` (`AUDIT_SOURCE_DIRS += test_utils`, `disposition` rules updated). `test_utils::PixelReader` calls `REContext::current().readRgba8(…)`, not a second anchor. **Constraint:** every context-setting GL call still flows through `T2 REContext` — no test helper touches raw GL.

**T** — `grep -R "glReadPixels" -- core/` == 1 hit (inside `re_context.cpp`), `grep -R "glReadPixels" -- test_utils/` == 0; suite still green via `test_utils::PixelReader`.

**G** — suite green, audit green.

## T19: `View` explicit lights field

**D** — `scene::View` and `render::View` gain `vector<Light> lights` (was implicit/absent). App: `Light { Type dir/point/spot; vec3 pos/dir; vec4 color; float intensity; … }` + `setLights()` bumping `lightsGen` (adds to `CompositeKey` per §10). RE: `ReLight` (derived uniform-ready) uploaded per view before `drawLayer` loop; empty vector = unlit (2D). Broker: `LightMapper : IMapper<Light,ReLight>` + `ViewMapper` composes `LightMapper`. Persistence: `lightsGen` participates in `ViewSynchronizer` dirty check (per-field, not whole-view dump).

**T** — two lights on one view produce analytic two-light composite distinct from single-light within `1/255` at probe; empty lights = unlit as before.

**G** — suite green, audit green.

## Definition of Done — review follow-ups (T1–T19 reordered, dependency-first)

**Loop artifacts (generic, every T):**
- [ ] `suite green N>=3` (`tools/logs/task_*.gate.log` shows 3 consecutive `ctest Passed`, `GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6`)
- [ ] `audit green` with `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils tests"` (+ `test_utils` after T18) — `tools/audit.sh` PASS incl. `disposition_scene/render` (review-gated), `broker_per_type`, `no_dump_sync`, `asset_indirection`, `ownership_raw_ptr_*`, `comment_tag_context`
- [ ] `ASan+UBSan clean` on all `re_*` libs (not just tests) + samples exit 0 under `xvfb` (FR-app.1)
- [ ] `LICENSE` beside every dataset dir (`data/meshes/LICENSE`, `data/volumes/LICENSE` enumerated) — T2 gate
- [ ] `R9` doc-map: `git diff --name-only` includes listed docs per T (review checks)

**Per-task gates (reordered T1–T19):**
- [ ] `T1` hierarchy: `ISceneObject` base + `ObjectBase<Derived>` — 15+ types open via `REGISTER_SCENE_OBJECT`, `variant<MeshObject` ==0, partitioned store `O(kind)` — suite green.
- [ ] `T2` REContext: `DrawContext→REContext` global per-GL-context `thread_local current()`, `g_cache` deleted, cross-pass spy 2→1 proves dedup — suite green.
- [ ] `T3` pair-key: `Broker::get<AppT,ReT>` pair-key `{AppT,ReT}` — wrong `ReT` returns `nullptr` not UB (typed miss) — suite green.
- [ ] `T4` draw-cache: single-writer analysis note + unify on `DrawContext` (port `LinkedListOIT` to `DrawContext&`, `g_cache` deleted) — `grep g_cache ==0`.
- [ ] `T5` header firewall: no `<glad` in any `core/*.hpp`, `core/CMakeLists` `glad` PRIVATE — minimal TU including `core/draw.hpp` builds.
- [ ] `T6` infra batch: `tests/test_helpers.*` single source `makeQuadMesh==1`, `re_tests` single binary preserved — suite green.
- [ ] `T7` owner-driven handles: volume/image `lookupVolume/Image` lazy-hash deleted, spy hash count 0 over 60-frame loop, registry slot growth 0 — suite green.
- [ ] `T8` OIT docs: per-view cost `w*h*16*32` table `640×480=152 MB, 1080p≈1GB` verified (analytic 152, ~1070) + `grep renders without OIT ==0`.
- [ ] `T9` broker polish: `aliasByApp_==0`, `weak_ptr<ViewCompositor>==0`, `clearColorGen` bumps dedicated gen, single `GenerationTracker`, `count()==6` — suite green.
- [ ] `T10` render dedup: `kScreenQuadVerts`/`LazyProgram` deduped, `inverse(uViewProj)==0`, TF per-frame alloc 0 — zero pixel drift 1/255 N>=3.
- [ ] `T11` app batch: `makeCtTransferFunction==1`, `runSample` present, `oit_sample` no hardcode `{1,2,3,4}` — smoke exits 0.
- [ ] `T12` infra/tests alt: already in T6 (kept for ordering) — suite green.
- [ ] `T13` validation: uniform `-1` checked + location cache 1, `bind(16)` fails, `ERANGE` on giant OBJ index, single `Aabb`, EGL optional — suite green.
- [ ] `T14` sanitizers: `INTERFACE re_project_sanitizers` linked by all `re_*`, `cmake --build --verbose` shows `-fsanitize` on `re_core` etc.
- [ ] `T15` NRRD pre-probe: `file_size > cap` → `BudgetExceeded` + `spdlog::warn`, no multi-GB alloc — suite green.
- [ ] `T16` collapse variant: `IRenderer::render(Scene)` + `using Scene =` variant deleted, `drawLayer` only — 4 copies boilerplate gone — suite green.
- [ ] `T17` GlfwRuntime: `glfwTerminate==1` hit inside `glfw_runtime.*` only, `window.hpp` not in `tests/` — order-independent teardown — suite green.
- [ ] `T18` TransferFunction: default ramp defined, `sample(0.5f)` defined, `toByte(1.5f)` clamped 255, `toByte(-0.2f)` clamped 0 — ASan clean.
- [ ] `T19` test_utils + View lights: `glReadPixels==1` in `re_context.cpp` only, `0` in `test_utils/`; `View.lights` explicit on both `scene::View`/`render::View`, `lightsGen` in `CompositeKey` — two-light probe within 1/255.
