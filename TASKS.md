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

## FR → T traceability matrix (V6 archived + V7 active, BLOCKER #1)

V6 archived owners (R3 regression-locked, `COMPLETED_TASKS.md:V6` — not active but preserved via `R3 regression_lock`): `T3a/T3b` + `T9` + `T11a/T11b` + `T13` owned all 21 FRs (io4+data3+vol3+core2+render6+app3) at V6 close. V7 active `T1..T10` re-verifies `1/255`/`1e-6`/`ε=1e-4`/`152 MB` via `T8` smoke + `T10` gates while `R3` keeps prior gates green; no `FR` is weakened.

Every `FR-N.M` in `docs/spec/frs.md` (21 FRs: io4+data3+vol3+core2+render6+app3, lines 15-86 + V7 `FR-render.7/8/9`/`FR-app.4` extension §4.7) maps to at least one `T`-gate asserting its explainable constant (1/255, 1e-6, ε=1e-4, 128×128×70, 1280×960/640×480, 90%/2px). V7 `FR-render.7/8/9` (CSG/Point/Line) + `FR-app.4` are owned by `V7 T3/T4/T5/T10`.

| FR | V6 archived owner + V7 active re-verifier | Analytic constant | Type |
|---|---|---|---|
| FR-io.1 | **V6 T11b** hostile OBJ `1e-6` AABB + **V7 T8** `Engine` smoke re-verifies `AABB 1e-6` via `SceneStore` | vertex/index hand-count, AABB 1e-6 | hist + V7 smoke |
| FR-io.2 | **V6 T11a** `128×128×70 ≤128³` exact + `BudgetExceeded` code 8 + **V7 T10** `Caps` tiled still 1/255 | NRRD dims exact, voxel corner exact, BudgetExceeded code 8 | hist + V7 |
| FR-io.3 | **V6 T11b** `loadImageAsset` golden `2×2` RGBA `9ccfc2ab…` `1/255` + **V7 T8** `PlaneRenderer` 1/255 smoke kept | dimensions + corner/center 1/255 | hist + V7 |
| FR-io.4 | **V6 T11a+T11b** malformed → `BudgetExceeded`/`Stale` + **V7 T8** smoke preserves typed error | error enum, no exception | hist + V7 |
| FR-data.1 | **V6 T11b** `faceNormal == cross` 1e-6 + **V7 T8** smoke | cross-product | hist + V7 |
| FR-data.2 | **V6 T11b** `AABB exact` + **V7 T8** smoke | exact bounds | hist + V7 |
| FR-data.3 | **V6 T11a** `sampleTrilinear 1e-6` + **V7 T8** smoke | trilinear 1e-6 | hist + V7 |
| FR-vol.1 | **V6 T11a** `TransferFunction 1e-6` + **V7 T8** smoke | control-point exact, ramp 1e-6 | hist + V7 |
| FR-vol.2 | **V6 T11a+ T3b** `alpha-blend 1e-6` + **V7 T10** `over()` 1/255 k-way | compositing 1e-6 | hist + V7 |
| FR-vol.3 | **V6 T11a+T3b** `ray/AABB 1e-6` + **V7 T3** `CsgStage depth 1e-6` covers | step positions 1e-6 | hist + V7 |
| FR-core.1 | **V6 T13** `GL 4.6 core` + **V7 T3** `ssboAtomics` probe keeps 4.6 | 4.6 core, no GL errors | hist + V7 |
| FR-core.2 | **V6 T13** `glibberish → ERROR: 0:7` + **V7 T3** probe preserves | ERROR:0:7 | hist + V7 |
| FR-render.1 | **V6 T3a** center 1/255 `MeshRenderer` + **V7 T8/T10** `outside A` 1/255 | 1/255 | hist + V7 |
| FR-render.2 | **V6 T3b** OIT blend 1/255 + **V7 T10** `A α0.5−B + surrounding α0.6` k-way `over()` 1/255 | 1/255 | hist + V7 |
| FR-render.3 | **V6 T3b** `isEngaged` spy + **V7 T7/T10** `isEngaged` 1 + `readResolvedCount` | alpha 1.0 / spy 1 | hist + V7 |
| FR-render.4 | **V6 T3a** `SliceRenderer ε=1e-4` + hist kept + **V7 T3** `resolved depth 1e-6` | ε=1e-4 / 1e-6 | hist + V7 |
| FR-render.5 | **V6 T3a** `PlaneRenderer 1/255` + **V7 T8** smoke 1/255 | 1/255 | hist + V7 |
| FR-render.6 | **V6 T3b** `VolumeRenderer 1/255` + **V7 T8** smoke | 1/255 | hist + V7 |
| FR-render.7 (V7 new) | **V7 T3+T10** `Cube(2)−Sphere(0.6)` hole `B` mat `1/255`, `outside A` intact, ray through hole `clearColor` `1/255` | 1/255 | V7 active |
| FR-render.8 (V7 new) | **V7 T4+T10** `Point 3D→Sphere` oracle `1/255`, `2D circle 1/255`, `worldUnits 10px` const `1/255` | 1/255 | V7 active |
| FR-render.9 (V7 new) | **V7 T5+T10** `Line 2px ≥90% within 2px 1/255`, `dash 8/4 1/255`, `worldUnits` `1/255` | 90%/2px, 1/255 | V7 active |
| FR-app.1 | **V6 T9** `RE_SAMPLE_MAX_FRAMES=20 1/255` + **V7 T8** smoke `1/255` | 1/255 | hist + V7 |
| FR-app.2 | **V6 T9** `1280×960/640×480` + **V7 T8** re-verified `1280×960/640×480` | 1280×960/640×480 | hist + V7 |
| FR-app.3 | **V6 T9** `≥90% within 2px 1/255` + **V7 T10** `Contour/Line 90%` | 90%/2px | hist + V7 |
| FR-app.4 (V7 new) | **V7 T8** `addCsg/addPoint/addLine` via `Engine` smoke `1/255` + `DepthConfig true` | 1/255 | V7 active |

