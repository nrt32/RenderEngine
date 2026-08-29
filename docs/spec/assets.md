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
| Stanford bunny (OBJ) | `https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/8a4f8642acaf43f9cd7b67858a1502e1055ef202/data/stanford-bunny.obj` (commit `8a4f8642acaf43f9cd7b67858a1502e1055ef202` — `master` HEAD at `git ls-remote` 2026-08-29, SHA256 `1eb35d1e21ce99e5ce911353b6be278990713448dd9e8f5c9387f9de39b32205`) | Public domain | `data/meshes/bunny.obj` | sample mesh rendering; 2.4 MB |
| Utah teapot (OBJ) | `https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/8a4f8642acaf43f9cd7b67858a1502e1055ef202/data/teapot.obj` (commit `8a4f8642acaf43f9cd7b67858a1502e1055ef202` — `master` HEAD at `git ls-remote` 2026-08-29, SHA256 `1b5396fedd74b577e32cef41146582c2f2e1a050d5b4915193c0ac1ad4187ed4`) | Public domain | `data/meshes/teapot.obj` | sample mesh/slice rendering |
| CT chest sample volume | `https://github.com/Slicer/SlicerTestingData/releases/download/SHA256/4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e` (published SHA256 `4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e`, file `CT-chest.nrrd`) | CC-BY-SA 4.0 (Medical Decathlon) | `data/volumes/sample_ct.nrrd` | downsample ≤128³ → raw NRRD; used by volume/MPR samples + tests |
| Golden fixtures | Hand-authored small meshes/volumes/images | Project-owned | `data/fixtures/` | committed, tiny, used by io/data tests (hand-counted acceptance constants) |
| Golden 2×2 RGBA (T11b) | Hand-authored 2×2 RGBA (`red` `green` `blue` `white`, iteration 3 #2) — `sha256sum 9ccfc2abaa3984dc34c93aee16be0afa8a5e1395f25492b3df67897e6d00df10` | Project-owned | `data/fixtures/golden_rgba.png` | `T11b` `FR-io.3` pixel oracle `1/255` via `PlaneRenderer` — hand-authored, committed, no `curl`, verified SHA in `T11b` gate (iteration 3 #2/4) |
| Font atlas golden (T3a) | **Generated in-repo at `T3a`, not fetched at setup** — via `tools/generate_font_atlas.sh` `RE_SAMPLE_MAX_FRAMES=1 ./build/tests/re_tests --gtest_filter=*FontAtlas*` then `sha256sum data/fixtures/font_atlas_golden.rgba` — Project-owned, deterministic, byte-identical on re-run (spec-review #9 fixed circular fetch, iteration 1 #14 adds `tools/generate_font_atlas.sh` script, iteration 5 #3 pre-generated dummy `64×64` `16384 B` `sha256sum 74bc1f394723a260d6a8501fc2c499bc515588a915205c8ef01cfb5349d72f22` interim) | Project-owned | `data/fixtures/font_atlas_golden.rgba` | **T3a deliverable:** file appears at `T3a` via `tools/generate_font_atlas.sh` (dummy `64×64` pre-pin `sha256sum 74bc1f394723a260d6a8501fc2c499bc515588a915205c8ef01cfb5349d72f22` interim at `spec_review`, real FBO capture overwrites at `T3a` and re-pins) — **no setup fetch** — `T2` Verified SHA256s table now lists `74bc1f394723a260d6a8501fc2c499bc515588a915205c8ef01cfb5349d72f22` interim (overwritten at `T3a`); deterministic generation, no `comment_context.allow` waiver |
| Procedural geometry | Generated in code at runtime | n/a | n/a | deterministic tests; no file dependency |
| LICENSE (meshes) | In-repo (per-dataset-dir) | CC0 / Public domain (bunny+teapot) | `data/meshes/LICENSE` | per-dataset-dir LICENSE beside every dataset (audit `audit.sh` built-in `assets licensed per-dir` + T2 gate `test -f data/meshes/LICENSE`) |
| LICENSE (volumes) | In-repo (per-dataset-dir) | CC-BY-SA 4.0 (CT chest) | `data/volumes/LICENSE` | per-dataset-dir LICENSE beside every dataset (audit built-in + T2 gate `test -f data/volumes/LICENSE`) |

**Fetch method (two-phase: SETUP stages, T2 commits):** because assets are
committed, setup does NOT download into the repo at build time. The setup phase
(`/loop-setup`) downloads the pinned source files above with `curl -L --fail`, verifies `sha256sum -c`, runs
`python3 (>=3.10, stdlib only) tools/convert_nrrd.py` (downsample the CT to ≤128³ and re-write as raw NRRD),
and stages the results under `data/` — but does **not** commit. Committing the
assets, `LICENSE` per dataset dir (`data/meshes/LICENSE`, `data/volumes/LICENSE`), `data/README.md` (sources, URLs, licenses, checksums),
and recording the verified SHA256s in this section is the **T2 implementer's**
deliverable. Re-running setup is therefore idempotent and never touches git
state. Explicit fetch (idempotent):

```bash
curl -L --fail https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/8a4f8642acaf43f9cd7b67858a1502e1055ef202/data/stanford-bunny.obj -o /tmp/bunny.obj
echo "1eb35d1e21ce99e5ce911353b6be278990713448dd9e8f5c9387f9de39b32205  /tmp/bunny.obj" | sha256sum -c
curl -L --fail https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/8a4f8642acaf43f9cd7b67858a1502e1055ef202/data/teapot.obj -o /tmp/teapot.obj
echo "1b5396fedd74b577e32cef41146582c2f2e1a050d5b4915193c0ac1ad4187ed4  /tmp/teapot.obj" | sha256sum -c
curl -L --fail https://github.com/Slicer/SlicerTestingData/releases/download/SHA256/4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e -o /tmp/CT-chest.nrrd
echo "4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e  /tmp/CT-chest.nrrd" | sha256sum -c
python3 tools/convert_nrrd.py /tmp/CT-chest.nrrd data/volumes/sample_ct.nrrd  # downsample ≤128³, raw NRRD, python3 >=3.10 stdlib only
```

**Reproducibility:** `tools/convert_nrrd.py` is pinned to the repo commit (in-repo, stdlib-only, `python3 >=3.10`; no pip deps, no external hash) — verification is `python3 --version` and stdlib-only check plus `sha256sum tools/convert_nrrd.py` pin `cec7d6356631cbeaff1139f6ebcdbbad52b1d349dec2a74ad8b55c62b7668b56` (spec-review #2 fix) (reproducibility gate: re-running `python3 tools/convert_nrrd.py /tmp/CT-chest.nrrd` yields byte-identical `data/volumes/sample_ct.nrrd` SHA `816375cdcbb3a00abb87fcbd14075f78287aaf7e05eb751082b5c900f2df7865` — deterministic downsample, no numpy randomness); the derived `data/volumes/sample_ct.nrrd` SHA `816375cdcbb3a00abb87fcbd14075f78287aaf7e05eb751082b5c900f2df7865` is the binding artifact. `data/README.md` (sources, URLs, licenses, checksums) is also a T2 deliverable (see T2 doc-map).

**Verified SHA256s of the committed files (T2):**
- `tools/convert_nrrd.py` — `cec7d6356631cbeaff1139f6ebcdbbad52b1d349dec2a74ad8b55c62b7668b56` (in-repo, stdlib-only, spec-review #2 fix)
- `data/meshes/bunny.obj` — `1eb35d1e21ce99e5ce911353b6be278990713448dd9e8f5c9387f9de39b32205` (matches source)
- `data/meshes/teapot.obj` — `1b5396fedd74b577e32cef41146582c2f2e1a050d5b4915193c0ac1ad4187ed4` (matches source)
- `data/volumes/sample_ct.nrrd` — `816375cdcbb3a00abb87fcbd14075f78287aaf7e05eb751082b5c900f2df7865` (downsampled 128×128×70; source SHA256 `4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e`)
- `data/fixtures/golden_rgba.png` — `9ccfc2abaa3984dc34c93aee16be0afa8a5e1395f25492b3df67897e6d00df10` (hand-authored `2×2` RGBA `red/green/blue/white`, iteration 3 #2 `T11b` `FR-io.3` pixel oracle — `sha256sum` pinned, `PlanerRenderer` `1/255`)
- `data/fixtures/font_atlas_golden.rgba` — `74bc1f394723a260d6a8501fc2c499bc515588a915205c8ef01cfb5349d72f22` (iteration 1 #14 + iteration 3 #3 + iteration 5 #3 pre-generated dummy `64×64` RGBA `16384 B` deterministic `sha256sum` pinned at `spec_review` — `tools/generate_font_atlas.sh` will overwrite with real `RE_SAMPLE_MAX_FRAMES=1` FBO capture at `T3a` and `T3a` gate will `sha256sum` re-pin; byte-identical on re-run, no setup fetch — interim `TBD_T3a` replaced at iteration 5 #3 with dummy `64×64` pre-pin to make `spec_review.pass` reproducible pre-`T3a`; real atlas overwrites at `T3a`; pre-setup gate `sha256sum -c <<<"74bc1f394723a260d6a8501fc2c499bc515588a915205c8ef01cfb5349d72f22  data/fixtures/font_atlas_golden.rgba" && test $(wc -c < data/fixtures/font_atlas_golden.rgba) -eq 16384` verifies dummy before loop start, iteration 6 #6; after `T3a`: `sha256sum data/fixtures/font_atlas_golden.rgba` must be updated in `assets.md:60` and `T3a:T` — interim gate `74bc1f...` waived post-`T3a`, per iteration 8 #6)

These are asserted by the T2 gate (SHA256 of each committed file, plus the NRRD
dims ≤128³ and the bunny.obj hand-counted vertex count).

**Dependency pins (FetchContent `GIT_TAG`, verified SHAs — canonical table is `docs/spec/techstack.md` §2, spec-review #16 DRY):** this section mirrors `techstack.md` §2; `techstack.md` is the canonical pin table — `T16` gate verifies `grep -c "GIT_TAG" CMakeLists.txt` SHAs match `techstack.md` SHAs (spec-review #16, prevents drift). Pins:
- `glfw 3.4` — `https://github.com/glfw/glfw` — tag `3.4` (commit `7b6aead9fb88b3623e3b3725ebb42670cbe4c579`) — `zlib` — `GIT_TAG 3.4` (verified `CMakeLists.txt:72`, `git ls-remote` SHA `7b6aead`) — see `techstack.md:15`
- `glad2 v2.0.8` — `https://github.com/Dav1dde/glad` — tag `v2.0.8` (commit `73db193f853e2ee079bf3ca8a64aa2eaf6459043` full 40-char, short `73db193` — iteration 1 #6 pins full SHA, `GIT_TAG v2.0.8` verified `CMakeLists.txt:82` + `git ls-remote https://github.com/Dav1dde/glad v2.0.8` `73db193f853e2ee079bf3ca8a64aa2eaf6459043`) — MIT — see `techstack.md:14`
- `glm 1.0.1` — `https://github.com/g-truc/glm` — tag `1.0.1` (commit `0af55ccecd98d4e5a8d1fad7de25ba429d60e863`) — MIT — `GIT_TAG 1.0.1` (verified `CMakeLists.txt:93`, `git ls-remote` SHA `0af55cc`) — see `techstack.md:16`
- `imgui v1.92.9` — `https://github.com/ocornut/imgui` — tag `v1.92.9` (commit `01380c579715e62fb9a8d6ec0502c4ea83bfde6e`) — MIT — `GIT_TAG v1.92.9` (verified `CMakeLists.txt:102`) — see `techstack.md:17`
- `googletest v1.15.2` — `https://github.com/google/googletest` — tag `v1.15.2` (commit `b514bdc898e2951020cbdca1304b75f5950d1f59`) — BSD-3 — `GIT_TAG v1.15.2` (verified `CMakeLists.txt:111`) — see `techstack.md:18`
- `spdlog v1.14.1` — `https://github.com/gabime/spdlog` — tag `v1.14.1` (commit `27cb4c76708608465c413f6d0e6b8d99a4d84302` via `git ls-remote --tags https://github.com/gabime/spdlog v1.14.1` — iteration 4 #1) — MIT — `GIT_TAG v1.14.1` (verified `CMakeLists.txt:123`) — see `techstack.md:19`
- `stb_image` — `https://github.com/nothings/stb` — commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` — Public Domain — `GIT_TAG 2c980bb59875b0d32144a71867fbdebb2f77cd20` (verified `CMakeLists.txt:132`) — see `techstack.md:20`
- `nlohmann/json 3.11.3` — `https://github.com/nlohmann/json` — tag `v3.11.3` (commit `9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03`) — MIT — `GIT_TAG v3.11.3` (verified `CMakeLists.txt:141`) — see `techstack.md:21`
- Fetch method: `FetchContent` `GIT_TAG` + `GIT_SHALLOW TRUE` (deps_pinned, deps_pinned_no_branch); system `libglm-dev`/`nlohmann-json3-dev` not used — canonical pins in `docs/spec/techstack.md` §2, `CMakeLists.txt:48-120`.

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

> **Landed (T14, 2026-08-24):** the GPU-side store is now **one typed
> multi-kind store**: `render::AssetRegistry` covers all four asset kinds —
> `data::Mesh → MeshGeometry` (the original V2/T7 table), `data::VolumeDataset
> → core::Texture3D`, `data::Image → core::Texture2D` and `PhongMaterial
> value → canonical IMaterial` (new `GpuSlotTable` tables; the material kind
> hashes every Phong VALUE field — baseColor RGBA, specular RGB, shininess,
> ambient, diffuse — so identical parameters share one immutable canonical
> instance, the store side of the §12.2 `ReMaterial` dedup). Every slot is
> keyed by `(index, generation, contentHash)` and
> reference-counted: registering already-present content increments the slot's
> reference count; releasing decrements it; the GPU object is destroyed and its
> generation bumped (invalidation — every outstanding handle then resolves to
> typed error code 2) only when the last reference drops. Dedup is by content
> hash of stable bytes everywhere (no pointer-keyed maps remain in render/):
> two renderer instances sharing one dataset see one GL texture id, and
> identical-content distinct allocations alias to one slot. The former
> per-renderer caches (`VolumeRenderer::textureFor` /
> `PlaneRenderer::textureFor` maps, weak-observer keyed) are deleted; both
> renderers resolve lazily through the shared store (`lookupVolume` /
> `lookupImage` — find-or-upload without reference-count changes), defaulting
> to the process-wide instance (`AssetRegistry::shared()`, torn down by the
> test fixture via `resetShared()` while the GL context is current). The
> scene→RE material HAND-OFF (`MeshObjectMapper` translating `presentation`
> into a store-resolved material instead of the current null) remains §12.2
> `MaterialMapper` work tracked with the broker mapper inventory. The
> CPU-side identity layers are unchanged: `scene::AssetRegistry<T>` stays the
> GL-free typed registry template and `broker::AssetStore` the broker-side
> generational-handle skeleton; the hash values match byte-for-byte because
> both layers call the ONE GL-free definition in `data/content_hash.hpp`
> (`data::computeContentHash` overloads — scene forwards to it, the render
> registry calls it directly; neither layer includes the other, data/ is the
> shared GL-free root).
- **`scene::SceneStore` owns `AssetId` handles** (`uint64_t` stable, `generation`+`contentHash` per `data::Mesh`/`VolumeDataset`/`Image` imported through `io/`) — hierarchical `Version:AssetId:Hash` with SHA-256 of canonicalized stable bytes at load time (not per-frame) + `contentHash` cache (System Overflow SHA-256 correctness + Dev Genius version-your-cache-keys 2025-12-25). The store is **hybrid** per SPEC §10.2/10.5: `SceneStore` global for assets (maximises content-hash dedup, pointer-identity `byObject_` replaced by `(AssetId,gen,hash)`), `ViewStore`/`LayoutStore` per-page for visibility (page-local `itemIds`). A mesh visible on page A but hidden on page B remains in global store, page B's `ReView` does not `items` it — one global entry, released when last `ReView` drops (ref-count `atomic` per Q35). **T7 binding (V3 anal review):** `VolumeDataset`/`Image` migrate **atomically** to `AssetId+contentHash` (global, `LayoutId`-scoped arena); `MeshGeometry` keeps **dual-key `byObject_` shim + `AssetId` in V3.6** with deprecation (shim removed V4) — avoids big-bang migration while still fixing hot-reload identity (Q3/Q28/Q35). See Q35/Q46 in `open_questions.md`.
- **`broker/` `Broker` holds the `Re*` cache** keyed by `CompositeKey{Version,LayoutId, AssetId/ObjectId, TypeIndex, Generation, ContentHash}` (SPEC §10.1, hierarchical `Version:LayoutId:Type:Hash` + SHA-256 at load time). `MeshObjectMapper::mapCached` probes `fieldGen==lastFieldGen && fieldHash==lastHash` (per-field gen split §10.4 — SRP/ISP per Clean Architecture Ch.7 + ICS SRP) — on hit returns cached `ReMeshObject` (same `AssetHandle`+`ReMaterial*`+`model+worldBounds`) without touching `AssetRegistry` or recreating `ReView::items_`. `AssetStore<T>` typed template (`AssetStore<Mesh>`, `AssetStore<VolumeDataset>`) per kind (SRP per `T`, OCP via template) — avoids per-kind duplicate. `Broker` holds mapper *registry* `type_index→IMapper`, `ViewSynchronizer` holds generation *cache* `CompositeKey→ReView` (SRP split).
- **`RE` topics:** `core::Texture3D` (`IRHITexture` via `IRHIContext::createTexture3D`) for volumes, `MeshGeometry` (`IRHIBuffer` via `IRHIContext::createBuffer`) for meshes (via existing `AssetRegistry` shim + new `AssetStore<Mesh>`), `Texture2D` (`IRHITexture`) for images — each `AssetStore<T>` with `atomic<uint32_t>` ref-count even under inline `IJobExecutor` (data-race-free per NFR §5; EOL Q37). `ReMaterial` via `MaterialMapper` SHA-256 value-hash dedup (identical `baseColor/shininess` share one `ReMaterial*`; `MaterialId` opt-in for shared themes — hybrid `variant<Desc,MaterialId>` cache, see §12.2). `Re*Object`s carry only derived GPU-ready values (`AssetHandle/ReMaterial*/Tex/ClipPlane/worldBounds/sliceUVW/normalMatrix` — §12.4 inventory) — never verbatim `data::Mesh` bytes (guardrail `asset_indirection` enforces RE-minimal; audit `asset_indirection` forbids `data::Mesh::positions` copy in `render/re_scene/`).
- **`RE`-minimal rule (SPEC §12.4) + EOL GC:** `LayoutId`-scoped arenas + LRU within arena + deferred GC until `compactHidden` or layout discard (not immediate free) — balances hidden-keep fast re-show (§10.5) vs memory ceiling (Q35). `core/rhi/` abstraction (`IRHIContext` — Qt QRhi/O3DE/Adept) keeps `render`/`broker` agnostic to GL vs Vulkan/Metal (DIP/OCP for EOL graphics API drift).

### T7 — Asset identity V3.6 binding (SceneStore-owned `AssetId`)

**Binding (T7 V3.6):** `SceneStore` owns `AssetId{generation,contentHash}`
per `data::Mesh` / `data::VolumeDataset` / `data::Image` via typed
`AssetRegistry<T>` template (`scene/asset_registry.hpp`). No per-kind duplicate
— `AssetRegistry<Mesh>`, `AssetRegistry<VolumeDataset>`, `AssetRegistry<Image>`
share one template (SRP per `T`, OCP via template). `data::Mesh` stays pure
(no `AssetId` field — preserves `data` RE-agnostic for physics/UI). `SceneStore`
dedups by **content hash of stable bytes — the single canonical `data/content_hash.hpp` `re::data::computeContentHash` / `hashStableBytes` (FNV-1a 64-bit for skeleton/tests, SHA-256 truncated 64 bits for prod per SPEC §10.1; verbatim reference, not a second definition)** — two `Mesh` allocations with identical bytes share
the same `AssetId` (same `contentHash` → same `index+generation`). Hashed **at load/register time, never per frame** (header `data/content_hash.hpp:31` is source of truth; `persistence.md:26` references it verbatim).

`render::AssetRegistry` keeps `Slot{MeshGeometry}` generational `AssetHandle`
but keys by stable `contentHash` (`byHash_`) from `SceneStore` — not by
`byObject_` pointer `render/asset_registry.hpp:137` alone. The dual-key shim
`byObject_ + byHash_` is **deleted at `T7`** per the consolidated backlog binding (`TASKS.md:T7` `grep -c "byObject_" render/ ==0` — `byHash_` only, `V3.6` shim retired at `T7` not `V4`, spec-review #3 fix); `byHash_` content-hash IS identity. Stale `AssetId{gen+1}` → typed
`Error{code=2}` (never crash) — `AssetRegistry<T>::resolve` checks
`generation != slot.generation` → code 2 (`StaleHandle`) per SPEC §5.

`broker::AssetStore` likewise dedups by `contentHash` (T7) — same
`scene::computeContentHash` via ACL (`broker` may include both `scene/` and
`render/`).

**T7 owner-driven handles for volumes/images (landed):** broker mappers
(`VolumeObjectMapper`, `VolumeSliceObjectMapper`, `PlaneObjectMapper`/`PlaneMapper`)
register volumes/images through `SceneStore`/`broker::AssetStore`/`render::AssetRegistry`
at sync, handing renderers `VolumeTextureHandle`/`ImageTextureHandle` instead of
`shared_ptr<const T>`; renderers' `textureFor` becomes O(1) handle resolve
(`assets_->resolveVolume(handle)` / `resolveImage(handle)`), never per-frame
FNV-1a (`data/content_hash.hpp:31` hashed at load/register time, never per frame).
The lazy-hash `lookupVolume`/`lookupImage` insertion paths that recomputed FNV-1a
over every byte per instance per frame (`render/asset_registry.cpp:404,459` before T7)
are deleted (keep explicit `registerVolume`→`resolveVolume` / `registerImage`→`resolveImage` only);
the contract-violating comment about store-pinned `refs==0` lazy slots is removed.
This also closes R8a/R8b: pinned refs==0 lazy slots can no longer appear and the
`byObject_` pointer-key shim is **deleted at `T7`** (`grep -c "byObject_" render/ ==0`, spec-review #3) — content-hash IS identity for volumes/images. Direct-renderer
tests register explicitly in fixtures (or via the shared test helper
`registerVolume`/`registerImage`); no `legacyHandleCache_` fallback remains (`grep -c "legacyHandleCache" render/ ==0` per `TASKS.md:T7` binding — fallback deleted, explicit handle path is required). Gate asserts (explainable):
spy counter proves `hashStableBytes`/FNV executes zero times during a steady-state
60-frame loop after warm-up (volume + plane); registry slot count constant across
1000 distinct-image frames (no pinned-slot growth); same `VolumeDataset` registered
through two `VolumeRenderer` instances yields one `Texture3D`; suite green N>=3.

RE-minimal unchanged: `render/re_scene/` never copies `data::Mesh::positions`
(`asset_indirection`).

### Meshes (sample OBJs)
- `data/meshes/bunny.obj` — Stanford bunny, public domain.
- `data/meshes/teapot.obj` — Utah teapot, public domain.
- Both small enough to commit; used by the mesh + slice samples.

### Volumes
- `data/volumes/sample_ct.nrrd` — a small freely-licensed CT sample,
  downsampled to ≤128³ at setup for the *committed sample* (example `128×128×70`), but product loader has **no `≤128³` cap** — any dims via `core::Caps` tiled streaming (per Q4, `T11a` `core::Caps`); `BudgetExceeded` only on probe fail, not on `>128³` alone.
- Tests use procedural synthetic volumes (analytic voxel fields) so expected
  values are closed-form.

### Fixtures
- `data/fixtures/` — hand-authored golden meshes/volumes/images with
  hand-counted acceptance constants (FR-io.1/2/3, FR-data.2).