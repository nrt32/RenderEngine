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
   recreating its `ReView`/`FBO`. `ContentHash` is **the single canonical hash defined in `data/content_hash.hpp` `re::data::computeContentHash` / `hashStableBytes`** — **FNV-1a 64-bit of stable bytes for the skeleton/tests, SHA-256 truncated to 64 bits for prod** (both hash canonical stable bytes, not pointers — like content-hash cache pattern — SHA-256 path-independent invalidation); the header `data/content_hash.hpp:31` is the **source of truth** pinned by all specs (`assets.md:127-128` and here reference it verbatim, not a second definition). **Hash of stable bytes, not pointer** (`CompositeKey::hashStableBytes` delegates to `data::hashStableBytes`; two heap allocations with identical bytes produce identical hash, distinct pointer addresses do not affect the hash). `computeContentHash` is hashed **at load/register time, never per frame** (spy `contentHashCallCount()` must be 0 per frame in steady-state `T7` gate).
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
- `SceneStore` is **global** (content-hash dedup, pointer-identity `data::Mesh*`/`VolumeDataset*` shared across pages) and `ViewStore`/`LayoutStore` are **per-page** (binding hybrid per `§10.5` Q10 — `SceneStore` global assets + per-page `ViewStore`/`LayoutStore`; the alternative “page-scoped SceneStore” is **retired** — see `§10.5` ★ Resolved). Global `SceneStore` visibility is `AppView::itemIds` page-local;
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

- **Poll vs push — hybrid (OCP-open, DIP — binding, web-verified).** `ViewSynchronizer::sync` is **poll-mostly with push opt-in** (hybrid): every `renderFrame` it polls `SceneStore::storeGeneration()` vs `lastStoreGen` as the cheap early-out (no scan if global gen unchanged — cache-key version prefix enables granular invalidation per System Overflow); if changed, it iterates only the per-field gen diff set produced by `SceneStore::dirtyFieldsSince(lastStoreGen)` — **computed, never hardcoded** (persistence-honesty requirement, §10.7): both stores keep a bounded per-field dirty log with at most ONE slot per `FieldId`, whose recorded generation is RAISED IN PLACE on every mutation of that field (memory is O(`#FieldIds`) regardless of frame count), so the answer is the exact distinct set of fields genuinely mutated after `lastGen`, in first-mutation order — a camera-only mutation yields exactly `{CameraView}`, an object add records `{Transform, Items}`, any erase records `{Items}`. An explicit `markDirty(ViewId, FieldId)` push path is provided for editor integrations that mutate off-frame (observer `AppView::setRect` calls `store.markDirty(viewId, Field::Rect)`); the poll path still coalesces no-ops via hash equality. `renderAll`/`presentAll` never poll — they render already-synced `ReView`s (SRP via `ViewCompositor` — ICS SRP). The dirty bus is abstracted behind `broker::IDirtyTracker { markDirty(), dirtyFieldsSince(), storeGeneration() }` (DIP: `ViewSynchronizer` depends on `IDirtyTracker` abstraction, not concrete `SceneStore`; future `ThreadPoolExecutor` can batch invalidations without editing synchronizer — Baeldung DIP composition root, Oleksii Tym DIP). Erased handles stay detectable: the tombstone generations written on remove are consulted by `SceneStore::resolve` / `ViewStore::resolve`, which return typed error code 2 ("stale") instead of letting a dead handle masquerade as unknown.

- **EOL: overflow & thread-safety.** `generation` is `uint64_t` with `!=` wrap-safe equality (not `<` ordering), so wrap is safe; hash still differentiates (documented for EOL). `AssetStore` ref-counts are `std::atomic<uint32_t>` even under inline executor (data-race-free per NFR §5); `ReView` list mutation is externally synchronised (`sync` happens-before `renderAll` happens-before `presentAll`) — documented `thread-compatible` vs `thread-safe` per header review (Q37).

References: cache-key `version` + tenant isolation via Software Patterns Lexicon cache-key-design + System Overflow cache-key design + Dev Genius version-your-cache-keys 2025-12-25 (hierarchical `Version:LayoutId:Type:Hash` + SHA-256 at load time); content-hash SHA-256 cache pattern; SRP actor-based per-field split per Clean Architecture Ch.7 + ICS SRP 2024-08-14.