V7 owns `FR-render.7/8/9` + `FR-app.4` (CSG/Point/Line/Engine facade) via `T3/T4/T5/T8/T10` `1/255`/`90%`/`1e-6`/`152 MB`; `FR-io/data/vol/core/render1-6/app1-3` remain `hist` via `R3` + re-verified by `T8/T10` smoke — no `FR` deferred without owner.

## V6 backlog — EMPTY (V6 25/25 green, archived 2026-08-30)

All 25 tasks (T1..T20 incl. splits T3a/b, T8a/b, T11a/b, T14a/b, T15a/b + T17/T18 stretch + T19/T20 hotfixes) have been completed and archived to `COMPLETED_TASKS.md` V6. Next iteration will be planned via `/loop-init`.

> **Naming:** archived backlog was `T1..T20` (consolidated engine iteration + hotfixes). Next `T1..Tn` will be assigned per new iteration.

## Definition of Done — V6 (archived)
- [x] All 25 task gates green; full suite green on a clean tree at T20 (25 tasks, T1..T20 incl. splits + stretch + hotfixes)
- [x] `suite green N>=3` where GL-touching, `audit green`, `ASan+UBSan clean`, `LICENSE` per-dir, `R9` doc-map, `R3` regression, `R4` evidence, `comment_tag_context` PASS, `install` reproduces — see `COMPLETED_TASKS.md` V6 for evidence

## V7 backlog — GPU CSG (Puxel 2-stage SSBO) + Points/Lines (10 required + 2 stretch)

> Stack: C++20, CMake >=3.24, GL 4.6 core (glad2 73db193), GLFW 3.4, GLM, Dear ImGui, GTest, spdlog, stb — all pinned via FetchContent `GIT_TAG` 40-char SHA (`tools/audit.rules:deps_pinned`). Build gate `source tools/env.sh && eval "$LOOP_BUILD_TEST_CMD"` (SPEC §8, R15). `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"`. Guardrails `tools/audit.rules` + `docs/spec/guardrails.md` §6 binding: `gpu_api_ownership forbid_outside core|\bgl[A-Z]|GL_`, `render_no_glad`, `render_no_window`, `asset_indirection`, `broker_per_type` (one `class.*Mapper` per file, grep -c ==1 at T15b), `isp_mapper_forbid`, `ownership_raw_ptr_*` `Type* /*borrow*/` + `@note lifetime:`, `evidence_analytic 1/255|1e-6`, `comment_tag_context >=120`, `no_per_target_sanitize INTERFACE re_project_sanitizers`, `engine_depth_default DepthConfig{true}` single-site, `no_production_readback forbid_outside core|glReadPixels`.

