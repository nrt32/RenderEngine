# io/ + data/ — loaders and CPU containers

This page documents the **T4 deliverable**: the OBJ-style mesh loader and the
stb_image image loader (`io/`), plus the `Mesh` and `Image` CPU containers
(`data/`). It is part of the `docs/io-data.md` documentation map (T4; the
NRRD volume loader in T5 extends this page).

## Module roles

- **`io/`** — loaders ONLY, no GL (SPEC §3). Each loader parses a file format
  into a `data/` container and reports failures as typed `data::Result`
  errors (SPEC §5: no exceptions in v1).
- **`data/`** — CPU containers, no GL: `Mesh` (positions + triangle indices +
  computed face normals + AABB), `Image` (decoded pixel bytes), plus the shared
  `data::Result<T>` from T1. `glm` is a pure-math dependency and keeps both
  modules GL-free.

## Mesh container (`data::Mesh`, FR-data.1/FR-data.2)

Built through `Mesh::fromTriangles(positions, indices)` from **pre-validated**
data — the io/ loader validates the whole file before constructing the Mesh, so
a malformed file can never yield a partially-built container (FR-io.4).

Two properties are computed analytically at construction:

- **Face normals (FR-data.1).** Face `i` spans `indices()[3i .. 3i+2]`; its
  normal is the closed-form right-hand-rule cross product
  `normalize(cross(p1 − p0, p2 − p0))`. A degenerate (zero-area) face gets the
  deterministic zero vector `(0,0,0)` — the normal is undefined for it.
- **AABB (FR-data.2).** Component-wise min/max over all positions.

Accessors: `positions()`, `indices()`, `faceNormals()`, `bounds()`,
`vertexCount()`, `triangleCount()`.

### Golden-box acceptance constants (hand-counted, from `data/fixtures/golden_box.obj`)

| Constant | Value |
|---|---|
| vertices | 8 |
| triangle faces | 12 |
| AABB min | `(0, 0, 0)` |
| AABB max | `(1, 1, 1)` |
| face normals | outward unit normals (right-hand rule, all 12 faces) |

## OBJ loader (`io::loadObjMesh`, FR-io.1/FR-io.4)

`data::Result<data::Mesh> loadObjMesh(const std::string& path)` parses a
Wavefront OBJ text file:

- `v x y z` vertices (optional 4th homogeneous component ignored);
- `f i j k ...` faces with **1-based positive** vertex indices, in any of the
  forms `i`, `i/t`, `i//n`, `i/t/n` (texture/normal components ignored);
  polygons are fan-triangulated;
- `#` comments and blank lines skipped; other elements (`vt`, `vn`, `g`, `o`,
  `s`, `usemtl`, `mtllib`, `l`, `p`, …) ignored (v1 needs geometry only).

Typed errors (FR-io.4) carry a `MeshLoadError` code — `FileOpen`,
`VertexParse`, `FaceParse`, `IndexRange`, `NoVertices`, `NoFaces` — and a
message naming the file and (for parse errors) the line. No exceptions are
thrown; the Mesh is constructed only after the entire file validates, so a
failed result never carries partial state.

### bunny.obj acceptance constants (hand-counted, from `data/meshes/bunny.obj`)

| Constant | Value |
|---|---|
| vertices | 35,947 |
| triangle faces | 69,451 |
| indices | 208,353 (= 3 × 69,451) |
| AABB min | `(-0.094690, 0.032987, -0.061874)` |
| AABB max | `(0.061009, 0.187321, 0.058800)` |

The AABB values are the float32-rounded extremes of the committed file
(verified component-wise against the exact extreme vertices, e.g.
`v -0.094690 0.124172 0.020267` for min-x). The loader parses with
`std::strtof` (correctly-rounded, identical to the C++ float literals the gate
asserts).

## Image container (`data::Image`, FR-io.3)

Decoded pixel bytes with dimensions and channel count. Layout is **row-major
with a top-left origin** (stb's native convention), `channels` bytes per pixel.
`pixel(x, y, c)` returns channel `c` of the pixel at column `x`, row `y`.

## Image loader (`io::loadImage`, FR-io.3/FR-io.4)

`data::Result<data::Image> loadImage(const std::string& path,
std::int32_t requestedChannels = 0)` decodes any format stb_image handles
(PNG, JPG, BMP, TGA, …). `requestedChannels` selects the output layout:
`0` keeps the file's native channel count; `1..4` forces that many channels
(stb's documented conversion — missing channels fill with 0, alpha defaults to
255). The file is opened with `std::ifstream` first so "cannot open" and
"cannot decode" map to distinct typed errors.

Typed errors (FR-io.4) carry an `ImageLoadError` code — `FileOpen`, `Decode`,
`InvalidChannels` — with the decode message including `stbi_failure_reason()`.
No exceptions are thrown; a failed result never carries a partial Image.

### golden_image.png acceptance constants (hand-counted, from `data/fixtures/golden_image.png`)

| Constant | Value |
|---|---|
| dimensions | 8 × 8, 8-bit RGB (3 channels) |
| pixel closed form | `pixel(x, y) = (32·x, 32·y, 128)` for every pixel |
| corners / center | `(0,0)=(0,0,128)`, `(7,0)=(224,0,128)`, `(0,7)=(0,224,128)`, `(7,7)=(224,224,128)`, `(3,3)=(96,96,128)`, `(4,4)=(128,128,128)` |
| RGBA (requestedChannels=4) | same RGB + alpha 255 |

## Guardrails observed

- **GL-free**: neither `io/` nor `data/` contains a raw GL call (guardrail
  `gpu_api_ownership`); `data/` links only `glm` (pure math), `io/` links
  `re_data` + the header-only `stb` FetchContent target.
- **Typed diagnostics**: every failure returns `data::Result` with an
  enumerated `*Error` code — never exceptions, never silent.
- **Dependency lock**: stb is pinned by commit SHA via FetchContent (SPEC §6);
  `STB_IMAGE_IMPLEMENTATION` is defined in exactly one translation unit.
- **Determinism**: loaders are pure functions of the input file; the same file
  always yields the same Mesh/Image (single-threaded, SPEC §5).