**Decision (resolves §13 Q4, Q5, Q6, and §10.5 Content-hash):** per-field generations + hybrid poll/push via `IDirtyTracker`, with `IJobExecutor` as the execute-only inline seam (batched execution arrives together with a real ThreadPoolExecutor consumer — see the §10.5 Q37 entry), is the binding V3 contract (EOL-sustaining till end-of-life: OCP for job system, DIP for tracker, SRP per field, ISP per mapper, cache-key versioning). Plain monolithic `generation` and pure-push or pure-poll alternatives are retired.

### 10.5 Decisions (V3 spec-review — bindings) & remaining grill

- **★ Resolved: `ViewBridge::sync` trigger — hybrid poll+push** (see §10.4): primary is per-frame poll on `SceneStore::storeGeneration()` with early-out and `dirtyFieldsSince()` bounded scan; `markDirty` push is opt-in for off-frame edits. Pure poll-only or pure push-only alternatives retired. (Resolves §13 Q6.)
- **★ Resolved: `rect` placement — absolute `ViewRect x,y,w,h` in *physical framebuffer pixels* produced by `Layout::resolve(framebufferSize, contentScale)`** with `app::mprViewports(framebufferSize)` as the default Layout resolver (absolute is the `ReView`/`core::setViewport`/`IRHIFramebuffer::blit` lingua franca — bottom-left y-up; relative `(row,col,rowSpan,colSpan)` is the app-level **layout constraint** input to `Layout::resolve`, not the persisted `View` field). `View::rect` stores **physical pixels** (`glfwGetFramebufferSize`); `LayoutSpec{row,col,rowSpan,colSpan,weight}` is the serialisable relative form. This preserves OCP (new layout constraints don't edit `View`) and EOL serialisation (JSON + binary blob per DCS; see §10.6/§13 Q36). On HiDPI (macOS Retina, Windows 125%/150% scale, `GLFW_SCALE_TO_MONITOR`) `framebufferSize` already accounts for `contentScale` (web.dev high-dpi 2025-04-14, MDN devicePixelRatio, GLFW #1857 vispy #99: framebuffer vs window size). `View::rect` physical pixels therefore implicitly includes DPR; `CompositeKey{rect}` hash changes when DPR changes via monitor move without extra `dpr` field in key — monitor move triggers `Layout::resolve` + `ViewTarget` recreate only (Q34). **Normalisation:** `weight` distribution is flex (remaining space after fixed `span` weights) with `rowSpan==0` = fill remainder; equality → equal weights; `Layout::resolve` uses `framebufferSize * (weight/sumWeights)` rounding to integer physical pixels with remainder to last view (deterministic). (Resolves §13 Q11 + Q34.)
- **★ Resolved: `AppSceneStore` scope — global `SceneStore` + per-page `ViewStore`** (hybrid, DDD bounded context): `SceneStore` (assets, `Mesh`/`VolumeDataset`) is global and shared — content-hash dedup (§7) is maximised, pointer-identity `byObject_` replaced by `(AssetId,gen,hash)`; `ViewStore`/`LayoutStore` is per-page (page-local `itemIds` visibility). A mesh visible on page A but hidden on page B remains in `global SceneStore` (cached `Re*Object`), page B's `ReView` simply does not `items` it — no `Re*Object` leak beyond one global entry (released when last page's `ReView`/`ReViewCompositor` drops; ref-count `atomic` per Q35). This decides the §10.2 "global vs per-page" choice as hybrid-global-assets + per-page-views. `SceneStore` is one aggregate root per page/layout (DDD bounded context) — cohesive, not God object (see §10.4). (Resolves §13 Q10.)
- **★ Resolved: `ReView` hidden-`Views` (`AppView::visible=false`) — KEEP `FBO` allocated** for fast re-show (GPU retained) while `ViewCompositor` keeps the `ReView` entry; `ReView::render` early-outs (`visible==false` skip). `Re*Object` hidden cache kept while `SceneStore` holds the `Id`; eviction is arena-scoped on `Layout` switch (see §7 `LayoutId`-scoped arena) or explicit `compactHidden` on memory pressure/page discard (opt-in free, not default; arena eviction is LRU within `LayoutId` scope, ref-count zero → deferred GC until `compactHidden` or layout discard — see §13 Q35). Same for hidden `App*Object` → `Re*Object`. (Resolves §10.5/§13 Q8.)
- **★ Resolved: `AppPlaneDesc::Space` — segregated `TranslateContext{ViewContext, optional<VolumeContext>}` supplies them** — `PlaneMapper::map` receives `ViewContext{viewPlane, viewMatrix}` + optional `VolumeContext{volumeModel,dims,meshBounds,voxelSpacing}` supplied by `ViewMapper` (who bakes the active volume's `ReVolumeObject::model`+`dims` or mesh `bounds()` and the `AppView`'s referenced `VolumeObjectId`). `PlaneMapper` does the `voxelIdx→world` math (`coord=idx+0.5` → `world = volumeModel * vec4(idx/dims)` etc.); callers never pre-convert. Adding a new `Space` (e.g., `NormalizedDevice`) adds a branch in ONE `SpaceConverter` consumed by `PlaneMapper` (OCP via `PlaneMapper`+`SpaceConverter` extension, not scatter per §11.4.2). (Resolves §13/§10.5 plane space.)
- **★ Resolved: Content-hash granularity — per-field split** (see §10.4): `FieldId` → separate `generation+contentHash` per field (SHA-256 of canonicalized stable bytes at load time, not per-frame — System Overflow + Dev Genius), so `Camera::rotate` does not force `MaterialMapper`. Single-object hash retained as global fallback for legacy objects but partitioned per-field is the binding path. Hash per field coalesces bit-identical assignments (`setBaseColor(sameColor)` bumps gen but hash equality short-circuits). (Resolves §10.5.)
- **★ Resolved: `IJobExecutor` + `IDirtyTracker` threading contract (EOL, §13 Q37):** `core::IJobExecutor{ void execute(Function<void()> f); } // `parallelFor` deferred to V4 — see `nfr.md` EOL-3` with inline synchronous fallback (`execute(f){f();}`) keeps ASan/UBSan 1-thread determinism (NFR §5 single-threaded V1 contract). `ViewSynchronizer::sync` + `AssetStore::registerAsset` are DIP-behind `IJobExecutor`/`IDirtyTracker`; `ITransparencyPipeline::begin/end` may dispatch via `IJobExecutor::execute` (explicit; `parallelFor` arrives with `ThreadPoolExecutor` V4). `AssetStore` ref-counts are `std::atomic<uint32_t>` (data-race-free under inline executor); `ReView` list mutation guarded by `sync` happens-before `renderAll` happens-before `presentAll` (externally synchronised, documented `thread-compatible` vs `thread-safe` per header review). Future `ThreadPoolExecutor` (V4) will EXTEND the interface with `parallelFor(size_t n, Function<void(size_t)> fn)` when it has a real consumer (OCP). **T21 update (binding):** the unused batched `parallelFor` entry was removed together with its discarded-results call site — `broker::IJobExecutor` ships `execute()`-only until a real `ThreadPoolExecutor` consumer lands; keeping an unexercised batched API was ruled write-only scaffolding. (Resolves §13 Q37 binding.)

