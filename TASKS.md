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

## V7 backlog — GPU CSG (Puxel 2-stage SSBO) + Points/Lines (10 required + 2 stretch)

> Stack: C++20, CMake >=3.24, GL 4.6 core (glad2 73db193), GLFW 3.4, GLM, Dear ImGui, GTest, spdlog, stb — all pinned via FetchContent `GIT_TAG` 40-char SHA (`tools/audit.rules:deps_pinned`). Build gate `source tools/env.sh && eval "$LOOP_BUILD_TEST_CMD"` (SPEC §8, R15). `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"`. Guardrails `tools/audit.rules` + `docs/spec/guardrails.md` §6 binding: `gpu_api_ownership forbid_outside core|\bgl[A-Z]|GL_`, `render_no_glad`, `render_no_window`, `asset_indirection`, `broker_per_type` (one `class.*Mapper` per file, grep -c ==1 at T15b), `isp_mapper_forbid`, `ownership_raw_ptr_*` `Type* /*borrow*/` + `@note lifetime:`, `evidence_analytic 1/255|1e-6`, `comment_tag_context >=120`, `no_per_target_sanitize INTERFACE re_project_sanitizers`, `engine_depth_default DepthConfig{true}` single-site, `no_production_readback forbid_outside core|glReadPixels`.

> Design decisions locked this iteration: closed manifold meshes only; CSG flat multi-subtract/multi-paint per `CsgObject` (`base + vector<Operand> subtractors + vector<PaintOperand> paints{mesh,paintInterior,bool}`) — tree `(A−B)∪C` via two `CsgObject`s on same `Layer` (layer-union free via depth, Goldfeather SOP deferred); hole interior uses `B`'s material (asymmetric `Subtract`); paint = recolor surviving base fragments where `inside(P)` (`paintInterior` true → volume interior, false → surface strip, `blend` override); point radius world default + `worldUnits` px toggle (100s points); line state-of-art = `SSBO+gl_VertexID` 6-vert view-quad strip, analytic `fwidth` AA, `Rougier mod(s,patternLen)` dash with `miterLimit 4→bevel`, `round/square` caps, `worldUnits` toggle; `Point 3D` single → `MeshRenderer` reuse (`GeometryKind::Sphere`), `PointCloud/2D` circle → `PointRenderer` impostor (`gl_FragDepth` ray sphere); `Line` own `LineRenderer` (not `ContourRenderer` GS); `CSG Stage-1 SSBO` (head R32UI + counter + node 16B `{colorU32,depth,facing,matId}` maxFpp [1,16] default 8, `nodeCapacity=w*h*maxFpp ≤152 MB` analytic `157286400`) → `csg_resolve.frag` sort+classify→`csgResolved` sorted SSBO linear per-pixel + counts → `LinkedListOIT::endWithCsg` k-way merge `over()` with `Mesh/Point/Line` transparent stream. Priority `techniqueOrder [Volume,VolumeSlice,Plane,Csg,Mesh,MeshSlice,Point,Line,Contour]` so `Csg` resolves before `Mesh` composite.

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

**D** — Update `docs/spec/guardrails.md` §6 (new kinds `Csg,Point,Line` `Count=9`, `techniqueOrder` `Volume,VolumeSlice,Plane,Csg,Mesh,MeshSlice,Point,Line,Contour`), `docs/spec/broker.md` §11.3 inventory (one file per mapper, `CsgOitStage` coordinator not mapper), `docs/spec/materials_lights.md` §12.2 `PointMaterialDesc{baseColor,radius,worldUnits,fill,fillParam,doubleSided}`, `LineMaterialDesc{baseColor,width,worldUnits,cap,join,dash}`, `CsgMaterialDesc{base,cap,op}` + `IMaterial` split `IColor/ILine/IVolume` (ISP). No `render/` edits; `audit.rules:gpu_api_ownership` `core|` anchor unchanged, `rhi_ownership` stays commented until T17, `broker_per_type` `ViewBridge` exempt.

**T** — `grep -c "SceneKind::Csg\|SceneKind::Point\|SceneKind::Line" scene/iscene_object.hpp ==3` + `grep -c "techniqueOrder" broker/render_stack.hpp ==1` + audit green.

**G** — docs green, audit green.

## T2: Scene value types — CsgObject / PointObject+PointCloudObject / LineObject+PolylineObject (GL-free, RE-free)

