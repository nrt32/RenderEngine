# NAMING_CONVENTIONS — RenderEngine

Accepted during `/loop-elicitation`. Hard conventions for all code in this
project.

## 1. Constants / linkage (hard, user-mandated)
- `constexpr` / `const` constants are defined in **`.cpp`** files by default.
- They move to a header ONLY when needed in **≥ 2 translation units**.
- Exception: template constants, and class-scope constants that are part of the
  public interface.
- Enums that are public API live in headers (they are types, not values).

## 2. File & module naming
- Headers `snake_case.hpp`, sources `snake_case.cpp`; one primary class per
  file, named after the class (`phong_material.hpp` → `PhongMaterial`).
- Module directories (lowercase): `io/ data/ volume/ scene/ core/ broker/ utils/ render/ app/ tests/` (`scene/` GL-free value lib, `broker/` only lib that may include both `scene/`+`render/`, `utils/` offscreen context + pixel reader — see `docs/spec/modules.md` §3).

## 3. Type names
- Classes/structs/enums/aliases: `PascalCase` (`PhongMaterial`, `VolumeDataset`).
- Interfaces/abstracts: `I` prefix (`IMaterial`, `ITransparencyPipeline`).
- Template type params: single uppercase letter or `PascalCase`.

## 4. Functions / methods
- Free functions and methods: `camelCase` (`computeFaceNormal`, `loadVolume`).
- Getters: `const`, `camelCase`, no `get` prefix (`bounds()`, `isTransparent()`);
  setters `setX(...)`.
- Booleans phrased as questions: `isTransparent()`, `hasNormals()`.

## 5. Variables
- Locals: `camelCase` (`maxSampleCount`).
- Class members: trailing underscore `camelCase_` (`modelMatrix_`); no `m_`,
  no pervasive `this->`.
- Parameters: `camelCase`, no trailing underscore.
- No Hungarian prefixes; the type carries pointer/reference-ness
  (`const Mesh* mesh`).

## 6. Namespaces
- One namespace per module under root namespace `re`:
  `re::io::`, `re::data::`, `re::volume::`, `re::scene::`, `re::core::`,
  `re::broker::`, `re::render::`, `re::app::`.
- **`scene/` naming:** types inside `re::app::` carry **no `App` prefix**
  (`app::MeshObject`, `app::Camera`, `app::View` — the namespace **is** the
  prefix). The RE mirror in `re::render::` uses `Re` where needed for grep
  distinctness (`ReMeshObject`, `ReView`) or keeps `render::` qualification
  where both are included (only `broker/` includes both).
- **`broker/` naming:** per-type translators are **`Mapper<AppT,ReT>`**
  (`IMapper` interface, `Broker` registry, `ViewBridge` façade — see
  `docs/spec/broker.md` SPEC §11 for the naming table). Aliases `Binder`,
  `Exchange`, `Adapter`, `Converter`, `Synchronizer` are recorded as
  alternatives but **one** name is chosen on the implementing branch and
  bulk-updated.
  - `IMapper<AppT,ReT>{map(Ctx)}` pure vs `ICachedMapper:IMapper{mapCached,invalidate}`
    ISP-split — one file per mapper (`camera_mapper.*`, `mesh_object_mapper.*`,
    `view_bridge.*` etc.; `ViewBridge` is coordinator not mapper — guardrail
    `broker_per_type`), `Broker{registerMapper<T>(unique_ptr<IMapper<T>>), get<T>()}`
    keyed by `std::type_index` (OCP — no `enum` switch), `IViewBridge{sync,renderAll,
    presentAll}` façade composing `ViewSynchronizer` (cache/dirty) + `ViewCompositor`
    (dispatch/present) SRP-split (T3 V3.2b). App never holds `IMapper`; only
    `IViewBridge` (DIP).
- **`render/` naming:** material hierarchy is
  `IMaterial → MeshMaterial/VolumeMaterial/SliceMaterial/ContourMaterial → PhongMaterial/PBRMaterial`
  (SPEC §12); light hierarchy is `ILight → DirectionalLight/PointLight/SpotLight`
  per-`View` (many per `View`).

## 7. Formatting & hygiene
- clang-format enforced; 4-space indent; 80-column soft limit.
- No `using namespace` in headers; `using` only inside `.cpp`.
- Includes: `"quoted"` for project, `<angle>` for deps/std.

## 8. Error handling
- Typed errors via a `Result<T, Error>` style type (SPEC §5); **no exceptions
  in v1**.
- Errors are typed and actionable; never silent.

## 9. Comments
- Doxygen `///` on all public API (SPEC §5).
- `//` for inline notes; no commented-out code.

## 10. Project / role naming
- Project name: **RenderEngine**; repo path: current project directory.
- Roles (loop contract): **runner** / **implementer** / **reviewer** /
  **orchestrator** (see loop-protocol).

## 11. Dependencies
- Use libraries' native types without aliases in v1 (`glm::vec3`, no wrapper
  aliases).