**Remaining grill (must pin before V3 kicks off — see §13):** None — all §10 and §13.8/13.9 ★ resolved binding 2026-08-23 (Q3/Q9/Q27/Q28/Q32f/Q39-Q47 pinned; Q32 weighted-blended OIT, Q36 `nlohmann/json` 3.11.x, Q38 `ITypeErasedDraw` virtual). See `docs/spec/open_questions.md:11` header.

### 10.6 Relationship to asset management (see SPEC §7 / §12)

- §12 (`Data Asset Persistence`) owns the store for `data::Mesh` /
  `VolumeDataset` itself; §10 owns the **view/object** persistence that *uses*
  those assets. §12's `(AssetId, type, contentHash, scope, refCount)` key
  is the lower layer §10's per-item translators call into.

> **T6 landed (V3.5):** Full content-addressed persistence — `CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash}` (not `id+size` dump) via `scene/composite_key.hpp` (now includes `typeHash`). `ReView`/`Re*Object`/`ViewTarget` persist across `sync()` — `Camera::rotate` dirties only `CameraMapper` (per-field `viewGen` via `ViewSynchronizer`+`ViewCompositor` SRP-split), `2D→3D` toggle same `ViewId` (`plane some→nullopt`, `itemIds` swap) rebinds `plane+items` without `ReView` map churn, size resize recreates only `ViewTarget` inner `FBO` (physical pixels `framebufferSize` + `contentScale` via `Layout::resolve`), layout count/set change inserts/erases `ReView`s. Hybrid `storeGen` poll early-out + computed bounded `dirtyFieldsSince()` scan + `markDirty()` push opt-in via `broker::IDirtyTracker` (`broker/idirty_tracker.hpp`, `IJobExecutor` execute-only inline fallback) + `scene::SceneStore`/`ViewStore::markDirty`/`dirtyFieldsSince` + `scene/LayoutSpec` relative → `Layout::resolve(windowSize,dpr)` absolute `Rect` (within 1 px). See gate `t6_persistence_test.cpp`.

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