> Design decisions locked this iteration: closed manifold meshes only; CSG flat multi-subtract/multi-paint per `CsgObject` (`base:Operand{AssetRef<Mesh> mesh, mat4 operandTransform, MeshMaterialDesc material} + vector<Operand> subtractors + vector<PaintOperand>{Operand oper; bool paintInterior; float blend;} paints`) — tree `(A−B)∪C` via two `CsgObject`s on same `Layer` (layer-union free via depth, Goldfeather SOP deferred); hole interior uses `B`'s material (asymmetric `Subtract`); paint = recolor surviving base fragments where `inside(P)` (`paintInterior` true → volume interior, false → surface strip, `blend` override); point radius world default + `worldUnits` px toggle (100s points); line state-of-art = `SSBO+gl_VertexID` 6-vert view-quad strip, analytic `fwidth` AA, `Rougier mod(s,patternLen)` dash with `miterLimit 4→bevel`, `round/square` caps, `worldUnits` toggle; `Point 3D` single → `MeshRenderer` reuse (`GeometryKind::Sphere`), `PointCloud/2D` circle → `PointRenderer` impostor (`gl_FragDepth` ray sphere); `Line` own `LineRenderer` (not `ContourRenderer` GS); `CSG Stage-1 SSBO` (head R32UI + counter + node 16B `{colorU32,depth,facing,matId}` padded 16B, maxFpp [1,16] default 8, `nodeCapacity=w*h*maxFpp*16 ≤152 MB` analytic `157286400` for `640×480×16×32` reference — `Csg` `16B` gives `640×480×8×16=39321600` `37.5 MB` well under budget, max `640×480×16×16=78643200` `75 MB` still ≤152 MB) → `csg_resolve.frag` sort+classify→`csgResolved` sorted SSBO linear per-pixel + counts → `LinkedListOIT::endWithCsg` k-way merge `over()` with `Mesh/Point/Line` transparent stream. Priority `techniqueOrder [Volume,VolumeSlice,Plane,Csg,Mesh,MeshSlice,Point,Line,Contour]` `size=9` (`SceneKind::Count=9`, `Layer::Count` stays `8` — layers are stacking, kinds are dispatch) so `Csg` resolves before `Mesh` composite; `Layer::Count` remains `8` (`LAYER_0..7` dumb) per `scene/layer.hpp:21`.

> **Approach C — Puxel-filter directly, no CPU boolean (user binding 2026-08-30):** CSG via separate `CsgOitStage` capture (SSBO linked-list) then resolve (sort+classify) writing surviving sorted fragments into `csgResolved` SSBO; final `LinkedListOIT::endWithCsg` merges `csgResolved` + `Mesh/Point/Line` transparent linked-lists with `over()`. This implements Kauker 2013 Puxels / Low 2010 Fragment-Sort adapted to `LinkedListOIT` (`render/linked_list_oit.hpp:50` maxFpp clamp [1,16], `headTexture_ R32UI + nodeBuffer_+counterBuffer_` `ensureCapacity` pattern); separate stage keeps `LinkedListOIT` unchanged and makes CSG testable in isolation.

| Task | Docs updated in same commit |
|---|---|
| T1 | docs/spec/guardrails.md, docs/spec/broker.md, docs/spec/materials_lights.md (Csg/Point/Line kinds + techniqueOrder) |
| T2 | scene/objects/csg_object.hpp, scene/objects/point_object.hpp, scene/objects/line_object.hpp, scene/csg_op.hpp, scene/point_fill.hpp, scene/line_style.hpp |
| T3 | render/csg_stage.hpp, render/csg_renderer.hpp, render/shaders/csg_capture.* + csg_resolve.frag, core/caps.hpp (SSBO atomics) |
| T4 | render/point_renderer.hpp, render/shaders/point_impostor.* |
| T5 | render/line_renderer.hpp, render/shaders/line.* |
| T6 | broker/csg_object_mapper.hpp, broker/csg_stage.hpp, broker/render_stack.hpp (csgStage) |
| T7 | broker/point_object_mapper.hpp, broker/point_cloud_mapper.hpp, broker/line_object_mapper.hpp, broker/view_synchronizer.cpp |
| T8 | include/render_engine/engine.hpp (addCsg/addPoint/addLine), app/samples wiring |
| T9 | docs/render.md, docs/re_scene_inventory.md (ReCsg/RePoint/ReLine) |
| T10 | tests/t*_csg*, tests/t*_point*, tests/t*_line* + audit green (paint interior) |
| T11 | (stretch) broker/csg_tree.hpp SOP tree `(A−B)∪C` + abacaba SCS |
| T12 | (stretch) render/material/shader_table.md + perf 152 MB + N>=3 audit |

---

## T1: Spec & guardrail delta — Csg/Point/Line kinds + techniqueOrder (no code)

