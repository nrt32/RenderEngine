# SPEC §12 — Materials and Lights — `scene/` ↔ `render/` hierarchies

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §12" mean this file. This section was added
> in the V3 spec update to make the material and light domains **even** across
> `scene/` disposition, `broker/` mediation, and `render/` execution.

## 12.1 Problem — `IMaterial` was a single concrete path, lights were absent

- Today `render::IMaterial → PhongMaterial` is the only hierarchy
  (`render/imaterial.hpp`, `render/phong_material.hpp:20`, `isTransparent()=a<1`),
  and `SliceRenderer` reuses `MeshInstance/IMaterial` (`render/slice_renderer.hpp:53`)
  while `VolumeRenderer` uses `TransferFunction` directly (`render/volume_renderer.hpp:58`).
  No `VolumeMaterial`/`SliceMaterial`/`ContourMaterial`, no `PBR` sibling, no
  `ILight`.
- App-side `MaterialDesc` / `LightDesc` did not exist — `app` could not drive
  appearance or lighting without including `render/` types (`app/mpr_sample.cpp:62`
  includes `render/phong_material.hpp` to set a hard-coded `baseColor`).
- `MeshRenderer` hardcodes `shade=max(dot(n,(0,0,1)),0)` (`render/mesh_renderer.cpp:53`).

> **V3.7 (T8) — Phong-only stays (deferred, binding).** This iteration keeps
> `render::IMaterial → PhongMaterial` single path (FR non-goal `SPEC §1` —
> PBR deferred) and no `ILight` (fixed headlight `max(dot(n,(0,0,1)),0)` in
> `MeshRenderer` stays). Even hierarchies `IColor/IVolume/ILineMaterial +
> PBR/SliceMaterial/ContourMaterial` (§12.2) and `Directional/Point/Spot`
> (§12.3) plus `render/material/` and `render/light/` files are **deferred**
> — headers not added this iteration; no new `render/material/` files
> (gate `G` enforces). `MaterialDesc`/`LightDesc` remain `app`-local free
> structs for the `MPR` sample. This task only tightens the
> `TransferFunction` vs `VolumeMaterial` boundary — `TransferFunction` stays
> **beside** `VolumeMaterial` in `VolumePresentation` (`VolumeMaterial` +
> `TransferFunction` + `stepLength` + `shading`, already decided §12.5);
> the renderer-side TF stays a field of `VolumeInstance` strictly separate from
> the dataset (T13: owned by value — the separation, not the pointer shape, is
> the §12.5 invariant).
> `PhongMaterial isTransparent ⇔ baseColor.a < 1` preserves `FR-render.2/3`
> transparency gates.

## 12.2 `IMaterial` hierarchy — RE-shaped, app disposition mirrors but stays RE-free (SOLID LSP/ISP — binding)

