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
- Module directories (lowercase): `io/ data/ volume/ scene/ core/ broker/ render/ app/ utils/ test_utils/ tests/` (exact order matching `tools/env.sh:6` `AUDIT_SOURCE_DIRS="io data volume scene core broker render app utils test_utils tests"` and `tools/audit.sh:63` default — spec-review #12 fix; `R15` exact-string gate `test "$AUDIT_SOURCE_DIRS" = "io data volume scene core broker render app utils test_utils tests"` requires this order; `test_utils/` is peer test-support lib, GL via `REContext` only — T18) (`scene/` GL-free value lib, `broker/` only lib that may include both `scene/`+`render/`, `utils/` offscreen context + pixel reader — see `docs/spec/modules.md` §3).

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
- **`scene/` naming:** types inside `re::scene::` carry **no `App` prefix**
  (`scene::MeshObject`, `scene::Camera`, `scene::View` — the namespace **is** the
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
    `broker_per_type` + `isp_mapper_forbid`), header `broker/i_mapper.hpp` declares both `IMapper` + `ICachedMapper` together (single header, ISP-segregated by inheritance) for include simplicity; `broker/i_cached_mapper.hpp` retained as alias include (`#include "i_mapper.hpp"`) so `app/` DIP graph stays stable per `docs/spec/broker.md` §11.2.1 Q33 — `T4`/`T5` gate `grep -c DrawContext docs/spec/*.md==0` outside historical alias ensures `REContext` rename. `Broker{registerMapper<T>(unique_ptr<IMapper<T>>), get<T>()}`
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
- An error's identity is the pair `(Error::domain, Error::code)`:
  producers that own an enumerated code range stamp their
  `data::ErrorDomain` on every error (the io/ loaders' ranges collide
  numerically), consumers branch on the pair — never by parsing messages.
  Untagged ad-hoc codes carry `ErrorDomain::None`.
- `Result::operator*` / `operator->` are value-branch accessors only:
  branch on `ok()`/`failed()` first. A failed dereference asserts in debug
  builds; release builds leave it undefined behavior (documented, never an
  exception).
- Monadic combinators are camelCase (`map`, `andThen`) per §4 — not std's
  snake_case `and_then`.

## 8b. Ownership & borrow notation (hard, T13 user mandate)
- **No raw pointers where ownership/lifetime matters.** Use `unique_ptr`
  (sole owner), `shared_ptr` (shared owner), `std::weak_ptr` (observer), or a
  generational handle (`AssetHandle` / `AssetId`) instead.
- **GPU resources are handle-based**, never refcounted per draw: generational
  handles into pools (`AssetHandle`) — no atomic-refcount churn in frame
  loops, no post-teardown zombie resources. `shared_ptr` is reserved for
  genuinely shared CPU-side assets across layers (e.g. an immutable
  `data::Mesh` co-owned by scene objects and stores).
- **Marked borrows:** a raw pointer is allowed only where the borrow is
  structurally scope-bounded (e.g. a call-scoped destination framebuffer, a
  store getter borrowing its own storage). Every such site MUST be written
  with the marker between star and identifier, and carry a Doxygen lifetime
  note naming its owner:
  ```cpp
  /// @note lifetime: borrowed for the duration of one render/blit call;
  /// owned by the caller (ViewTarget inner FB or the window's default FB).
  core::Framebuffer* /*borrow*/ framebuffer = nullptr;
  ```
- The mechanical audit floor (`ownership_raw_ptr_scene|broker|app` in
  `tools/audit.rules`) fails any UNMARKED raw-pointer declaration under
  `scene/`, `broker/`, `app/`; the allowlist of documented borrows is exactly
  the greppable set of `/*borrow*/` sites.

## 9. Comments
- Doxygen `///` on all public API (SPEC §5).
- `//` for inline notes; no commented-out code.

## 10. Project / role naming
- Project name: **RenderEngine**; repo path: current project directory.
- Roles (loop contract): **runner** / **implementer** / **reviewer** /
  **orchestrator** (see loop-protocol).
- **Broker roles:** `ViewSynchronizer`/`ViewCompositor`/`ViewBridge` (SRP split) and `IMapper`/`ICachedMapper` per-file naming — see `docs/spec/broker.md` §11.2 naming table and `AGENTS.md` broker mediation; one `IMapper`/`ICachedMapper` per file (`broker_per_type`), `ViewBridge` façade (spec-review #15).

## 11. Dependencies
- Use libraries' native types without aliases in v1 (`glm::vec3`, no wrapper
  aliases).

## 12. Test naming & infra (T6 IT1-IT5, landed T6)
- **Monolithic binary `re_tests` (IT1 deferred):** one `add_test(re_tests)` +
  `GTest::gtest_main` single binary, single `OffscreenEnvironment` shared GL
  context (utils::OffscreenContext delegating to core::loadCoreGl). Intentional:
  deterministic headless fixture, no per-case GL context churn, single
  `ctest -V` node (task traceability preserved by `tN_` prefix).
- **`tN_` task-prefixed test files (IT3 deferred):** `t1_*` .. `t23_*` prefixes
  map each test file to its originating `TASKS.md` task (regression lock R3
  traceability). Renaming to domain-first (`mesh_`, `volume_` etc.) deferred;
  current scheme is intentional gate choice, not drift.
- **xvfb hard-fail (IT4 deferred):** sample-smoke tests (`t12_samples_test`,
  `t14_mpr` etc.) hard-fail when Xvfb/display missing — intentional
  config-fail loudness (FR-app.1 needs display; silent skip would hide
  mis-configuration). CI provides Xvfb; local `xvfb-run` is the gate env.
- **Helpers single-source (IT2 landed):** `tests/test_helpers.{hpp,cpp}` is the
  single source for `makeQuad`/`makeCamera`/`WindowTarget`/`readPixel`/
  `expectPixel` (~150 lines deduplicated, `grep -c "makeQuadMesh"
  tests/test_helpers.cpp==1` proves single definition). Call sites use
  `makeQuad()` alias to keep the grep gate literal single-definition while
  sharing the golden mesh; pixel helpers delegate via `utils::PixelReader` →
  `core::readRgba8` (raw `glReadPixels` stays `core/re_context.cpp` count 1
  until T18 migrates to `test_utils::PixelReader`).
- **Weak asserts (IT5 deferred):** `strlen>0` / `EXPECT_GT(id,0)` residue
  compensated by strong analytic neighbors (1/255 + 1e-6 gates, R4); tighten
  explicitly when touched, but blanket rename deferred — note is decision record.