**D** — Update `docs/spec/goals.md` §1 (new capabilities `7 CSG` `8 Points` `9 Lines` + `FR-render.7/8/9`/`FR-app.4` in `docs/spec/frs.md` §4.7), `docs/spec/guardrails.md` §6 (new kinds `Csg,Point,Line` `SceneKind::Count=9` (`Layer::Count` stays `8` — layers are stacking, not dispatch), `techniqueOrder` `Volume,VolumeSlice,Plane,Csg,Mesh,MeshSlice,Point,Line,Contour` size `9`), `docs/spec/broker.md` §11.3 inventory (one file per mapper, `CsgOitStage` coordinator not mapper), `docs/spec/materials_lights.md` §12.2 `PointMaterialDesc{baseColor,radius,worldUnits,fill,fillParam,doubleSided}`, `LineMaterialDesc{baseColor,width,worldUnits,cap,join,dash}`, `CsgMaterialDesc{base,cap,op}` + `IMaterial` split `IColor/ILine/IVolume` (ISP). No `render/` edits; `audit.rules:gpu_api_ownership` `core|` anchor unchanged, `rhi_ownership` stays commented until T17, `broker_per_type` `ViewBridge` exempt. Evolves in one commit: docs only, `audit green`.

**T** — `grep -c "SceneKind::Csg\|SceneKind::Point\|SceneKind::Line" scene/iscene_object.hpp ==3` + `grep -c "SceneKind::Count.*9" scene/iscene_object.hpp ==1` + `grep -c "techniqueOrder" broker/render_stack.hpp ==1` + `grep -c "FR-render.7\|FR-render.8\|FR-render.9" docs/spec/frs.md >=3` + audit green.

**G** — docs green, audit green.

## T2: Scene value types — CsgObject / PointObject+PointCloudObject / LineObject+PolylineObject (GL-free, RE-free)

