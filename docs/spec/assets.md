# SPEC §7 — Data & asset plan

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §7" mean this file.

## 7. Data & asset plan

**Policy:** assets are committed **in-repo** under `data/` and reused by tests.
They must be small and clearly licensed; a `LICENSE` file sits beside every
external dataset (audit rule `assets_licensed`). Tests additionally use
procedural in-code geometry/volumes for determinism (no external dependency in
the test gate).

**Volume format:** io/volume loads **NRRD** (text header + raw, **uncompressed**
voxel block). The setup-time converter (`tools/convert_nrrd.py`, Python 3
stdlib only) downsamples the pinned CT source to ≤128³ and re-writes it as a
small raw NRRD for commit.

| Asset | Source (pinned URL) | License | Target path | Notes |
|---|---|---|---|---|
| Stanford bunny (OBJ) | `https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/stanford-bunny.obj` (SHA256 `1eb35d1e21ce99e5ce911353b6be278990713448dd9e8f5c9387f9de39b32205`) | Public domain | `data/meshes/bunny.obj` | sample mesh rendering; 2.4 MB |
| Utah teapot (OBJ) | `https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/teapot.obj` (SHA256 `1b5396fedd74b577e32cef41146582c2f2e1a050d5b4915193c0ac1ad4187ed4`) | Public domain | `data/meshes/teapot.obj` | sample mesh/slice rendering |
| CT chest sample volume | `https://github.com/Slicer/SlicerTestingData/releases/download/SHA256/4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e` (published SHA256 `4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e`, file `CT-chest.nrrd`) | CC-BY-SA 4.0 (Medical Decathlon) | `data/volumes/sample_ct.nrrd` | downsample ≤128³ → raw NRRD; used by volume/MPR samples + tests |
| Golden fixtures | Hand-authored small meshes/volumes/images | Project-owned | `data/fixtures/` | committed, tiny, used by io/data tests (hand-counted acceptance constants) |
| Procedural geometry | Generated in code at runtime | n/a | n/a | deterministic tests; no file dependency |

**Fetch method (two-phase: SETUP stages, T2 commits):** because assets are
committed, setup does NOT download into the repo at build time. The setup phase
(`/loop-setup`) downloads the pinned source files above, verifies SHA256, runs
`tools/convert_nrrd.py` (downsample the CT to ≤128³ and re-write as raw NRRD),
and stages the results under `data/` — but does **not** commit. Committing the
assets, LICENSE files, `data/README.md` (sources, URLs, licenses, checksums),
and recording the verified SHA256s in this section is the **T2 implementer's**
deliverable. Re-running setup is therefore idempotent and never touches git
state.

**Verified SHA256s of the committed files (T2):**
- `data/meshes/bunny.obj` — `1eb35d1e21ce99e5ce911353b6be278990713448dd9e8f5c9387f9de39b32205` (matches source)
- `data/meshes/teapot.obj` — `1b5396fedd74b577e32cef41146582c2f2e1a050d5b4915193c0ac1ad4187ed4` (matches source)
- `data/volumes/sample_ct.nrrd` — `816375cdcbb3a00abb87fcbd14075f78287aaf7e05eb751082b5c900f2df7865` (downsampled 128×128×70; source SHA256 `4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e`)

These are asserted by the T2 gate (SHA256 of each committed file, plus the NRRD
dims ≤128³ and the bunny.obj hand-counted vertex count).

### Asset persistence (V3 research — robust/cleaner/extensible)

V3 replaces the primitive `render::AssetRegistry::Slot{MeshGeometry}` keyed only
on `const data::Mesh*` (`byObject_` `render/asset_registry.hpp:137`) with an
extensible, typed, content-addressed, scoped store. The current `AssetRegistry`
remains for the V2 `MeshGeometry` path but is **not** the model for
`VolumeDataset`/`Image`/`Material`/`Light` assets. Research evaluated five
approaches and converges on the last:

| Approach | Key | Dedup | Shared across views/pages? | Lifetime / eviction | Extensible to new asset kinds? | Verdict |
|---|---|---|---|---|---|---|
| (A) **Generational index** (`Handle{index,generation}`) — today's `AssetRegistry` (`render/asset_registry.hpp:54`) | `index+gen` + `const data::Mesh*` pointer-identity (`byObject_`) | pointer-identity only | yes (global `byObject_`) | free increments `gen`, slot reuse — no ref-count, no page scope | one `AssetRegistry<MeshGeometry>` per kind, not extensible | **Kept for `MeshGeometry` only** — V3 does not expand it |
| (B) **Ref-counted handle + GC** | `shared_ptr<AssetData>` / intrusive `refCount` on handle | pointer-identity | yes — last reference drops GPU handle | `register` increments, `unregister` decrements, zero→free (or GC sweep) | any `T` with one `AssetStore<T>` | **Good for transient assets** but leaks if a cached `ReView` holds the last ref |
| (C) **Content-hash dedup** (`SHA256(bytes)+dims`) | `hash(voxelBytes)` / `hash(positions+indices)` | **content** — two `data::Mesh` with identical bytes but distinct pointers share one `MeshGeometry` | yes | hash map + ref-count | any hashable `T` | **Good for dedup**, but hash of a `128³` volume per frame is expensive — cache the hash at load time (`NRRD` header+raw `SHA256` already stored) |
| (D) **Typed persistent store with `(AssetId, type, contentHash, scope, version)`** | `AssetId` stable handle (`uint64_t` from `scene::SceneStore`) + `contentHash` + `LayoutId` scope | **content + id** — `(id,gen)` fast path, hash for imported-file dedup | page/layout scoped arena or global — caller chooses | `Scope` controls lifetime: `LayoutId`-scoped arena keeps FBOs/textures for the active layout and frees on layout switch; global `SceneStore` assets survive page swaps | fully extensible (`Mesh`, `Volume`, `Image`, `Material`, `Light`) | **Recommended for V3 `SceneStore`+`Broker`** |
| (E) **Copy-on-write immutable asset** (`Arc<AssetData>`) | immutable `AssetData` — mutation produces a new `AssetId` | content | yes — cheap clone shares `Arc` | new `id` on each edit; old `id`'s GPU handle lingers until its last `Re*Object` view drops | any immutable `T` | **Excellent for undo/redo and multi-page branching**, but requires `SceneStore` to issue new `id`s on every edit (versioning) |

