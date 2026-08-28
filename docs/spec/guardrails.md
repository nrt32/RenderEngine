# SPEC §6 — Guardrails / rules

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §6" mean this file.

## 6. Guardrails / rules (hard, enforced by tools/audit.rules + AGENTS.md)

- **Dependency lock** — every third-party dep pinned via FetchContent `GIT_TAG`
  (GLFW 3.4, glad2 v2.0.8, GLM 1.0.1, ImGui v1.92.9, GoogleTest v1.15.2,
  spdlog v1.14.1, stb pinned commit `2c980bb59875b0d32144a71867fbdebb2f77cd20`, **nlohmann/json 3.11.3 `v3.11.3` (commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`) — MIT**). A `GIT_TAG` must be a **release tag or
  commit SHA, never a branch name** (the reviewer verifies this). No unpinned
  fetches, no vendored binaries. *Audit: `deps_pinned` (+ `deps_pinned_no_branch`).*
- **GL ownership & RHI (EOL, web-verified)** — raw `glXxx(...)` calls only under `core/rhi/gl/` (RAII GL objects + thin `core::Draw` façade over `IRHIContext` with `IRHIContext{createTexture2D/3D,createFramebuffer,createBuffer,createShader,blit,memoryBarrier,capabilities}` + `IRHIShaderDesc{stages,SPIRV|GLSL,defines}` — Qt QRhi/O3DE/Adept RHI pattern); `render/`, `app/`, `tests/`, `broker/`, `scene/` use `core/` `IRHI*` wrappers (broker forwards through `render/` helpers, never calls `gl*` directly); never in `io/`, `data/`, `volume/`, `scene/`. The stable `core/rhi/{irhi_context.hpp,irhi_framebuffer.hpp,irhi_texture.hpp,irhi_buffer.hpp,irhi_shader.hpp}` abstraction is the DIP-owned policy (Oleksii Tym "policy owns interface") — `core/rhi/gl/` and future `core/rhi/vulkan/` are interchangeable implementations (one audit anchor, OCP for graphics API swap; new RHI = new `IRHIContext` impl, zero edits to `render`/`broker`/`app` per Adept command-list RHI). *Audit: `gpu_api_ownership` (`forbid_outside core|` transitional until **(stretch) RHI** lands (`core/rhi/gl/` appears), then tightens to `core/rhi/gl|` — see audit.rules transitional note + `guardrails.md` §6) + `rhi_ownership` (future hard anchor `# rhi_ownership` — **activate when `core/rhi/gl/` appears; `TASKS.md` stretch RHI G defines uncommenting `rhi_ownership` + enforcing `require_only core/rhi/gl|` for IRHI includes; until then `core|` anchor stays**) + `require_only core/rhi/gl|` for IRHI includes (stretch deferred).* Raw `gl*` in `core/` outside `core/rhi/gl/` also fails after migration; `core::loadCoreGl`/`core::readRgba8` anchors remain as `core/rhi/gl/` facades.
- **Disposition / layer isolation (DIP)** — `scene/` is RE-free and GL-free
   (may include `data/`+`volume/`+`glm`+`data/result` only); `render/` never includes
   `scene/`; `broker/` is the **only** library that may include **both**
   `scene/` and `render/` (`app/` includes `scene/`+`broker/` but never
   `render/` directly, and depends on `IViewBridge` abstraction — DIP — never concrete `ViewBridge`). No `render/types.hpp` include inside `scene/` headers. `app → IViewBridge`, `broker → IMapper/TranslateContext` (policy owns abstraction — DIP layers). *Audit: `disposition_scene` / `disposition_render` (`forbid_inside` mechanical — `scene|#include.*render/` and `render|#include.*scene/`); `scene` → `core/` GL leak additionally forbidden by `gpu_api_ownership` (`forbid_outside core|` raw `gl*`) + review gate (transitional — until V3.2a `core/rhi/gl/` lands, `scene/` may not include `core/` GL headers at all).* 
- **Per-type mapper files (`broker/`) — SRP/OCP/ISP (web-verified)** — the `broker/` mediation is heavily abstracted: one `IMapper<AppT,ReT>` (pure `map`) plus `ICachedMapper<AppT,ReT> : IMapper` (cached `mapCached`/`invalidate`) segregation per ISP (no fat `IMultiFunction` printer — code-note-vr ISP; `PlaneMapper` implements only `IMapper`, not `invalidate` — ISP `Worker`→`Workable`/`Feedable` per Baeldung/NDepend) and **one file per translator/mapper** (`camera_mapper.*`, `plane_mapper.*`, `material/mesh_object/volume_object/*_mapper.*`, `view_synchronizer.*`, `view_compositor.*`, `view_bridge.*` etc. — `ViewBridge` itself is a SRP-split coordinator over `ViewSynchronizer`+`ViewCompositor` per StackOverflow Facade SRP tension + ICS SRP "one actor"). `TranslateContext` is ISP-segregated `ViewContext` + `optional<VolumeContext>` (not God `ReView*` — ISP `Worker` segregation). A `broker/` god `Translator` that holds every `translate` in one `.hpp` is forbidden (God Object anti-pattern dilankam 2024). Adding a new `AppT`/`Material`/`Light` subtype = one new `*Mapper` file + one `Broker::registerMapper` call + one variant visitor overload (bounded; Here Be Braces variant vs virtuals), zero edits to existing concrete mapper files or `ViewMapper` (OCP via `type_index` registry — Meyer PV, NDepend; variant trade-off documented §12). `Broker` holds mapper *registry* `type_index→IMapper`, `ViewSynchronizer` holds generation *cache* `CompositeKey→ReView` (SRP split, not God Broker). *Audit: `broker_per_type` (at most one `class *Mapper` per file; `ViewSynchronizer`/`ViewCompositor` not mappers) + `isp_mapper_forbid` (IMapper must not expose `mapCached`) + `disposition_scene/render` (broker is only lib that may include both `scene/`+`render/` — ACL, not DIP violation).* Variant `MaterialDesc`/`LightDesc` closed type set (4/3) documented as intentional OCP trade-off (open for operations via visitor, closed for types — Here Be Braces).
- **Persistence — never `id`-only or size-dump sync (EOL cache-key versioning).** `broker/` persistence is content-addressed `CompositeKey{Version,LayoutId,Id,Type,Generation,ContentHash}` hierarchical `Version:LayoutId:Type:Hash` with SHA-256 at load time (Software Patterns Lexicon + System Overflow + Dev Genius version-your-cache-keys 2025-12-25; SHA-256 hash of canonicalized stable bytes, not pointers — per-field gen+hash §10.4). `Version` schema bump invalidates entire cache; `LayoutSpec{row,col,span,weight}` relative → `Layout::resolve(framebufferSize,contentScale)` absolute physical pixels (HiDPI `glfwGetFramebufferSize` + `glfwGetWindowContentScale` — web.dev high-dpi + GLFW #1857 vispy #99). A camera orbit dirties only `CameraMapper` (per-field `viewGen` vs `materialGen` — SRP/ISP per Clean Architecture Ch.7 + ICS SRP), not whole view; `ReView`/`ViewTarget` physical rect hash includes DPR without extra `dpr` field. Any direct `if (rect.w != last.w) recreateAll` or `if (id==lastId) reuseAll` pattern is forbidden — use `generation/contentHash` cache helpers via `IDirtyTracker` + `IJobExecutor` (DIP, OCP for threading). *Audit: `no_dump_sync` (forbid `recreateAll`/`dumpAll`) + `Version` prefix required in CompositeKey (cache-key-design audit floor).* Generation `uint64_t` wrap via `!=` equality (EOL), per-field split, hash canonicalization, HiDPI physical pixels.
- **RE-minimal types** — `render/` keeps only what it can directly use
  (SPEC §12.4): converted `ClipPlane`/`ReMaterial*`/`ReLight[]`/`Texture3D*`/
  `worldBounds`, not verbatim `scene::MaterialDesc`. Verbose `app/` types stored
  verbatim inside `render/re_scene/` are rejected in review.
- **Forbidden patterns** — legacy fixed-function GL anywhere
  (`no_legacy_api`); hard-coded secrets anywhere (`no_secrets`); readback raw
  calls only under `core/` (test-consumed) (`no_production_readback`); no raw
  `printf`/`std::cout` for diagnostics —
  use spdlog (`no_raw_diagnostics`); **broker app-reach rule** — app code must
  not hold a `broker/IMapper` handle directly (must go through `ViewBridge`).
- **Evidence rule + regression lock** (always keep, generic) — every gate
  produces verifiable evidence; a failing gate blocks the task; established
  behaviors must not regress without explicit approval.
- **Asset/data licensing** — only clearly-licensed free data (CC0 / CC-BY /
  CC-BY-SA / public domain) for meshes and volumes; a LICENSE file committed
  beside every dataset; no unlicensed redistribution. *Audit: `assets_licensed`
  (grep is only a floor — the T2 gate enforces one LICENSE per dataset dir).*
- **Asset persistence (new)** — `data` assets are **not** copied into
  `render/` or `scene/` disposition objects — only referenced; `render` holds
  GPU handles (`AssetHandle`, `core::Texture3D`) keyed by
  `(AssetId/type/contentHash, scope, refCount)` per SPEC §7/§10, not by
  pointer-identity alone (see SPEC §7 addendum research). *Audit:
  `asset_indirection` (forbid direct `data::Mesh` copy into `render/re_scene/`).*
- **Bridge completeness (Sr. review 2026-08-23)** — every scene kind routed
  through `broker/` produces a real layer or a typed error; no-op/placeholder
  renderables in `ViewSynchronizer::sync` (a scene kind silently vanishing
  from the frame) are forbidden. *Audit: `no_noop_broker` (`forbid_inside broker|Noop` — mechanical `Noop==0`) + review gate.*
- **Single internal implementation per renderer (Sr. review)** — pass
  prologue, shared quad geometry, `geometryFor`, and the content-hash helper
  each have exactly one definition; `<glad/gl.h>` never appears under
  `render/` (GL constants via core/ wrappers). *Audit:
  `render_no_glad` (`forbid_inside render|#include.*glad` — mechanical, replaces `render_no_glad_include`).*
- **Error codes carry their domain (Sr. review)** — numeric error ranges may
  repeat across loaders/renderers/broker; consumers disambiguate via the
  domain tag on `data::Error`, never by string-parsing messages. Failed
  `Result` dereference asserts in debug builds. *Review gate; landed T22
  (`data::ErrorDomain`, io/ loader stamping, debug-trap on failed
  dereference).*
- **Ownership discipline (T13, user mandate 2026-08-23)** — no raw pointers
  where ownership/lifetime matters. The ownership tool ladder:
  `unique_ptr` for sole owners (Broker's mappers, ReView's IRenderable items),
  shared handles for wiring whose participants must cross-reference each other
  (`ViewBridge` holds `shared_ptr` synchronizer/compositor precisely so the
  synchronizer can hold a real `weak_ptr` OBSERVER of its compositor — the
  textbook mutual-back-pointer fix; the bridge is the composition root and the
  only external owner), generational handles
  for GPU resources (`AssetHandle` into the shared registry — matches the
  industry-standard handle-based model; atomic refcount churn per frame and
  post-teardown zombie resources are documented pitfalls of
  smart-pointer-everywhere GPU designs), `shared_ptr` ONLY for genuinely
  shared CPU-side assets across layers (immutable `data::Mesh` /
  `VolumeDataset` / `Image` co-owned by scene objects, stores and RE
  instances via `scene::AssetRef<T>`), `std::weak_ptr` for observers (GPU
  texture caches keyed by weak asset observers so a destroyed asset's cache
  key expires with it; mutual wiring back-pointers such as
  ViewSynchronizer→ViewCompositor). Raw pointers survive only as **marked
  borrows** — structurally scope-bounded (call-scoped destination
  framebuffer, store-getter storage borrow) — written `Type* /*borrow*/
  name` with a Doxygen `@note lifetime:` tag naming its owner. Renderer
  constructor injection takes shared handles and validates them per draw
  (null registry → typed error code 4), so sample member declaration order
  can never silently break initialization — the worst case is a loud typed
  error. *Audit: `ownership_raw_ptr_scene` / `ownership_raw_ptr_broker` /
  `ownership_raw_ptr_app` (`forbid_inside`, unmarked raw-pointer
  declarations fail; the marked-borrow sites are the reviewable allowlist).*
  Notation: NAMING_CONVENTIONS §8b.
- **Build hygiene** — warnings-as-errors, no warning-suppression flags/pragmas
  (generic built-in checks).
- **Drift guards — sample bloat + object duplicate (T14, SPEC §6, no_sample_bloat / no_object_duplicate)** — the 11 byte-identical `scene/objects/*.hpp` headers collapsed in T5 to 6 technique kinds (`Mesh, MeshSlice, Volume, VolumeSlice, Plane, Contour`) via `MeshObject{GeometryKind}` and the 160-line `app/mesh_sample.cpp` trimmed in T7 via `Engine` facade + `SceneViewBuilder` must not re-bloat as new techniques are added. The audit enforces the secondary caps via `wc` in `tools/audit.sh` (`mesh_sample.cpp ≤80`, `mpr_slice.hpp ≤100` — the waist gauge when `mpr_sample.cpp` alone cannot reach 80) while the T14 gate asserts the exact committed sizes `mesh_sample.cpp ==42` and `mpr_slice.hpp ==98` (not `≤` caps) plus `grep -c "class.*Object.*ObjectBase" scene/objects/*.hpp ==6` (exact 6, not `≤6`) — the precise 1/255 ordering and mask are already verified in the T8 layer gate. The duplicate guard also forbids `>10%` identical lines between any two `scene/objects/*.hpp` headers via `diff` (identical files fail). *Audit: `no_sample_bloat` + `no_object_duplicate` (`forbid_grep` placeholders, real enforcement via `wc`/`diff` in `audit.sh` plus exact `wc -l` gate).*
- **Depth default divergence (V5 T8b/T17 G4, SPEC §3.1)** — low-level `scene::View`/`render::View` default to `DepthConfig{false}` / `DepthMode::ColorOnly` (color-only deterministic, painter's order) so every analytic gate baked on `GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6` stays byte-identical without depth interference; `scene::View` owns `DepthConfig{bool enabled{false}; float clearDepth{1.0f};}` value object (`scene/depth_config.hpp`, composition SRP, OCP for future `func/writeMask/clearDepth/stencil`) with `setDepthConfig(DepthConfig)` bumping `depthTestGen`/`generation` and syncing `depthTest` bool; `ViewTarget` `DepthMode::Enabled` owns `GL_DEPTH_COMPONENT24` at `GL_DEPTH_ATTACHMENT` (completeness-checked) and `REContext::beginPass(depthConfig)` enables+clears depth once per `View::render` before `drawLayer` loop (OIT capture/composite explicitly `disableDepthTest`). `viz::Engine` facade diverges for viz correctness — `Engine::setView(ViewDescriptor)` / `Engine::createView` default to `DepthConfig{true}` for mesh-containing views via single-site `applyMeshDepthDefault` (`v.setDepthConfig(DepthConfig{true})` in `include/render_engine/engine.hpp` — audit `engine_depth_default` enforces). Visualization consumers expect true occlusion (nearer wins) when meshes overlap; low-level stays `false` for deterministic gates. *Audit: `engine_depth_default` (`require_grep DepthConfig\{true` in `include/render_engine/engine.hpp`) + `grep -c "setDepthConfig" ==1 && grep -c "DepthConfig{true" ==1` (analytic `==1`, not `>=1`) + `grep -c "DepthConfig" scene/ ==1`.*
- **Naming — `re::scene` is prefix (T17 G6, NAMING_CONVENTIONS §6)** — types inside `re::scene::` carry no `App` prefix (`scene::MeshObject`, `scene::Camera`, `scene::View` — the namespace is the prefix); legacy prefixed alias is removed after `PerspectiveFraming` deletion T7 (the old `re::app` namespace alias). `docs/spec/*.md` and `scene/` task comments must cite `scene::MeshObject` (scoped `AppT`/`ReT` generic placeholders `IMapper<AppT,ReT>` kept as template param names, not legacy prefixed types, and `broker/README.md` ACL wording `app::` allowed as waived `app ↛ render` ACL example via `tools/comment_context.allow` if needed). `PerspectiveFraming` already removed T7, no prefixed legacy remains outside `broker/README.md`. `README.md` module list enumerates `scene/ broker/ utils/ test_utils/` matching `AUDIT_SOURCE_DIRS` (`io data volume scene core broker render app utils test_utils tests`). *Audit: legacy 12-char contiguous name grep `==0` in `docs/` and `scene/` (analytic 0 post-sweep, ACL `broker/README.md` allowed `app::` wording waived) + `grep -c "class Light" scene/light.hpp ==1` preserved + `grep -c "class Engine" ==1 && grep -c "setLights" ==1 && grep -c "addMesh" ==1` incremental (not reverting `T1`/`T15` facade) + `comment_tag_context` waiver only for `render/shaders/` + `test_utils/` + `examples/`.*
