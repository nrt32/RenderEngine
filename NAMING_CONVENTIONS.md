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
- Module directories (lowercase): `io/ data/ volume/ core/ render/ app/ tests/`.

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
  `re::io::`, `re::data::`, `re::volume::`, `re::core::`, `re::render::`,
  `re::app::`.

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