**D** — `scene/objects/csg_object.hpp` `class CsgObject : ObjectBase<CsgObject>` `Kind=Csg` `{id,transform,layer,priority,generation, Operand{AssetRef<data::Mesh> mesh, glm::mat4 operandTransform, MeshMaterialDesc material} base, vector<Operand> subtractors, vector<PaintOperand>{Operand oper; bool paintInterior; float blend;} paints}` (flat, tree deferred 2026-08-30 user binding: closed manifold, `B`'s material drives hole, `paintInterior` true → volume interior recolor, false → surface strip, `blend` override). `scene/objects/point_object.hpp` `PointObject{glm::vec3 position,float radius,bool worldUnits,glm::vec4 color,PointFill fill,float fillParam}` + `point_cloud_object.hpp` `PointCloudObject{vector<PointData{glm::vec3 pos,float radius,glm::vec4 color,uint32_t fillBits}> points,bool worldUnits}` (shared `worldUnits` flag, per-point fill bits). `scene/objects/line_object.hpp` `LineObject{vector<LineSegment{glm::vec3 a,b}> segments,glm::vec4 color,float width,bool worldUnits,LineCap cap,LineJoin join,float miterLimit,LineStyle style,DashPattern dash}`. All expose `ObjectBase` `id/transform/generation/layer/priority` + `REGISTER_SCENE_OBJECT` + `setLayer`/`setPriority` via `ObjectBase`. `scene/csg_op.hpp` (`enum class CsgOp{Subtract,Paint}` if needed), `scene/point_fill.hpp` (`enum class PointFill{Solid,Hollow,GridDashed}`), `scene/line_style.hpp` (`enum class LineCap{Round,Square}` `LineJoin{Miter,Bevel}` `DashPattern{len, pattern[]}`) value headers (no GL). `scene/material_desc.hpp` add `PointMaterialDesc{glm::vec4 baseColor; float radius; bool worldUnits; PointFill fill; float fillParam; bool doubleSided;}`, `LineMaterialDesc{glm::vec4 baseColor; float width; bool worldUnits; LineCap cap; LineJoin join; float miterLimit; DashPattern dash;}`, `CsgMaterialDesc{MeshMaterialDesc base; MeshMaterialDesc cap; CsgOp op;}` to `MaterialDesc variant<MeshMaterialDesc,VolumeMaterialDesc,PointMaterialDesc,LineMaterialDesc,CsgMaterialDesc>` (variant OCP, visitor overloads later — keeps `SliceMaterialDesc`/`ContourMaterialDesc` until fully landed, variant grows additively).

**T** — `SceneStore::addObject<CsgObject>(csg)` `generation` bump, `dirtyFieldsSince` `Transform|Material`, `SceneFactory::hasKind(Csg/Point/Line)==true`, `ObjectBase::clone` round-trip `EXPECT_EQ`, `MaterialDesc` variant `holds_alternative<PointMaterialDesc>` + `LineMaterialDesc`.

**G** — suite green, audit green (`ownership_raw_ptr` clean, `comment_tag_context` ≥120 per `T2` block).

## T3: Core + CSG Stage-1 SSBO — `CsgOitStage` capture→sort→filter→resolved (the Puxel core, no CPU boolean)

**D** — `render/csg_stage.hpp` `class CsgOitStage` owns `optional<Texture2D> headTexture_ (R32UI)`, `optional<ShaderStorageBuffer> nodeBuffer_, counterBuffer_, resolvedBuffer_, resolvedCount_` + `LazyProgramCache captureProgram_, resolveProgram_` + `ScreenQuad`. `ensureCapacity(w,h)` sized `w*h*maxFpp` `maxFpp=8` default clamp [1,16] like `LinkedListOIT` `render/linked_list_oit.hpp:53`, `node {uint colorU32; float depth; int facing; uint matId;}` `16B` padded → `nodeCapacity()=w*h*maxFpp*16` (`640×480×8×16=39321600` `37.5 MB` ≤152 MB, max `640×480×16×16=78643200` `75 MB` ≤`157286400` `152 MB` reference) analytic. `render/csg_renderer.hpp` `class CsgRenderer : IRenderable` stateless (`registry_, stage_`) `drawCsg(base,subtractors,paints)` `imageAtomicExchange(head, counter)` append front+back both `facing ±1`. `render/shaders/csg_capture.vert/.frag` append, `render/shaders/csg_resolve.frag` per-pixel gather→insertion sort near→far → `CSG classify` flat `A∩⋂B' + paint recolor` (subW union, baseW visibility, paintW recolor, Bback facing -1 cap emission with `B` mat, paintInterior bool). Write survivors to `resolvedBuffer` linear per-pixel sorted + `resolvedCount`. `core/caps.hpp` `ssboAtomics` probe (fallback typed error 8 `BudgetExceeded` if missing, like `LinkedListOIT` caps check). Evolves in two incremental commits inside session: (1) `CsgOitStage` + shaders + caps probe, green on `gpu_api_ownership`; (2) `CsgRenderer` integration, mid-gate `ctest` after (1).

**T** — headless `640×480` `CsgOitStage::begin→drawCsg(Cube2−Sphere0.6)→resolve` `readCapturedCount()>0` + `readResolvedCount()` per-pixel `1` where hole / `0` outside + analytic `resolved depth` `1e-6` + `nodeCapacity()==640*480*8*16` `39321600` and `<=157286400` `152 MB` gate `N>=3`, `evidence_analytic 1/255|1e-6`.

**G** — suite green N>=3, `ASan+UBSan` clean, `audit green` (`gpu_api_ownership` `core|` only, `no_production_readback` `core|` only `readResolvedCount` test façade via `REContext`).

## T4: PointRenderer — impostor billboard (`3D` delegate to MeshRenderer for single, `2D` circle own FS)

**D** — `render/point_renderer.hpp` `class PointRenderer : IRenderable` `explicit PointRenderer(registry, pipeline)` owns `LazyProgramCache impostorProgram_`, `ScreenQuad` shared, reuses `MeshRenderer` for `PointObject` single 3D lit sphere (inject `MeshRenderer* /*borrow*/` — `@note lifetime: RenderStack` co-owned, `ownership_raw_ptr` exempt via `/*borrow*/`). `drawLayer(PointScene{vector<PointInstance{vec3 pos,float radius,bool worldUnits,vec4 color,PointFill fill}}}, Camera)` instanced quad `[−1,−1]..[1,1]` expand `center→clip→ndc→viewport`, `right/up` from `Camera`, `radiusScreen = worldUnits? radius*viewport.w/pos.w/tan(fov/2) : radiusPx`, pass `mapping`, `centerWS`. `render/shaders/point_impostor.vert/.frag` `r2=dot(mapping,mapping); if(r2>1) discard; hollow/grid via fill; n=vec3(mapping,sqrt(1−r2)); pos=centerWS+n*radius; gl_FragDepth=project(pos) (3D), flat `alpha*halo` (2D `is2D()` branch `ClipPlane` present → no `gl_FragDepth` write); shade `max(dot(n,(0,0,1)),0)` headlight.

**T** — 3D `Perspective` sphere center pixel `EXPECT_NEAR(..., oracle MeshObject{GeometryKind::Sphere},1.0/255.0)` 1/255 N>=3 + 2D `ClipPlane` same points as flat circles 1/255 + `worldUnits=false` `10px` constant at `2` camera distances 1/255 + `fill=Hollow` vs `GridDashed` golden `1/255` N>=3.

**G** — suite green N>=3, audit green.

## T5: LineRenderer — `SSBO+gl_VertexID` view-quad strip, `Rougier mod(s)` dash, analytic AA

**D** — `render/line_renderer.hpp` `class LineRenderer : IRenderable` `LazyProgramCache lineProgram_`, `SSBO` `LineSegmentSSBO{a,b,color,width,s0,s1,worldUnits}` populated CPU `s` cumulative `length(viewport*(b−a))`. `drawLayer(LineScene{vector<LineInstance>},Camera)` `glDrawArrays(6*N,0)` `6` virtual verts/segment `a±n*wA,b±n*wB` `n=perp(viewport*(b−a))` view-space. Pass `segmentCoord,s`. `render/shaders/line.vert/.frag` `distToStroke`, `inDash=step(mod(s+offset,patternLen),dashLen)`, `alpha=smoothstep(fwidth) * inDash`, `discard` gap, `fragColor=vec4(color.rgb,color.a*alpha)` (premul for LL). Joins `miterLimit 4→bevel` via `prev/next` at polyline nodes, caps `round/square`. `worldUnits` `w` scaled like points.

**T** — `640×480` solid red `2px` horizontal across black `≥90%` of geometric `±width/2` band within `1/255` of red (mirrors `contour` gate `t20_contour_test.cpp` `≥90% within 2px`) N>=3 + dashed `dash 8 gap 4` known pixel `1/255` + `worldUnits=true` attenuates with distance `1/255`.

**G** — suite green N>=3, audit green.

## T6: Broker Csg wiring — `CsgObjectMapper` + `CsgOitStage` + `RenderStack` + `Broker` registry

**D** — `broker/csg_object_mapper.hpp:.cpp` `class CsgObjectMapper : CachedMapperBase<CsgObject,ReCsgObject>` override `map(const CsgObject&,TranslateContext) → ReCsgObject{AssetHandle baseHandle, vector<AssetHandle> subHandles, vector<glm::mat4> subTransforms, vector<AssetHandle> paintHandles, vector<glm::mat4> paintTransforms, vector<float> paintBlends, vector<bool> paintInteriorFlags, glm::mat4 model, worldBounds}` via `AssetRegistry` resolve (dedup by `ContentHash`, not pointer; per-operand `operandTransform` preserved), `isCacheHit` includes `operandHashes Σ hash(AssetId+gen+contentHash+operandTransform+matHash+paintBlend+paintInterior)`. `broker/csg_stage.hpp` thin façade over `render::CsgOitStage` for `ViewCompositor`. `broker/render_stack.hpp:53` add `shared_ptr<CsgRenderer> csg; shared_ptr<CsgOitStage> csgStage;` `techniqueOrder` `Volume,VolumeSlice,Plane,Csg,Mesh,MeshSlice,Point,Line,Contour` size `9` (`Csg` index 3, `Mesh` 4, `SceneKind::Count=9`). `broker/broker.hpp` `registerMapper<CsgObject,ReCsgObject>` pair-key `type_index`.

**T** — `Broker::get<CsgObject,ReCsgObject>()` returns registry, `mapCached` hit spy `2→1` on `generation` unchanged + `operand gen` bump miss, `ViewSynchronizer` order `grep -c "stable_sort" broker/view_synchronizer.cpp` still layer+order+priority+insertionIdx.

**G** — suite green, audit green `broker_per_type` `grep -c "class.*Mapper" broker/*_mapper.hpp ==1` per-file, `isp_mapper_forbid`.

## T7: Broker Point/Line wiring — `Point/Line` mappers + `ViewSynchronizer` translate

**D** — `broker/point_object_mapper.hpp` `ICachedMapper<PointObject,RePointObject>` + `point_cloud_mapper.hpp` + `line_object_mapper.hpp` (each one class per file). `broker/view_synchronizer.cpp:70` `ItemTranslator::translate` add `if(k==SceneKind::Point)… else if(k==PointCloud)… else if(k==Line)…` mirroring `k==Mesh` pattern `broker/view_synchronizer.cpp:80` — `broker_->getByKind(k)` alias or `getMutable<MapperT>`, `mapCached`, then `if(tr) tout.push_back else rv->addItem(scene,stack->point/line)`. `RenderStack` add `point,line`. `AppContext` `Params{enableOIT,enableCsg,enablePoints,enableLines}` wiring (composition root `broker/app_context.hpp`).

**T** — `ViewBridge::sync` with `PointCloud` (10 points) + `Line` (5 segments) `isEngaged` spy `≥1` when any `alpha<1`, `layerOrderHash` re-translate on `priority` change.

**G** — suite green, audit green, `acl_app_render forbid_inside app|#include.*render/` still 0, `test_window_forbid`.

## T8: Engine facade + samples — `Engine::addCsg/addPoint/addLine` + wiring through `IViewBridge`

**D** — `include/render_engine/engine.hpp` `DepthConfig{true}` single-site preserved `engine_depth_default require_grep DepthConfig\{true` `tools/audit.rules:90` — add `ObjectId addCsg(CsgDesc{AssetRef<Mesh> base, vector<Operand> subtractors, vector<PaintOperand> paints})`, `addPoint/addPointCloud(PointDesc)`, `addLine/addPolyline(LineDesc)` → `SceneStore::addObject` + `ViewStore::setItemIds`. `app/sample_harness` no `render/` include (`acl_app_render`). Update `app/mpr_slice.hpp` slice count still `≤100` `==98` `audit.sh wc -l` `no_sample_bloat`.

**T** — `Engine` headless `renderOffscreen(640,480)` smoke `1/255` per new kind: `addCsg(Cube−Sphere)` hole `1/255`, `addPointCloud(10)` 1/255 vs `addLine(2px solid)` 1/255, `Engine` `DepthConfig` still `1` via `grep -c "DepthConfig\{true" include/render_engine/engine.hpp ==1` `N>=3`.

**G** — suite green N>=3, audit green.

## T9: RE-minimal inventory — `render/re_scene/` ReCsg/RePoint/ReLine + shader table

**D** — Produce `docs/re_scene_inventory.md` binding table per `docs/spec/materials_lights.md:166` `Re*` inventory (per-field `{derived|uniform-ready|handle}`) for `ReCsgObject{AssetHandle base, vector<AssetHandle> subs/paints, mat4 model, worldBounds}`, `RePointObject{vec3 pos,float radius,vec4 color,PointFill}`, `ReLineObject{vec3 a,b,vec4 color,float width,DashPattern}`. Move to `render/re_scene/*.hpp` — never verbatim `data::Mesh::positions` `asset_indirection forbid_grep`. Add `render/material/shader_table.md` `ReMaterial→ShaderProgram` branch table (`Phong/PBR/Point/Line/Csg→ mesh_opaque/impostor/line/csg_resolve`).

**T** — `grep -R "data::Mesh::positions" render/re_scene/ ==0` + `docs/re_scene_inventory.md` 9 tables × per-field rationale `grep -c "derived\|uniform-ready\|handle"` analytic.

**G** — audit green, docs green.

## T10: Gates — hole `1/255` + transparent CSG merge + paint interior + `152 MB` + `N>=3` (required)

**D** — *No new code beyond tests*; `tests/t*_csg_stage_test.cpp` already landed. This task only hardens gates: `Cube(2)−Sphere(0.6)` centered `640×480` hole center pixel matches `Sphere` `B` mat within `1/255` (`EXPECT_NEAR`), `outside A` intact, ray through hole sees `clearColor` `1/255`; `Transparent(A α0.5)−B (B mat) + surrounding Mesh α0.6 behind` all visible via `LinkedListOIT::endWithCsg` k-way merge `1/255` (analytic `over()` not `>0`), `isEngaged()=true` spy `1`; `paintInterior=true` interior base fragment `1/255` paint vs `false` surface strip only `1/255`; `nodeCapacity()==640*480*8*16 ≤157286400` `152 MB` exact; `Contour/Line` `≥90% within 2px` `1/255` kept via `ReView` not CPU.

**T** — single gate file asserts `grep -c "1/255" tests/t*_csg* >=4` + `grep -c "1e-6" >=1` + `grep -c "BudgetExceeded" >=1` + `grep -c "152 MB" >=1` + suite `N>=3` consecutive green.

**G** — suite green N>=3, `ASan+UBSan` clean, audit green, `R4 evidence_rule` PASS.

## T11: (stretch) CSG tree SOP — `(A−B)∪C` + `abacaba` SCS hardening

**D** — Extend `broker/csg_tree.hpp` optional `struct Node{Op op; variant<Mesh,Node> left,right}` for `(A−B)∪C` only if layer-union insufficient (SOP `Σ Pi` `Stewart 1998` + `abacaba` SCS `n=3/4` only when two `CsgObject`s co-located fail gate). Keep `CsgObject` flat as default; tree is additive file, no edit to `CsgObjectMapper` (OCP via `Broker::registerMapper<CsgTreeObject>`).

**T** — `two CsgObject`s on same layer `(A−B)∪C` union pixel `1/255` + tree `(A∩B')∪(C∩D')` `1/255` N>=3.

**G** — stretch green when activated.

## T12: (stretch) RHI `IRHIContext::capabilities()` + perf mirror — `core/rhi/gl/` `T17` hardening

**D** — Deferred EOL `rhi_ownership forbid_outside core/rhi/gl|\bgl` (commented `tools/audit.rules:54` until `T17`) — not required for looping. Keep `core|` anchor `gpu_api_ownership`.

**T** — audit `rhi_ownership` still commented, `core/rhi/gl/` absent, `core|` `gl*` PASS.

**G** — (stretch) audit green when activated.

## Definition of Done — V7

- [ ] All 10 required gates T1..T10 green; full suite green `N>=3` where GL-touching, `audit green` with `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` via `source tools/env.sh` (R15 companion exact `test "$AUDIT_SOURCE_DIRS" = "io ... tests" && test -n "$LOOP_BUILD_TEST_CMD"`).
- [ ] `ASan+UBSan` clean, `-Werror` clean, `LICENSE` per-dir `data/meshes/LICENSE` + `data/volumes/LICENSE`, `R9` doc-map above, `R3` regression never weakened, `R4` evidence `1/255|1e-6|BudgetExceeded|152 MB` per-task `grep -c` floors + `evidence_analytic` anchor, `comment_tag_context` ≥120 PASS.
- [ ] `CsgObject` flat `{base, subtractors[], paints[]}` Puxel 2-stage SSBO `Cube(2)−Sphere(0.6)` hole `B` mat `1/255`, `transparent(A α0.5)−B + surrounding α0.6` k-way merge `1/255`, `paintInterior` true `1/255` vs false strip `1/255`, `Point 3D` vs `Sphere` mesh oracle `1/255` + `2D` circle `1/255` + `worldUnits` `10px` const `1/255`, `Line 2px` `≥90% within 2px` `1/255` + `dash 8/4` `1/255`.

## FR → T traceability delta (V7 re-verifies `1/255`/`1e-6` on green tree — full 25 FRs per §22 above)

| FR | V7 T owner | Analytic constant |
|---|---|---|
| FR-io.1..io.4 / FR-data.1..3 / FR-vol.1..3 | hist V6 + V7 T8 smoke `1/255`/`1e-6`/`BudgetExceeded` | hist `T11a/b` `1e-6`/`BudgetExceeded` re-verified via `T8` `Engine` headless `renderOffscreen` smoke |
| FR-core.1/2 | hist V6 T13 + V7 T3 `ssboAtomics` probe | 4.6 core / `ERROR: 0:7` |
| FR-render.1 (opaque center 1/255) | T8 `Engine` smoke + T10 `outside A` | 1/255 |
| FR-render.2 (depth-ordered blend) | T10 `A α0.5−B + surrounding α0.6` k-way `over()` | 1/255 |
| FR-render.3 (OIT spy) | T7/T10 `isEngaged` flip `1` + `readResolvedCount` | spy 1 |
| FR-render.4 (ε=1e-4) | hist `SliceRenderer` kept, `CsgStage` depth `1e-6` covers | 1e-6 |
| FR-render.5/6 | hist `Plane/Volume` `1/255` via `T8` smoke re-verified | 1/255 |
| FR-render.7 (V7 CSG hole) | T3+T10 `Cube(2)−Sphere(0.6)` hole `B` mat `1/255` | 1/255 |
| FR-render.8 (V7 Points) | T4+T10 `Point 3D→Sphere` `1/255`, `2D 1/255`, `worldUnits 10px` | 1/255 |
| FR-render.9 (V7 Lines) | T5+T10 `Line 2px ≥90% 1/255`, `dash 8/4 1/255` | 90%/2px |
| FR-app.1/2/3 | T8 `RE_SAMPLE_MAX_FRAMES=20` + `1280×960 / 640×480 / 90% 2px` re-verified | 1280×960/90% |
| FR-app.4 (V7 Engine facade) | T8 `addCsg/addPoint/addLine` `1/255` + `DepthConfig true` `==1` | 1/255 |
| NFR 152 MB | T3/T10 `nodeCapacity() ≤157286400` exact (`39321600` at `640×480×8×16`) | 152 MB |