**D** — `scene/objects/csg_object.hpp` `class CsgObject : ObjectBase<CsgObject>` `Kind=Csg` `{id,transform,layer,priority,generation, Operand{AssetRef<Mesh> mesh, mat4 operandTransform, MeshMaterialDesc material} base, vector<Operand> subtractors, vector<PaintOperand>{Operand mesh; bool paintInterior; float blend;} paints}` (flat, tree deferred 2026-08-30 user binding: closed manifold, `B`'s material drives hole, `paintInterior` bool). `scene/objects/point_object.hpp` `PointObject{vec3 position,float radius,bool worldUnits,vec4 color,PointFill fill,float fillParam}` + `point_cloud_object.hpp` `PointCloudObject{vector<PointData{vec3 pos,float radius,vec4 color,uint32_t fillBits}> points,bool worldUnits}`. `scene/objects/line_object.hpp` `LineObject{vector<LineSegment{a,b}> segments,vec4 color,float width,bool worldUnits,LineCap cap,LineJoin join,float miterLimit,LineStyle style,DashPattern dash}`. All expose `ObjectBase` `id/transform/generation/layer/priority` + `REGISTER_SCENE_OBJECT`. `scene/csg_op.hpp`, `scene/point_fill.hpp`, `scene/line_style.hpp` value headers (no GL). `scene/material_desc.hpp` add `PointMaterialDesc/LineMaterialDesc/CsgMaterialDesc` to `MaterialDesc variant<Mesh,Volume,Point,Line,Csg>` (variant OCP, visitor overloads later).

**T** — `SceneStore::addObject<CsgObject>(csg)` `generation` bump, `dirtyFieldsSince` `Transform|Material`, `SceneFactory::hasKind(Csg/Point/Line)==true`, `ObjectBase::clone` round-trip `EXPECT_EQ`.

**G** — suite green, audit green (`ownership_raw_ptr` clean, `comment_tag_context` ≥120 per `T2` block).

## T3: Core + CSG Stage-1 SSBO — `CsgOitStage` capture→sort→filter→resolved (the Puxel core, no CPU boolean)

**D** — `render/csg_stage.hpp` `class CsgOitStage` owns `optional<Texture2D> headTexture_ (R32UI)`, `optional<ShaderStorageBuffer> nodeBuffer_, counterBuffer_, resolvedBuffer_, resolvedCount_` + `LazyProgramCache captureProgram_, resolveProgram_` + `ScreenQuad`. `ensureCapacity(w,h)` sized `w*h*maxFpp` `maxFpp=8` default clamp [1,16] like `LinkedListOIT` `render/linked_list_oit.hpp:53`, `node {uint colorU32; float depth; int facing; uint matId;}` `16B` → `nodeCapacity()<=152 MB` analytic. `render/csg_renderer.hpp` `class CsgRenderer : IRenderable` stateless (`registry_, stage_`) `drawCsg(base,subtractors,paints)` `imageAtomicExchange(head, counter)` append front+back both `facing ±1`. `render/shaders/csg_capture.vert/.frag` append, `render/shaders/csg_resolve.frag` per-pixel gather→insertion sort near→far → `CSG classify` flat `A∩⋂B' + paint recolor` (subW union, baseW visibility, paintW recolor, Bback facing -1 cap emission with `B` mat, paintInterior bool). Write survivors to `resolvedBuffer` linear per-pixel sorted + `resolvedCount`. `core/caps.hpp` `ssboAtomics` probe (fallback typed error 8 `BudgetExceeded` if missing, like `LinkedListOIT` caps check).

**T** — headless `640×480` `CsgOitStage::begin→drawCsg(Cube2−Sphere0.6)→resolve` `readCapturedCount()>0` + `readResolvedCount()` per-pixel `1` where hole / `0` outside + analytic `resolved depth` `1e-6` + `nodeCapacity()<=157286400` `152 MB` gate `N>=3`, `evidence_analytic 1/255|1e-6`.

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

**D** — `broker/csg_object_mapper.hpp:.cpp` `class CsgObjectMapper : CachedMapperBase<CsgObject,ReCsgObject>` override `map(const CsgObject&,TranslateContext) → ReCsgObject{AssetHandle baseHandle, vector<AssetHandle> subHandles, vector<AssetHandle> paintHandles, mat4 model}` via `AssetRegistry` resolve (dedup by `ContentHash`, not pointer), `isCacheHit` includes `operandHashes Σ hash(AssetId+gen+contentHash+transform+matHash)`. `broker/csg_stage.hpp` thin façade over `render::CsgOitStage` for `ViewCompositor`. `broker/render_stack.hpp:53` add `shared_ptr<CsgRenderer> csg; shared_ptr<CsgOitStage> csgStage;` `techniqueOrder` `Volume,VolumeSlice,Plane,Csg,Mesh,MeshSlice,Point,Line,Contour` (`Csg` index 3, `Mesh` 4). `broker/broker.hpp` `registerMapper<CsgObject,ReCsgObject>` pair-key `type_index`.

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

## FR → T traceability delta (V7 re-verifies `1/255`/`1e-6` on green tree)

| FR | V7 T owner | Analytic constant |
|---|---|---|
| FR-render.1 (opaque center 1/255) | T8 `Engine` smoke + T10 `outside A` | 1/255 |
| FR-render.2 (depth-ordered blend) | T10 `A α0.5−B + surrounding α0.6` k-way `over()` | 1/255 |
| FR-render.3 (OIT spy) | T7/T10 `isEngaged` flip `1` + `readResolvedCount` | spy 1 |
| FR-render.4 (ε=1e-4) | hist `SliceRenderer` kept, `CsgStage` depth `1e-6` covers | 1e-6 |
| FR-render.5/6 | hist `Plane/Volume` `1/255` via `T8` smoke re-verified | 1/255 |
| FR-app.1/2/3 | T8 `RE_SAMPLE_MAX_FRAMES=20` + `1280×960 / 640×480 / 90% 2px` re-verified | 1280×960/90% |
| NFR 152 MB | T3/T10 `nodeCapacity() ≤157286400` exact | 152 MB |


