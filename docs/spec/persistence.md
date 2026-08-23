# SPEC §10 — Persistence, Layouts/Pages, and Asset Lifetime

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §10" mean this file. This section was added
> in the V3 spec update to replace the primitive `id + size-dump` persistence that
> blocked multi-page/layout Views with colliding `id`s.

## 10. Persistence — what stays alive across a frame

### 10.1 Principle — persistence is content-addressed, not `id`-addressed (SOLID/EOL — binding)

- **Identity ≠ handle.** A `uint64_t id` is a *stable handle* that app may
  generate for UI/picking; it is **never** the sole persistence key.
  Two `pages/layouts` with the same `id` but **different content**
  (`itemIds`, `plane`, `material`, `rect`) are different objects and MUST NOT
  alias a cached `ReView` / `Re*Object` / `ReMaterial` from the other layout.
  The cache key is always `(type, contentHash)` or `(scope, id, generation)`,
  where `scope` is the owning **layout/page** and `generation`/`version` is
  bumped by every mutator.
- **Content-addressed with scope + schema version (EOL).** The canonical key in `broker/` (SPEC §3/§11) is
  `CompositeKey{ Version, LayoutId, ViewId or ObjectId, TypeIndex, Generation, ContentHash }`
  (skeleton `CompositeKey{Version,LayoutId,Id,Gen,Hash}` in `scene/composite_key.hpp` header-only value type — T2 V3.2a lands the skeleton before any cached mapper).
  `Version` is the persistence schema version (uint32, bumped when `Re*` field inventory or hash algorithm changes) — without it a V3.7 `ReView` cache would alias a V3.8 cache with different fields (EOL cache-coherence bug; see Software Patterns Lexicon cache-key-design: omitting version/tenant/locale causes wrong-value-to-wrong-caller). A generation-bumped mutation (`Camera::rotate(Δ)`, `MaterialDesc::baseColor.a
  1→0.5`) keeps the same `Id` but changes `Generation`+`ContentHash`; the
  broker's `translateCached` overwrites the `Re*` object **in place** without
  recreating its `ReView`/`FBO`. `ContentHash` is SHA-256 (skeleton FNV-1a 64-bit truncate for deterministic tests) of the stable field bytes (like content-hash cache pattern — SHA-256 path-independent invalidation), **hash of stable bytes, not pointer** (`CompositeKey::hashStableBytes` hashes canonical bytes; two heap allocations with identical bytes produce identical hash, distinct pointer addresses do not affect the hash).
- **Phase-type is disallowed.** No code path does `if (rect.w != last.w) dump()`
  alone. Size, pose, and layout are orthogonal signals (SPEC §10.3).