> **T21 landed (persistence honesty):** all five requirements above are
> shipped and enforced by `tests/t21_persistence_honesty_test.cpp`:
> `dirtyFieldsSince` computes from the bounded one-slot-per-field dirty log
> (a camera-only mutation yields exactly `{CameraView}`); tombstones are read
> by `SceneStore::resolve`/`ViewStore::resolve`, which return typed error
> code 2 for a stale (erased) handle and code 1 for a never-existing id;
> ReView identity is the single versioned `StableKey` in
> `broker/stable_key.hpp`, shared by synchronizer and compositor; the fake
> batch-exercise scaffolding is gone (`parallelFor` occurs zero times under
> `broker/`; `IJobExecutor` ships `execute()`-only); and `CameraMapper`
> memoizes per owning-view id — one CompositeKey-validated slot per view,
> hit/miss spy counters prove two cameras alternating pans each keep their
> own hits, and `invalidate(id)` evicts exactly that view's slot.

> **Asset-store gap (Task T14) — RETIRED (closed by T14, Landed `assets.md:90`):** today's GPU-side store WAS mesh-only —
> `render::AssetRegistry` dedups `MeshGeometry`, but volume/image textures
> live in per-renderer pointer-keyed caches without invalidation, and
> `broker::AssetStore` mirrors the mesh-only shape despite unused
> `computeContentHash(VolumeDataset/Image)` overloads. §10's "Volume Tex lives
> globally, deduped" becomes true only when T14 lands the typed multi-kind
> store keyed by `(AssetId, generation, contentHash)`.

### 10.8 Versioned `SceneStore::serialize()` JSON — wire format + migrations (T13)

`SceneStore::serialize()` is the versioned persistence wire format for the
scene value library — the only place the `CompositeKey{Version,LayoutId,ViewId,
Type,Gen,Hash}` leaves memory as bytes. `MaterialDesc`/`LightDesc` JSON via
`nlohmann/json` 3.11.3 (`CMakeLists.txt:117` `GIT_TAG v3.11.3`, already pinned)
is the stable variant wire for `MeshMaterialDesc`/`LightDesc`; this section
stabilizes `View` persistence on the same JSON lib, with `Version` migrations
and the `View` wire that was missing before T13.

Wire (JSON text + binary NRRD raw `uint16` blob + SHA-256 `contentHash` for
`VolumeDataset` beside the JSON; see `assets.md:64` §7 addendum decision
Q36/Q47 `nlohmann/json` 3.11.x — not `glaze`):