> **V3.2.1 SOLID verdict — IMaterial was LSP/ISP violation (research-backed, web-verified 2026-08-23).** The prior `IMaterial{ baseColor(), isTransparent() }` forced `VolumeMaterial` to implement `baseColor()` where no base color exists (volume's appearance is `TransferFunction` ramp, not a uniform RGBA; `baseColor.a` postcondition `isTransparent ⇔ baseColor.a<1` does not hold for volumes). Per LSP (Barbara Liskov: subtype must preserve supertype contract; invariants + postconditions cannot be weakened/strengthened — Wikipedia LSP + TechWayFit Rectangle-Square + Baeldung LSP "preconditions cannot be strengthened, postconditions cannot be weakened" + LogRocket LSP 2025-06-06 + StackOverflow 79132898 LSP pre/post invariants), `VolumeMaterial` substituting for `IMaterial` breaks callers expecting `baseColor` semantics (Rectangle-Square classic violation: Square strengthening invariant `w==h` breaks `Rectangle::setWidth` postcondition). Per ISP (Robert C. Martin: "no client should be forced to depend on methods it does not use" — NDepend ISP + Baeldung ISP + code-note-vr ISP), `VolumeMaterial` is forced to depend on `baseColor` it never uses — fat interface (`MultiFunction` printer anti-pattern per code-note-vr + Baeldung `Worker`→`Workable`/`Feedable`; NDepend `ICollection` vs `IReadOnlyCollection`). Same applies to `SliceMaterial`/`ContourMaterial` needing `specular/shininess`. Web research verdict: split the fat `IMaterial` into role interfaces (see Baeldung ISP `Worker` → `Workable`/`Feedable`; code-note-vr ISP `IMultiFunction` → `Printable`/`Scannable`; NDepend ISP). **Variant vs hierarchy OCP (binding):** `scene::MaterialDesc` uses `std::variant` (see §12 App) — variant is open for operations (new visitors) but closed for types (variant list edit — Here Be Braces 2020-06-26); polymorphic `IMaterial` hierarchy is opposite. V3 chooses variant for `MaterialDesc` because type set is closed/small (4) while operations (visitors/dispatch) vary more; polymorphic hierarchy retained for `render::IMaterial` concrete tree where new `ToonMaterial` = new file + visitor overload once, zero edits to existing concrete mappers (OCP via `Broker::registerMapper`).

### RE (`render/material/` — role-segregated, OCP-open):

> **Deferred (T8 V3.7):** `render/material/` hierarchy (`IColorMaterial`/
> `IVolumeMaterial`/`ILineMaterial` + `PBR`/`SliceMaterial`/`ContourMaterial`)
> stays **spec-only** this iteration — no headers under `render/material/`
> landed; `render::IMaterial → PhongMaterial` single path preserved (Phong-only
> non-goal SPEC §1). TransferFunction vs VolumeMaterial boundary tightened
> per §12.5 decision only.

```
render::IMaterial                      // MINIMAL, stable abstraction (DIP-owned by render/policy) — ONLY isTransparent() + kind()
  // isTransparent() is the SOLE method on the root; every concrete can fulfil it (LSP-preserving).
  // kind() returns MaterialKind enum for debug/dispatch, not for switching in renderers (renderers depend on concrete type via IRenderer<MatT> overloads — OCP).

render::IColorMaterial  : IMaterial    // role interface for color-uniform materials (ISP segregation)
render::IVolumeMaterial : IMaterial    // role interface for TF-driven materials
render::ILineMaterial   : IMaterial    // role interface for line/edge materials

Concrete tree (one file per concrete, OCP-open — new Toon/HairMaterial adds a file, no edit to existing):

render::IMaterial
 ├─ render::MeshMaterial  (abstract, : IColorMaterial)   // introduces cull/doubleSided/alphaMode
 │    ├─ render::PhongMaterial  (ambient/diffuse/specular/shininess, baseColor.a drives isTransparent)
 │    └─ render::PBRMaterial    (albedo/metallic/roughness/occlusion, IBL env if present)
 │    └─ (future) ToonMaterial, HairMaterial … — open for extension, closed for modification
 ├─ render::VolumeMaterial  : IVolumeMaterial        // owns ReTfUniforms (via TransferFunctionMapper) + stepLength + shading toggle; used by VolumeRenderer/VolumeSliceRenderer — NO baseColor()
 ├─ render::SliceMaterial   : IColorMaterial         // cap/contour shading: closed-surface fill + cap color/roughness
 └─ render::ContourMaterial : ILineMaterial          // line/edge: color, stipple, width — used by MeshSlice contour layer (second IRenderable in a 2D ReView) — NO specular/shininess

// Dispatch: renderers take concrete *Material, not IMaterial (avoids LSP downcast). MeshRenderer::drawLayer takes const MeshMaterial*; VolumeRenderer takes const VolumeMaterial*; clients hold unique_ptr<IMaterial> polymorphically but dispatch via std::visit / IRenderer<MatT> overload (see §11.3 Broker visitor).
```

- `ReMeshObject` carries `const MeshMaterial*` (today a shared
  `IMaterial` handle — `std::shared_ptr<IMaterial>`, T13 ownership discipline;
  would tighten to `MeshMaterial*` to avoid LSP downcast when the deferred
  hierarchy lands), `ReVolumeObject` carries `VolumeMaterial*` (no `baseColor` field), `ReMeshSliceObject` carries `SliceMaterial*` *or* `MeshMaterial*` depending on the closing decision (see §12.2 choice below), `ReContourLine` carries
  `ContourMaterial*`. A `ReView` that holds `VolumeSlice+MeshSlice` thus holds
  `VolumeMaterial`+`SliceMaterial` without sharing a base unsafely — and without LSP substitution hazard because no caller holds a `VolumeMaterial` as an `IColorMaterial`.
- `isTransparent()` remains on `IMaterial` (root, LSP-safe: every material has a transparency predicate, postcondition consistent), `doubleSided`/`needsOIT` live on `IColorMaterial`/`MeshMaterial` only (ISP: `IVolumeMaterial` never exposes `doubleSided`; `ILineMaterial` never exposes `needsOIT`). The source-authoritative `bool transparent`
  is `scene::MeshMaterialDesc::baseColor.a<1` or `scene::VolumePresentation::tf` alpha ramp — `*Mapper` derives the RE flag per-material-kind (no fat base; TF beside mat per §12.5).

### App (`scene/material/` — mirrors RE roles, GL-free, variant OCP binding):

```
scene::MaterialDesc  variant< MeshMaterialDesc, VolumeMaterialDesc, SliceMaterialDesc, ContourMaterialDesc >  // variant keeps scene/ header-only, no vtable (Here Be Braces variant vs virtuals)
  MeshMaterialDesc   variant< PhongDesc, PBRDesc, … >  { vec4 baseColor, vec3 specular, float shininess, float ambient, diffuse; bool doubleSided; }  // IColorMaterial source
  VolumeMaterialDesc { float densityScale; bool shading; }  // TF NOT inside — TransferFunctionDesc lives **beside** VolumeMaterialDesc in `VolumePresentation{VolumeMaterialDesc mat; TransferFunctionDesc tf; float stepLength; bool shading;}` per §12.5 binding (ISP: TF has its own cache key/lifecycle `TransferFunctionMapper`; bundling would dirty MaterialMapper on TF edit — see §12.5)
  SliceMaterialDesc  { vec4 capColor; bool capping; }   // IColorMaterial source — distinct per §12.2 choice (capping not on MeshMaterial — ISP)
  ContourMaterialDesc{ RgbaColor color; float lineWidth; uint16_t stipple; } // ILineMaterial source — distinct (ISP: linetype not on mesh)

// Variant vs base+kind vs polymorphic hierarchy (OCP+ISP binding, web-verified):
// V3 fixes on variant (Here Be Braces 2020-06-26: variant open for operations/new visitors, closed for types; polymorphic hierarchy open for types/new derived classes, closed for operations).
// Variant chosen because app type set is closed/small (4) and operations (MaterialMapper visitor dispatch, dedup hashing) vary more than types; adding new desc adds a variant alternative + one new *Mapper file + one visitor overload (one file edit) but zero edits to existing descs/mappers — OCP via Broker visitor. base+kind (enum+switch) would be OCP violation (Meyer PV) and is rejected. Polymorphic hierarchy (variant alternative = distinct class hierarchy) retained for render:: concrete IMaterial tree where new ToonMaterial = new file open for extension.
// Trade-off documented: variant edit cost (one alias + one overload) is bounded and acceptable per Nordvarg 2025 variant+visit ("use variant when set is closed, use virtuals when hierarchy must be extended by external modules"). Future EOL with many kinds can migrate to polymorphic hierarchy without touching ViewMapper.
```

- **Identity (resolved §12.5 decision — value-hash vs MaterialId):** `scene::MaterialDesc` is **by-value in** the owning `App*Object` (`scene::MeshObject { MaterialDesc mat; }` — simple, copy) as the **default** (SRP: one reason to change = that object's appearance; OCP: no central `MaterialStore` that must be edited for new kinds). The store path `MaterialId → MaterialStore` is retained as an **opt-in** for shared editing (e.g., global theme edits) — `MaterialStore` is then a separate `scene::MaterialStore` with `MaterialId{generation}` and `Broker`'s `MaterialMapper` value-hash dedup (`hash(baseColor,shininess,…)`) shares one `ReMaterial*` across objects with identical desc (like `AssetRegistry::byObject_` but content-hash, not pointer-identity — see SPEC §7 / vulkan-guide caching). The implementer's `MaterialMapper` MUST document whether it dedups by **value hash** (identical `baseColor/shininess` share one `ReMaterial*`) or by **id** reuse.
  A mutation (`shininess 32→64`) dirties **only** `MaterialMapper` when by-value-plus-hash; with `MaterialId` it dirties `MaterialStore` and every `MeshObjectMapper` referencing it. The two paths are not mutually exclusive — value-hash is the LHS for anonymous per-object materials; `MaterialId` is the RHS for shared theme materials; the visitor `std::visit(overloaded{ [&](MeshMaterialDesc& m){…}, …})` handles both (OCP).

### Choices resolved (V3 spec-review, web-backed):

- `SliceMaterial` **distinct** (not reuse `MeshMaterial` for caps) — keep distinct (`SliceMaterial` adds `capping`; reuse would force `MeshMaterial` to carry `capping` it never uses — ISP fat interface + LSP postcondition weakening). Cost is one extra `SliceMaterialMapper` file (one file per type guardrail, OCP-open) — worth it for EOL clarity.
- `ContourMaterial` **distinct** line material (not `MeshMaterial` reuse) — distinct is required (`ContourMaterial` carries `lineWidth/stipple` which `Phong` does not; `MeshMaterial::shininess` meaningless for lines — ISP violation). Same file-cost argument.

## 12.3 Light — per-`View`, many per `View`, multiple types, same `broker/` abstraction (SOLID LSP/ISP — binding)

> **Deferred (T8 V3.7) — amended V5 T15:** `ILight` full hierarchy (`Point`/`Spot` + `render/light/` 3-type headers) stays **spec-only** — no `render/light/` hierarchy landed this iteration beyond the **minimal V5 `scene/light.hpp` single-struct + `broker/light_mapper` exception** (see `docs/spec/goals.md` §1 updated + `TASKS.md T15`). V5 minimal `Light{Directional, dir}` (`scene/light.hpp`) + `Engine::setLights` + `broker/light_mapper.hpp → render/light.hpp` upload is **IN-SCOPE** (empty `lights` = fixed headlight `max(dot(n,(0,0,1)),0)` preserved, one `Directional` shifts `diffuse` ≥5/255). Full `ILight` (`Directional/Point/Spot` + `render/light/` headers) remains deferred; `LightDesc` remains `app`-local for `MPR` sample beyond the minimal `scene/light.hpp` (V5 exception).

> **V3.3.1 SOLID verdict — ILight was LSP/ISP violation (web-verified).** Prior `ILight{ dir/pos/radius … }` forced `DirectionalLight` to implement `pos` or `PointLight` to throw on `dir` — classic `Bird::fly`/Ostrich (LSP: subclass cannot fulfil `fly()` — code-note-vr LSP 2026-06-22 Bird/Ostrich) / `Worker::eat` (Baeldung ISP fat `Worker` → `Workable`/`Payable`; NDepend `ICollection` vs `IReadOnlyCollection`) LSP/ISP violation (LSP "throw UnsupportedOperationException" anti-pattern — Baeldung LSP, Wikipedia LSP history). `ILight` fat also violates ISP: `DirectionalLight` forced to depend on `pos`/`attenuation` it never uses (Baeldung ISP). Web verdict: `ILight` must be **role-segregated** marker (`isEnabled()+kind()` only), per-type data only in concrete, dispatch via `std::variant` + `std::visit` (same as §12.2; ISP segregates `IColorMaterial`/`IVolumeMaterial`). Variant OCP trade-off same as §12.2 (closed type set 3 lights, bounded per SPEC §1).

### App (`scene/light/`):

```
scene::LightDesc  variant< DirectionalLightDesc, PointLightDesc, SpotLightDesc >  // OCP-open: new AreaLightDesc adds variant alternative + one mapper file; no edit to existing descs
  DirectionalLightDesc { vec3 dir;   RgbaColor color; float intensity; bool castShadows; }  // dirWS pre-normalised world-space (see §12.5 view vs world)
  PointLightDesc       { vec3 pos;   RgbaColor color; float intensity; float radius; float attenuation[3]; bool castShadows; }
  SpotLightDesc        { vec3 pos, dir; RgbaColor color; float intensity; float innerCone, outerCone, radius; bool castShadows; }
  // (future) AreaLightDesc { vec3 pos; vec2 size; ... } — OCP via variant extension

// Per-View light composition — a View owns its lights the way it owns its plane:
scene::View::lights  vector<LightDesc> inline  — **INLINE by default (SRP-favouring)** — a light tweak dirties only LightMapper cache via generation, not whole View (see broker cache §10.4 per-field gen split; vector<LightId> indirect is the opt-in for shared lights, same tradeoff as MaterialId — see §12.2). V3 fixes inline as default; LightId path retained for shared-light themes.
// DIP: scene::View owns vector<LightDesc> values (no LightStore dependency); Broker resolves per-element via LightMapper visitor.
```

- `scene::View::lights` is the source of truth, just like `scene::View::plane`.
  `lights` is **many per View** (your requirement). A `View` that holds
  `lights=[dir0, point1, spot2]` + `plane` + `itemIds` is `3D` shaded;
  a `2D View` with `plane` present may be **unlit** (`lights` empty or
  `shading=false` in its `VolumeSlice` presentation) — View's `plane`
  presence does **not** implicitly imply `lights.empty()` but the default
  `ViewMapper` should treat `2D → shading=false` unless overridden.

### RE (`render/light/` — role-segregated, LSP-preserving):

```
// Minimal root (DIP-owned by render/policy) — ONLY isEnabled/intensity/color type, no per-light geometry:
render::ILight  // minimal: virtual ~ILight(); virtual LightKind kind() const; virtual bool isEnabled() const; — concrete data NOT on base (ISP/LSP safe)
 // Per-type role data lives ONLY in concrete (no fat base throwing UNSUPPORTED — LSP safe):
render::DirectionalLight : ILight { vec3 dirWS; vec3 color; float intensity; bool castShadows; }
render::PointLight       : ILight { vec3 posWS; vec3 color; float intensity; float radius; vec3 attenuation; bool castShadows; }
render::SpotLight        : ILight { vec3 posWS, dirWS; vec3 color; float intensity; float innerCone, outerCone, radius; bool castShadows; }
 // Dispatch is via variant ReLight = variant<DirectionalLight,PointLight,SpotLight> + std::visit (OCP) — no switch on base pointer with dynamic_cast (LSP anti-pattern).

// Storage: ReView holds vector<ReLight> values (variant values), not vector<unique_ptr<ILight>> — ISP/LSP both favour value-variant over polymorphic base where possible (no slicing, no forced heap, OCP via variant alternative).
```

- `ReView::setLights(vector<ReLight>)` pushes `uLightCount/uLightPos/uLightColor/...`
  uniforms **once** before iterating `items[i]->drawLayer(camera, target)` —
  not per-`ReMeshObject` (per-object lights would duplicate uniforms and
  break `MeshRenderer::opaqueProgram` `uniform` setup `render/mesh_renderer.cpp:32`).
  `ViewMapper` translates each `scene::Light` via `LightMapper` → `ReLight`
  and `ReView::setLights` uploads them.
- Shadows/attenuation/castShadows are reserved as `LightDesc` fields now.

### Mappers (SOLID — one file per type, visitor dispatch, ISP/DIP — web-verified)

- `TransferFunctionMapper : IMapper<volume::TransferFunction, ReTfUniforms>` — **separate from `VolumeMaterialMapper`** (ISP: `VolumeMaterial` must NOT own `TransferFunction` — decided §12.5 keeps them separate in `VolumePresentation` composition; TF has its own cache key/lifecycle + generation per §10.4 — OCP: `stepLength` changes don't dirty TF cache, TF edit doesn't dirty `VolumeMaterial` — Baeldung ISP `Worker` segregation).
- `MaterialMapper` is **dispatch-only** (`IMapper<scene::MaterialDesc, unique_ptr<IMaterial>>` via `std::visit(overloaded{Phong,PBR,Volume,Slice,Contour})` → delegates to `PhongMapper`/`PBRMapper`/… per-subtype mappers, each `IMapper<SpecificDesc, unique_ptr<SpecificRe>>`). No central switch/enum edited for new material (OCP via `Broker::registerMapper` + visitor; variant trade-off documented §12.2 — Here Be Braces). Similarly `LightMapper` dispatches `scene::Light` variant via visitor to `DirectionalLightMapper`/`PointLightMapper`/`SpotLightMapper` (one file per subtype, Broker holds one per alternative — OCP PV at Broker boundary per NDepend).
- Each `*Mapper` is `ICachedMapper` where stateful (material/light dedup cache + `generation+contentHash` per §10.4) and `IMapper` where stateless (TF — pure hash of stable bytes, no gen). `ViewMapper` composes `CameraMapper + PlaneMapper + LightMapper[] + per-item mappers` → `ReView` (see `broker.md` §11.4) via `Broker::get<AppT>` generically — adding a new light/material type requires **zero** edits to `ViewMapper` (OCP) — only new `*Mapper` file + one `registerMapper` + one visitor overload (bounded variant edit). **DIP:** mappers depend on `IMaterial`/`ILight` abstractions, not concrete `PhongMaterial`/`DirectionalLight` beyond their own file (IJobExecutor behind `MaterialMapper` value-hash dedup keeps thread-safety — NFR §5). **SRP:** `MaterialMapper` single responsibility is variant dispatch; per-subtype mapper single responsibility is one `Desc→Re` translation (one file per mapper guardrail `broker_per_type`).

## 12.4 RE-direct types rule (your "RE should only have data types directly needed in RE, intelligently, yet another question mark" — SRP/ISP/DIP)

- **Guideline (SRP/ISP):** if an app field needs conversion (voxel-index→world, `scene::MaterialDesc→ReMaterial`, `scene::Light→ReLight`, `TransferFunction→uniforms`), the **converted** field is `Re*`, not the app struct stored verbatim (RE-minimal: violates ISP if `Re*` exposed verbatim app types — `render/re_scene/` must not store `data::Mesh::positions` copy; guardrail `asset_indirection` enforces). Fields that are already GL-uniform-ready (e.g., `mat4 model`, `Aabb worldBounds` derived) are shared as-is (derived uniform-ready per inventory §12.4). RHI abstraction: `Re*` carries `IRHIBuffer`/`IRHITexture` handles via `core/rhi/`, not raw `gl*` names — DIP (O3DE RHI/QRhi).
- **Question mark retained:** the boundary is **per field**, not per struct — `ReMeshSliceObject::plane` *must* exist as world `ClipPlane` (`render/slice_renderer.hpp:48`) even though `scene::MeshSliceObject` has no plane field (plane is on `View` per §11.4 — View owns the plane, not the item; `TranslateContext{ViewContext, optional<VolumeContext>}` supplies it); `ReVolumeSliceObject::sliceUVW` is derived from `ClipPlane` precomputed to avoid per-frame shader math; `Re*Object`s may intentionally duplicate `worldBounds` derived from `scene::MeshObject`'s `transform`+`data::Mesh::bounds()` (derived field, not verbatim copy — RE-minimal rule). `normalMatrix` derived if `model` non-uniform scale belongs in `Re*` (shader uniform, not app field).
- This section is the **source of truth** for the per-object `Re*` field
  inventory the Phase-1 implementer must enumerate before writing
  `render/re_scene/*.hpp` — produce binding inventory `docs/re_scene_inventory.md` (see TASKS T9) listing every `Re*` field with rationale `derived|uniform-ready|handle` per SPEC §12.4; audit `asset_indirection` must pass (no `data::Mesh` copy in `render/re_scene/`).

## 12.5 Resolved decisions (V3 spec-review) + remaining grill

- **★ Resolved: `VolumeMaterial` vs `VolumePresentation{VolumeMaterial, TransferFunction, stepLength}` — KEEP SEPARATE** — `TransferFunction` lives **beside** `VolumeMaterial` in `VolumePresentation{ VolumeMaterialDesc mat; TransferFunctionDesc tf; float stepLength; bool shading; }`. Rationale: ISP (forcing `VolumeMaterial` to own `stepLength`/`shading` + TF dirty couples volume shape with sampling policy — Baeldung ISP fat `Worker` segregation; NDepend ISP) + OCP (`TransferFunctionMapper` cache key `ReTfUniforms` differs from `VolumeMaterialMapper` key — System Overflow hierarchical `Version:Type:Hash`; bundling forces `MaterialMapper` dirty on TF edit and vice versa) + existing renderer boundary (`VolumeInstance` today carries the `TransferFunction` as its own field, strictly separate from the dataset — `render/volume_renderer.hpp` — keep that separation; T13 made the field owned by value, which does not change the boundary; `VolumePresentation` is composed by `volume_object_mapper` who composes both mappers via `TranslateContext`). `ReVolumeObject` carries `VolumeMaterial*` + `ReTfUniforms` separately (see §12.2). Decision is binding for `scene/` + `broker/` header review — reopen only with audit-level justification and web-backed LSP/ISP proof.
- **★ Resolved: `SliceMaterial` owns `capping`** — `capping` is a material trait (closed surface caps' appearance), not an object trait; `scene::MeshSliceObject::capping` would force `MeshSliceObject` to switch on a mesh flag (ISP — Baeldung `Worker`→`Workable`/`Feedable`; mesh object would depend on `capping` it never uses). `SliceMaterial{capping, capColor}` stays distinct (see §12.2; one file per mapper guardrail, OCP-open).
- **★ Resolved: `MaterialMapper` dedup — value-hash by default, `MaterialId` opt-in (ISP/SRP — web-verified):** `MaterialMapper` dedup is **SHA-256(content-hash) of canonicalized desc bytes** (hash at `SceneStore` mutation time, not per-frame; System Overflow SHA-256 correctness + Dev Genius versioning) — identical `baseColor/shininess` share one `ReMaterial*` across objects (like `AssetRegistry::byObject_` but content-hash, not pointer-identity — Vulkan Guide caching; Horizon Engine). `MaterialId → MaterialStore{generation}` opt-in retains for shared theme edits (global edits dirty `MaterialStore` + every `MeshObjectMapper` referencing it; inline `Value` dirties only `MaterialMapper` — per-field gen split §10.4). V3 headers must document `MaterialMapper::dedupKey` (value-hash) and the `MaterialId` path as theme-shared opt-in. Hybrid `Value+Id` dedup table `map<variant<Desc,MaterialId>, ReMaterial*>` keeps one cache without fragmentation (see open grill below). (Resolves §13 Q35 for materials.)
- **★ Resolved: `LightMapper` world space (SRP, web-verified)** — `scene::Light::pos/dir` are **world-space** (`dirWS`/`posWS` in `ReLight`); `LightMapper` just forwards (SRP: mapper single responsibility is translation, not view-space multiplication). Directional `dir` view-space needs are **not** a `LightMapper` concern — view-space conversion belongs to the shader's `uView` multiply, not to the mapper (which stays SRP-pure per Clean Architecture "one actor" — ICS SRP). `TranslateContext{ViewContext{viewMatrix}}` is available but unused by default (documented as contextual opt-in for future `ViewSpaceLight` variant via `ViewContext` role — §11.4 ISP-segregated). `PlaneMapper` context is unified `TranslateContext{ViewContext, optional<VolumeContext>}` — not via ad-hoc `ITranslator::translateWithContext`; OCP via `PlaneMapper`+`SpaceConverter` (see §11.4.2).
- **★ Resolved: `IJobExecutor` behind `MaterialMapper`/`LightMapper` (EOL, §13 Q37):** Inline `IJobExecutor` fallback keeps material/light dedup hash under 1-thread determinism; future `ThreadPoolExecutor` can parallelize dedup without touching `broker/` (OCP via an interface extension — since the persistence-honesty task the seam ships `execute()`-only, and the batched form arrives with a real pool consumer rather than as unused scaffolding).
- **Remaining grill (must pin before V3.7 header review):** None — all §12 Qs resolved binding 2026-08-23 (Q27 RE-minimal, Q42 variant keep). `ContourMaterial`/`SliceMaterial` distinct `ShaderProgram` branches is the binding direction (ISP per `ILineMaterial` vs `IColorMaterial`); shader branch inventory `render/material/shader_table.md` remains a V3.7 predecessor (enumerate each `ReMaterial→ShaderProgram` for `Phong/PBR/Volume/Slice/Contour`) but no longer blocks kickoff.
- **Resolved (Q35):** `MaterialMapper` dedup granularity for mixed inline-vs-id paths — single `unordered_map<variant<MaterialDesc,MaterialId>, ReMaterial*, DescHash>` (`DescHash`=SHA-256 canonical bytes, `Id` hash=`MaterialId{gen}`) — one cache, no fragmentation (see Q35). Hybrid table is doc'd in `broker/material_mapper.hpp`.