- **RE long-lived, app long-lived, broker caches both (SRP).** `render::View` (`ReView`)
  owns a single `ViewTarget{Texture2D+Framebuffer}` (`ReView` delegates FBO lifecycle to `ViewTarget` — SRP via composition; `ReView`'s single responsibility is view semantics, not GL resource lifetime) sized `rect.w×rect.h` per `View`; its map entry (`ViewCompositor::reViews_[compositeKey]` — see §11.3 `ViewBridge` now split) lives for the lifetime of the containing layout. `AssetRegistry::Slot {MeshGeometry}` (`render/asset_registry.hpp`) and `Volume Tex` (`core::Texture3D`) live globally, deduped, and are shared across Views and pages.

**SOLID note — SRP/ISP for persistence:** Per SRP, persistence has *one reason to change per key.* The composite key's per-field generations (see §10.4) give `ViewBridge` one reason to change per *field* (camera vs material vs plane), not one monolithic `View::generation`. Per ISP, callers needing only `ViewRect` cache (`ensureTarget`) do not depend on `MaterialMapper` cache invalidation — segregated caches per `ICachedMapper`.

### 10.2 Scope — pages, layouts, views, objects

- A **Layout** is a `vector<AppView>` snapshot with a `LayoutId` (stable
  generation per user action: `2×2 → 1×1`, split, close). Layouts may be
  cached off-screen.
- A **Page** may own `N` layouts (or one layout — the simpler case keeps one
  active layout + `N` cached). The `ViewBridge` key is `CompositeKey{
  LayoutId, ViewId }`. A global `ViewId=1` in `layout A` and `layout B` maps to
  **two** distinct `ReView`s, even if `rect` and count coincide.
- `SceneStore` can be global (pointer-identity `data::Mesh*`/`VolumeDataset*`
  shared across pages) or page-scoped; the choice is recorded per object
  registry. Global `SceneStore` visibility is `AppView::itemIds` page-local;
  a mesh visible on page A but hidden on page B remains in the global store,
  its `Re*Object` cached, but the `ReView` on page B simply does not draw it.
- Swapping two views' `rect`s (same `ViewId`s, rectangles permuted) is a
  **size/view-rect dirty**, not a layout-count change — each `ReView`
  keeps its identity and `ensureTarget` recreates only its inner
  `Texture2D+Framebuffer` in place. Swapping two views' **content**
  (same `ViewId`s, `itemIds` permuted) is an **item-list dirty** —
  `ReView::setItems` rebuilds the `IRenderable` list without map churn.

### 10.3 What changes on each kind of user action

| User action | What mutates (app) | What MUST NOT recreate | What MAY recreate in place | Map churn |
|---|---|---|---|---|
| `Camera pan/rotate/zoom` (see §3 camera) | `app::Camera::generation` (view matrix) | `ReView`, `FBO`, items, assets, lights | none — `CameraTranslator::translateCached` only | none |
| `Plane slice scroll` (`AppPlaneDesc::point`) | `plane.generation` | `ReView` identity, `FBO`, `Texture3D`, other items | none — `PlaneTranslator` only | none |
| `Light` move/color/intensity | `Light.generation` | `ReView`, `FBO`, scene items, `Asset` | `ReLight[]` uniforms | none |
| `Material` edit (`baseColor.a 1→0.5` flips `isTransparent`) | `MaterialDesc::generation` | `Re*Object` identity, `FBO` | `ReMaterial` (dedup by hash) + `needsOIT` flag | none |
| `Mesh transform` drag | `MeshObject::generation` | `ReView`, `FBO`, `MeshGeometry` | `ReMeshObject::model+worldBounds` in place | none |
| `TF` edit | `VolumePresentation::generation` | `Texture3D`, `ReView` | `ReTfUniforms` | none |
| Visibility toggle (`visible=false`) | `bool visible`+`gen` | keep cached `Re*Object`/`ReLight` | none — `ReView::render` skips that layer | none |
| Add object | new id in `SceneStore` + one `AppView::itemIds` push | other `ReView`s/items | one new `Re*Object` + one new `IRenderable` in that `ReView` | none (vector grows) |
| Delete object | `itemIds` loses id, `liveCount-1` | other `ReView`s | one `IRenderable` erased from that `ReView`; asset kept if shared | none |
| Mode toggle `planar→3D` (same `ViewId`, plane `some→nullopt`, `itemIds` swap) | `planeGen`+`itemIds`+`camera.mode` | `ReView` identity (map key unchanged) | `plane` cleared, items rebuilt, camera re-translated | none |
| Window **size** change (`rect.w×h` all new, count+ids same) | each `AppView::rect` | `ReView` identities, items | each `ReView::ensureTarget` recreates inner `Texture2D+Framebuffer` | none |
| **Layout** change (count or set of `ViewId`s differs) | `AppViewStore` size/set | assets, `ReMaterial`, global `Texture3D` survivors still referenced | new/removed `ReView` map entries | insert/erase `ReView` entries |

### 10.4 Generation & dirty wiring (SOLID — per-field generations, OCP for dirty, web-verified)

- **Per-field generations (SRP, ISP, performance — binding).** Each *field* of an `app::` object carries its own `uint64_t generation` (not one monolithic `generation` per object — monolithic violates SRP: a `Camera::rotate` would dirty `MaterialDesc` cache because one actor (camera team) forces recompile of material actor; violates ISP: `MaterialMapper::mapCached` forced to depend on camera gen it never uses — Baeldung ISP fat `Worker` per 2026-05-25, NDepend ISP, ICS SRP 2024-08-14 "one actor, one reason to change"). Concrete: `app::Camera` carries `viewGen, projGen` (separate — `orbit` bumps both, `pan` bumps only `viewGen`); `app::View` carries `rectGen, cameraGen, planeGen, lightsGen, itemsGen` (see §10.3 table). Each `ICachedMapper` caches `map<CompositeKey, {uint64_t lastFieldGen, Hash lastHash}>` keyed by its *field's* gen+hash (CompositeKey includes `Version` — cache-key versioning per Software Patterns Lexicon + Dev Genius 2025-12-25): `translateCached` short-circuits when `fieldGen==lastFieldGen` **and** `fieldHash==lastFieldHash` (hash avoids redundant re-translation when an app assignment was bit-identical — e.g., `setBaseColor(sameColor)` bumps gen but hash equality short-circuits; canonicalization: normalize `glm::vec4` bytes before SHA-256 per System Overflow key design). This is the *"per-field (cameraViewGen, planeGen, materialGen) split"* required by §10.5 to avoid cross-contamination. **SRP note:** per-field gen gives `ViewSynchronizer` one reason to change per *field* (camera vs material vs plane), not one monolithic `View::generation` (SRP actor = field owner, per Clean Architecture Ch.7 + Evertop SRP).

- **Who bumps (encapsulation, SRP):** Mutators (`Camera::rotate`, `MeshObject::setTransform`) bump their field's generation **and** the owning store's global `storeGen` via single entry point `SceneStore::bump(FieldId)` — SRP: store owns generation policy, objects are values (God Object guard: store does not expose raw `generation++`; all fields private with bumping setters — `setMaterial(desc)` not `obj.mat = desc` — so direct mutation without bump is forbidden at compile time). This resolves §13 Q4/Q5 poll-vs-push ambiguity with a clear contract (see below). `SceneStore` bounded-context justification: one aggregate root per page/layout, one owning team (layout/scene), so single store is cohesive per DDD bounded context (Software Patterns Lexicon) rather than fragmented HandleRegistry+GenTracker+HashCache (deferred to Q39 if actor splits).

- **Poll vs push — hybrid (OCP-open, DIP — binding, web-verified).** `ViewSynchronizer::sync` is **poll-mostly with push opt-in** (hybrid): every `renderFrame` it polls `SceneStore::storeGeneration()` vs `lastStoreGen` as the cheap early-out (no scan if global gen unchanged — cache-key version prefix enables granular invalidation per System Overflow); if changed, it iterates only the per-field gen diff set produced by `SceneStore::dirtyFieldsSince(lastStoreGen)` (a bounded set of `FieldId`s, not a full store scan — OCP via `IJobExecutor::parallelFor` extension, see NFR §5). An explicit `markDirty(ViewId, FieldId)` push path is provided for editor integrations that mutate off-frame (observer `AppView::setRect` calls `store.markDirty(viewId, Field::Rect)`); the poll path still coalesces no-ops via hash equality. `renderAll`/`presentAll` never poll — they render already-synced `ReView`s (SRP via `ViewCompositor` — ICS SRP). The dirty bus is abstracted behind `broker::IDirtyTracker { markDirty(), dirtyFieldsSince(), storeGeneration() }` (DIP: `ViewSynchronizer` depends on `IDirtyTracker` abstraction, not concrete `SceneStore`; future `ThreadPoolExecutor` can batch invalidations without editing synchronizer — Baeldung DIP composition root, Oleksii Tym DIP). `IJobExecutor` behind `dirtyFieldsSince` parallelizes the bounded scan (DIP + OCP for threading; inline `execute(f){f();}` fallback keeps ASan/UBSan 1-thread determinism per NFR §5).

- **EOL: overflow & thread-safety.** `generation` is `uint64_t` with `!=` wrap-safe equality (not `<` ordering), so wrap is safe; hash still differentiates (documented for EOL). `AssetStore` ref-counts are `std::atomic<uint32_t>` even under inline executor (data-race-free per NFR §5); `ReView` list mutation is externally synchronised (`sync` happens-before `renderAll` happens-before `presentAll`) — documented `thread-compatible` vs `thread-safe` per header review (Q37).

References: cache-key `version` + tenant isolation via Software Patterns Lexicon cache-key-design + System Overflow cache-key design + Dev Genius version-your-cache-keys 2025-12-25 (hierarchical `Version:LayoutId:Type:Hash` + SHA-256 at load time); content-hash SHA-256 cache pattern; SRP actor-based per-field split per Clean Architecture Ch.7 + ICS SRP 2024-08-14.

**Decision (resolves §13 Q4, Q5, Q6, and §10.5 Content-hash):** per-field generations + hybrid poll/push via `IDirtyTracker` + `IJobExecutor` is the binding V3 contract (EOL-sustaining till end-of-life: OCP for job system, DIP for tracker, SRP per field, ISP per mapper, cache-key versioning). Plain monolithic `generation` and pure-push or pure-poll alternatives are retired.

### 10.5 Decisions (V3 spec-review — bindings) & remaining grill

- **★ Resolved: `ViewBridge::sync` trigger — hybrid poll+push** (see §10.4): primary is per-frame poll on `SceneStore::storeGeneration()` with early-out and `dirtyFieldsSince()` bounded scan; `markDirty` push is opt-in for off-frame edits. Pure poll-only or pure push-only alternatives retired. (Resolves §13 Q6.)
- **★ Resolved: `rect` placement — absolute `ViewRect x,y,w,h` in *physical framebuffer pixels* produced by `Layout::resolve(framebufferSize, contentScale)`** with `app::mprViewports(framebufferSize)` as the default Layout resolver (absolute is the `ReView`/`core::setViewport`/`IRHIFramebuffer::blit` lingua franca — bottom-left y-up; relative `(row,col,rowSpan,colSpan)` is the app-level **layout constraint** input to `Layout::resolve`, not the persisted `View` field). `View::rect` stores **physical pixels** (`glfwGetFramebufferSize`); `LayoutSpec{row,col,rowSpan,colSpan,weight}` is the serialisable relative form. This preserves OCP (new layout constraints don't edit `View`) and EOL serialisation (JSON + binary blob per DCS; see §10.6/§13 Q36). On HiDPI (macOS Retina, Windows 125%/150% scale, `GLFW_SCALE_TO_MONITOR`) `framebufferSize` already accounts for `contentScale` (web.dev high-dpi 2025-04-14, MDN devicePixelRatio, GLFW #1857 vispy #99: framebuffer vs window size). `View::rect` physical pixels therefore implicitly includes DPR; `CompositeKey{rect}` hash changes when DPR changes via monitor move without extra `dpr` field in key — monitor move triggers `Layout::resolve` + `ViewTarget` recreate only (Q34). **Normalisation:** `weight` distribution is flex (remaining space after fixed `span` weights) with `rowSpan==0` = fill remainder; equality → equal weights; `Layout::resolve` uses `framebufferSize * (weight/sumWeights)` rounding to integer physical pixels with remainder to last view (deterministic). (Resolves §13 Q11 + Q34.)
- **★ Resolved: `AppSceneStore` scope — global `SceneStore` + per-page `ViewStore`** (hybrid, DDD bounded context): `SceneStore` (assets, `Mesh`/`VolumeDataset`) is global and shared — content-hash dedup (§7) is maximised, pointer-identity `byObject_` replaced by `(AssetId,gen,hash)`; `ViewStore`/`LayoutStore` is per-page (page-local `itemIds` visibility). A mesh visible on page A but hidden on page B remains in `global SceneStore` (cached `Re*Object`), page B's `ReView` simply does not `items` it — no `Re*Object` leak beyond one global entry (released when last page's `ReView`/`ReViewCompositor` drops; ref-count `atomic` per Q35). This decides the §10.2 "global vs per-page" choice as hybrid-global-assets + per-page-views. `SceneStore` is one aggregate root per page/layout (DDD bounded context) — cohesive, not God object (see §10.4). (Resolves §13 Q10.)
- **★ Resolved: `ReView` hidden-`Views` (`AppView::visible=false`) — KEEP `FBO` allocated** for fast re-show (GPU retained) while `ViewCompositor` keeps the `ReView` entry; `ReView::render` early-outs (`visible==false` skip). `Re*Object` hidden cache kept while `SceneStore` holds the `Id`; eviction is arena-scoped on `Layout` switch (see §7 `LayoutId`-scoped arena) or explicit `compactHidden` on memory pressure/page discard (opt-in free, not default; arena eviction is LRU within `LayoutId` scope, ref-count zero → deferred GC until `compactHidden` or layout discard — see §13 Q35). Same for hidden `App*Object` → `Re*Object`. (Resolves §10.5/§13 Q8.)
- **★ Resolved: `AppPlaneDesc::Space` — segregated `TranslateContext{ViewContext, optional<VolumeContext>}` supplies them** — `PlaneMapper::map` receives `ViewContext{viewPlane, viewMatrix}` + optional `VolumeContext{volumeModel,dims,meshBounds,voxelSpacing}` supplied by `ViewMapper` (who bakes the active volume's `ReVolumeObject::model`+`dims` or mesh `bounds()` and the `AppView`'s referenced `VolumeObjectId`). `PlaneMapper` does the `voxelIdx→world` math (`coord=idx+0.5` → `world = volumeModel * vec4(idx/dims)` etc.); callers never pre-convert. Adding a new `Space` (e.g., `NormalizedDevice`) adds a branch in ONE `SpaceConverter` consumed by `PlaneMapper` (OCP via `PlaneMapper`+`SpaceConverter` extension, not scatter per §11.4.2). (Resolves §13/§10.5 plane space.)
- **★ Resolved: Content-hash granularity — per-field split** (see §10.4): `FieldId` → separate `generation+contentHash` per field (SHA-256 of canonicalized stable bytes at load time, not per-frame — System Overflow + Dev Genius), so `Camera::rotate` does not force `MaterialMapper`. Single-object hash retained as global fallback for legacy objects but partitioned per-field is the binding path. Hash per field coalesces bit-identical assignments (`setBaseColor(sameColor)` bumps gen but hash equality short-circuits). (Resolves §10.5.)
- **★ Resolved: `IJobExecutor` + `IDirtyTracker` threading contract (EOL, §13 Q37):** `core::IJobExecutor{ void execute(Function<void()> f); void parallelFor(size_t n, Function<void(size_t)> fn); }` with inline synchronous fallback (`execute(f){f();}`) keeps ASan/UBSan 1-thread determinism (NFR §5 single-threaded V1 contract). `ViewSynchronizer::sync` + `Broker::parallelFor` + `AssetStore::registerAsset` are DIP-behind `IJobExecutor`/`IDirtyTracker`; `ITransparencyPipeline::begin/end` may dispatch via `IJobExecutor` (explicit). `AssetStore` ref-counts are `std::atomic<uint32_t>` (data-race-free under inline executor); `ReView` list mutation guarded by `sync` happens-before `renderAll` happens-before `presentAll` (externally synchronised, documented `thread-compatible` vs `thread-safe` per header review). Future `ThreadPoolExecutor` (V4) injected without editing `broker`/`scene` (OCP). (Resolves §13 Q37 binding.)

**Remaining grill (must pin before V3 kicks off — see §13):** None — all §10 and §13.8/13.9 ★ resolved binding 2026-08-23 (Q3/Q9/Q27/Q28/Q32f/Q39-Q47 pinned; Q32 weighted-blended OIT, Q36 `nlohmann/json` 3.11.x, Q38 `ITypeErasedDraw` virtual). See `docs/spec/open_questions.md:11` header.

### 10.6 Relationship to asset management (see SPEC §7 / §12)

- §12 (`Data Asset Persistence`) owns the store for `data::Mesh` /
  `VolumeDataset` itself; §10 owns the **view/object** persistence that *uses*
  those assets. §12's `(AssetId, type, contentHash, scope, refCount)` key
  is the lower layer §10's per-item translators call into.

> **T6 landed (V3.5):** Full content-addressed persistence — `CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash}` (not `id+size` dump) via `scene/composite_key.hpp` (now includes `typeHash`). `ReView`/`Re*Object`/`ViewTarget` persist across `sync()` — `Camera::rotate` dirties only `CameraMapper` (per-field `viewGen` via `ViewSynchronizer`+`ViewCompositor` SRP-split), `2D→3D` toggle same `ViewId` (`plane some→nullopt`, `itemIds` swap) rebinds `plane+items` without `ReView` map churn, size resize recreates only `ViewTarget` inner `FBO` (physical pixels `framebufferSize` + `contentScale` via `Layout::resolve`), layout count/set change inserts/erases `ReView`s. Hybrid `storeGen` poll early-out + bounded `dirtyFieldsSince()` scan + `markDirty()` push opt-in via `broker::IDirtyTracker` (`broker/idirty_tracker.hpp`, `IJobExecutor` inline fallback) + `scene::SceneStore`/`ViewStore::markDirty`/`dirtyFieldsSince` + `scene/LayoutSpec` relative → `Layout::resolve(windowSize,dpr)` absolute `Rect` (within 1 px). See gate `t6_persistence_test.cpp`.

### 10.7 Implementation honesty requirements (Sr. review 2026-08-23 — Task T19)

The persistence contract above is only real if the machinery actually computes
what it claims. The following are **binding**, closing gaps the architecture
review found between the documented mechanism and its implementation:

- **`dirtyFieldsSince` must derive from the dirty log.** Returning a hardcoded
  field set while maintaining a `dirtyLog_` that is never read makes the
  hybrid push/poll story decorative. The gate asserts a camera-only mutation
  yields exactly `{Camera}`.
- **Tombstones are enforced at resolve time.** An id erased from
  `SceneStore`/`ViewStore` resolves to a typed stale error afterward — the
  written-but-never-read tombstone generation is consulted by `resolve`.
- **One identity type.** `StableKey` exists exactly once in `broker/`
  (versioned shape `{Version/LayoutId/ViewId}`); synchronizer and compositor
  share it — divergent twins drift and silently break reuse semantics.
- **No discarded dirty computation and no fake parallelism.** Dirty results
  are consumed or the code is deleted; executor-exercise stubs whose outputs
  are `(void)`-discarded misrepresent the concurrency contract.
- **Per-camera mapper caching.** A single-slot camera cache thrashes under
  N cameras; `invalidate(id)` implies id-keyed entries.

> **Asset-store gap (Task T14):** today's GPU-side store is mesh-only —
> `render::AssetRegistry` dedups `MeshGeometry`, but volume/image textures
> live in per-renderer pointer-keyed caches without invalidation, and
> `broker::AssetStore` mirrors the mesh-only shape despite unused
> `computeContentHash(VolumeDataset/Image)` overloads. §10's "Volume Tex lives
> globally, deduped" becomes true only when T14 lands the typed multi-kind
> store keyed by `(AssetId, generation, contentHash)`.