```json
{
  "Version": 1,
  "LayoutId": 0,
  "Views": [
    {
      "ViewId": 1,
      "Rect": {"x":0,"y":0,"w":800,"h":600},
      "Camera": {"eye":[0,0,3],"center":[0,0,0],"up":[0,1,0],
                 "fov":60,"aspect":1.333,"near":0.1,"far":10,
                 "viewGen":7,"projGen":7},
      "CompositeKey": {"Version":1,"LayoutId":0,"ViewId":1,"Type":"View","Gen":7,"Hash": 0x9e3779b9},
      "Plane": null,
      "ItemIds": [1],
      "ClearColor": [0.10,0.10,0.12,1.0],
      "DepthTest": false,
      "Lights": [],
      "LayerMask": 255,
      "LayerOverrides": {}
    }
  ],
  "Objects": [
    {"ObjectId":1,"Type":"Mesh","Gen":1,"Hash":1469598103934665603,
     "GeometryKind":"Mesh","Transform":[1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1],
     "Material": {"baseColor":[0.85,0.45,0.15,1.0],"metallic":0,"roughness":1},
     "AssetId": 42, "ContentHash": 0x1234}
  ],
  "Assets": [
    {"AssetId":42,"Type":"Mesh","ContentHash": 0x1234, "RefCount":1, "BytesSHA256":"abc..."}
  ]
}
```

`Version` is the persistence schema version (`CompositeKey::version`, `uint32`,
bumped when `Re*` field inventory or hash algorithm changes per §10.1 — without
it a V3.7 cache aliases a V3.8 cache, the EOL cache-coherence bug). `Version`
prefix invalidates the broker cache on migration (hierarchical
`Version:LayoutId:Type:Hash`, Software Patterns Lexicon + Dev Genius). On disk
the `Version` field is the first key in the JSON object so a reader can branch
before parsing the rest.

Migrations — `SceneMigrator{Version→Version}` chain (OCP via migrator registry
per `open_questions.md:78` Q36/Q47 DCS Data Contracts 2026-05-26 + `broker.md`
EOL-4): each `Version` bump adds one `Migrator` file plus one
`registerMigrator(from, to, fn)` call, zero edits to existing migrators. The
chain is `BACKWARD` compatible (new reader reads old writer, per Confluent Schema
Registry `BACKWARD` per V3 `EOL-4`) — old files remain readable to EOL; forward
and full compat are not required. `Re*` caches are never serialized (they are
reconstructible via `ViewSynchronizer` replay); only stable wire is
`SceneStore` (`Id+gen+hash` per object), `MaterialDesc`/`LightDesc` (stable
variant JSON `kind` discriminator), `LayoutSpec`/`ViewDesc` (relative), `Camera`
(`view/proj` matrices). `SceneStore::deserialize()` applies the migrator chain
from the file's `Version` to the current `CompositeKey::version` before
rebuilding the secondary `kindIndex_` (`SceneKind→set<Id>`) from the single-map
`objects_` (T6 single-map invariant — no `meshObjects_` partitions).

Why `View` was not serialized before T13: `MaterialDesc`/`LightDesc` already had
JSON via `nlohmann/json`, but `View` persistence via the content-addressed
`CompositeKey` was in-memory only; the T13 stabilization documents the `View`
wire (`Rect`, `Camera`, `CompositeKey`, `Plane`, `ItemIds`, `ClearColor`,
`DepthTest`, `Lights`, `LayerMask`, `LayerOverrides`) and the `Version` migration
contract so future `Version` bumps have a pinned format. `SceneStore::serialize()`
lives in `scene/store.hpp` (`std::string serialize() const` returning JSON text)
and `scene/store.cpp` (`deserialize` static), header-only `CompositeKey` stays
the cache key, not the file format — the JSON `CompositeKey` field is the
serialized projection of the in-memory key for debugging, the store rebuilds the
live key via `CompositeKey{Version,LayoutId,ViewId,Type,Gen,Hash}` on load.

Example `Version` migration (1→2): `View::clearColor` field added; migrator
`Migrator{1→2}` inserts `"ClearColor":[0,0,0,0]` default when missing and bumps
`Version` to 2; `Migrator{2→3}` would add `DepthTest` etc. — one file per bump,
registry `SceneMigrator::registerMigrator(1,2,fn)` OCP, zero edits to `Migrator
1→2` when `2→3` lands. `T6` single-map `objects_` is the only store of truth;
`kindIndex_` is rebuilt, not serialized (derived index, not source). Binary
`VolumeDataset` bytes are not in the JSON — they are the NRRD raw `uint16` blob
beside the JSON file plus `SHA-256` `contentHash` (see `assets.md` §7).