**Chosen V3 design (will land in `scene/store` + `broker/`, web-verified EOL):**

- **`scene::SceneStore` owns `AssetId` handles** (`uint64_t` stable, `generation`+`contentHash` per `data::Mesh`/`VolumeDataset`/`Image` imported through `io/`) — hierarchical `Version:AssetId:Hash` with SHA-256 of canonicalized stable bytes at load time (not per-frame) + `contentHash` cache (System Overflow SHA-256 correctness + Dev Genius version-your-cache-keys 2025-12-25). The store is **hybrid** per SPEC §10.2/10.5: `SceneStore` global for assets (maximises content-hash dedup, pointer-identity `byObject_` replaced by `(AssetId,gen,hash)`), `ViewStore`/`LayoutStore` per-page for visibility (page-local `itemIds`). A mesh visible on page A but hidden on page B remains in global store, page B's `ReView` does not `items` it — one global entry, released when last `ReView` drops (ref-count `atomic` per Q35). **T7 binding (V3 anal review):** `VolumeDataset`/`Image` migrate **atomically** to `AssetId+contentHash` (global, `LayoutId`-scoped arena); `MeshGeometry` keeps **dual-key `byObject_` shim + `AssetId` in V3.6** with deprecation (shim removed V4) — avoids big-bang migration while still fixing hot-reload identity (Q3/Q28/Q35). See Q35/Q46 in `open_questions.md`.
- **`broker/` `Broker` holds the `Re*` cache** keyed by `CompositeKey{Version,LayoutId, AssetId/ObjectId, TypeIndex, Generation, ContentHash}` (SPEC §10.1, hierarchical `Version:LayoutId:Type:Hash` + SHA-256 at load time). `MeshObjectMapper::mapCached` probes `fieldGen==lastFieldGen && fieldHash==lastHash` (per-field gen split §10.4 — SRP/ISP per Clean Architecture Ch.7 + ICS SRP) — on hit returns cached `ReMeshObject` (same `AssetHandle`+`ReMaterial*`+`model+worldBounds`) without touching `AssetRegistry` or recreating `ReView::items_`. `AssetStore<T>` typed template (`AssetStore<Mesh>`, `AssetStore<VolumeDataset>`) per kind (SRP per `T`, OCP via template) — avoids per-kind duplicate. `Broker` holds mapper *registry* `type_index→IMapper`, `ViewSynchronizer` holds generation *cache* `CompositeKey→ReView` (SRP split).
- **`RE` topics:** `core::Texture3D` (`IRHITexture` via `IRHIContext::createTexture3D`) for volumes, `MeshGeometry` (`IRHIBuffer` via `IRHIContext::createBuffer`) for meshes (via existing `AssetRegistry` shim + new `AssetStore<Mesh>`), `Texture2D` (`IRHITexture`) for images — each `AssetStore<T>` with `atomic<uint32_t>` ref-count even under inline `IJobExecutor` (data-race-free per NFR §5; EOL Q37). `ReMaterial` via `MaterialMapper` SHA-256 value-hash dedup (identical `baseColor/shininess` share one `ReMaterial*`; `MaterialId` opt-in for shared themes — hybrid `variant<Desc,MaterialId>` cache, see §12.2). `Re*Object`s carry only derived GPU-ready values (`AssetHandle/ReMaterial*/Tex/ClipPlane/worldBounds/sliceUVW/normalMatrix` — §12.4 inventory) — never verbatim `data::Mesh` bytes (guardrail `asset_indirection` enforces RE-minimal; audit `asset_indirection` forbids `data::Mesh::positions` copy in `render/re_scene/`).
- **`RE`-minimal rule (SPEC §12.4) + EOL GC:** `LayoutId`-scoped arenas + LRU within arena + deferred GC until `compactHidden` or layout discard (not immediate free) — balances hidden-keep fast re-show (§10.5) vs memory ceiling (Q35). `core/rhi/` abstraction (`IRHIContext` — Qt QRhi/O3DE/Adept) keeps `render`/`broker` agnostic to GL vs Vulkan/Metal (DIP/OCP for EOL graphics API drift).

### Meshes (sample OBJs)
- `data/meshes/bunny.obj` — Stanford bunny, public domain.
- `data/meshes/teapot.obj` — Utah teapot, public domain.
- Both small enough to commit; used by the mesh + slice samples.

### Volumes
- `data/volumes/sample_ct.nrrd` — a small freely-licensed CT sample,
  downsampled to ≤128³ at setup (memory budget cap per §5), committed as NRRD.
- Tests use procedural synthetic volumes (analytic voxel fields) so expected
  values are closed-form.

### Fixtures
- `data/fixtures/` — hand-authored golden meshes/volumes/images with
  hand-counted acceptance constants (FR-io.1/2/3, FR-data.2).