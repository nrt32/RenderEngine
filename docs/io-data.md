# io/ + data/ — loaders and CPU containers

This page documents the **T4 deliverable** — the OBJ-style mesh loader and the
stb_image image loader (`io/`), plus the `Mesh` and `Image` CPU containers
(`data/`) — and the **T5 deliverable**: the NRRD volume loader (`io/volume/`)
and the `VolumeDataset` CPU container with trilinear sampling. It is part of
the `docs/io-data.md` documentation map (T4/T5).

## Module roles

- **`io/`** — loaders ONLY, no GL (SPEC §3). Each loader parses a file format
  into a `data/` container and reports failures as typed `data::Result`
  errors (SPEC §5: no exceptions in v1).
- **`data/`** — CPU containers, no GL: `Mesh` (positions + triangle indices +
  computed face normals + AABB), `Image` (decoded pixel bytes),
  `VolumeDataset` (3D scalar-voxel grid + trilinear sampling), plus the shared
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
  always yields the same Mesh/Image/VolumeDataset (single-threaded, SPEC §5).

---

# Volume loader + `VolumeDataset` (T5)

## Volume container (`data::VolumeDataset`, FR-data.3)

A CPU 3D grid of scalar voxel values, stored row-major **x-fastest**
(`index = x + sizeX*y + sizeX*sizeY*z` — the exact layout the NRRD loader
produces and the layout `tools/convert_nrrd.py` writes, SPEC §7). Storage is a
single `std::vector<float>`; the io/ loader converts the source scalar type to
float32 at load time. This is **exact** for the committed sample data:
`sample_ct.nrrd` values are int32 in `[-3024, 2529]` (`|v| < 2^24`, so int32 →
float32 is lossless) and the golden fixtures are int16.

Accessors: `sizeX()`, `sizeY()`, `sizeZ()`, `voxelCount()`, `byteSize()`,
`voxels()`, `voxelAt(x, y, z)` (discrete integer-index access), and
`sampleTrilinear(x, y, z)` (continuous sampling, FR-data.3).

### Trilinear sampling (`sampleTrilinear`, FR-data.3)

`sampleTrilinear(x, y, z)` takes continuous **index** coordinates in
`[0, dim-1]` per axis (voxel centers sit at integer coordinates); out-of-range
coordinates clamp to the nearest voxel center. The result is the weighted
average of the 8 surrounding voxel centers with weights

```
w000 = (1−fx)(1−fy)(1−fz)   w100 = fx(1−fy)(1−fz)
w010 = (1−fx)fy(1−fz)        w110 = fx·fy·(1−fz)
w001 = (1−fx)(1−fy)fz        w101 = fx(1−fy)fz
w011 = (1−fx)fy·fz           w111 = fx·fy·fz
```

where `fx/fy/fz` are the fractional parts of the clamped coordinates. At
integer coordinates the weights collapse to a single 1.0, so the sampler
reproduces the voxel value **exactly**.

### golden_volume.nrrd acceptance constants (FR-data.3)

From `data/fixtures/golden_volume.nrrd` (2x2x2 int16, x-fastest):

| Constant | Value |
|---|---|
| dims | `2 × 2 × 2` |
| voxel closed form | `v(x, y, z) = x + 2·y + 4·z` (values `0..7`) |
| interior sample (multilinear field) | `sampleTrilinear` equals `v` itself: `(0.5,0.5,0.5)→3.5`, `(0.25,0.75,0.5)→3.75`, `(0.7,0.2,0.9)→4.7` (within 1e-6) |
| weighted-average check | for corner field `c(x,y,z)=(x+1)(y+2)(z+3)` on the cell, `sampleTrilinear(0.25,0.75,0.5)=12.03125` (within 1e-6) |

## NRRD loader (`io::loadNrrdVolume`, FR-io.2/FR-io.4)

`data::Result<data::VolumeDataset> loadNrrdVolume(const std::string& path)`
parses a NRRD file (text header + raw, uncompressed voxel block). The supported
subset is exactly the format `tools/convert_nrrd.py` writes (SPEC §7):

- magic `NRRD0001`..`NRRD0005` (committed files are `NRRD0004`);
- `dimension 3` with exactly three `sizes`;
- scalar types matching the converter's `TYPE_MAP` (`int8`..`int64`,
  `uint8`..`uint64`, `float`, `double`);
- `endian: little` (default) or `endian: big`;
- `encoding: raw` only (v1; gzip/bzip2/txt/hex/ascii are rejected as
  `UnsupportedEncoding`, SPEC §7 uncompressed voxel block);
- extra fields (`space`, `space directions`, `kinds`, `space origin`, …) are
  ignored — v1 samples in index space.

The raw block must hold at least `sizes-product × element-width` bytes (extra
trailing bytes are ignored, matching the converter). The **v1 memory budget cap
(SPEC §5)** is enforced at load time: every axis must be `≤ 128` and the voxel
count `≤ 128^3`, so an oversized file fails with a typed error instead of a
multi-megabyte allocation.

### Typed errors (`VolumeLoadError`)

| Code | Meaning |
|---|---|
| `FileOpen` | file could not be opened |
| `BadMagic` | first line is not `NRRD0001..NRRD0005` |
| `MalformedHeader` | missing/invalid required fields (`dimension`/`sizes`/`type`), malformed `sizes`, bad `endian`, unterminated header |
| `UnsupportedDimension` | `dimension != 3` (v1 is 3D only) |
| `UnsupportedType` | `type` not in the supported scalar set |
| `UnsupportedEncoding` | `encoding != raw` (v1) |
| `VoxelBlockSize` | raw block shorter than `dims × element-width` |
| `BudgetExceeded` | dims exceed the v1 budget cap (≤ 128^3) |

No exceptions are thrown; the `VolumeDataset` is constructed only after the
whole file validates, so a failed result never carries partial state (FR-io.4).

### sample_ct.nrrd acceptance constants (FR-io.2)

From `data/volumes/sample_ct.nrrd` (128 x 128 x 70 int32, x-fastest):

| Constant | Value |
|---|---|
| dims | `128 × 128 × 70` (SPEC §7; all axes ≤ 128) |
| voxel count | `128 · 128 · 70 = 1,146,880` |
| float32 storage | `1,146,880 × 4 = 4,587,520` bytes |
| 8 indexed corners | `(0,0,0) (127,0,0) (0,127,0) (127,127,0) (0,0,69) (127,0,69) (0,127,69) (127,127,69)` — each `-3024` (outside-body background fill) |
| interior indexed voxels | `(64,64,35)→26`, `(64,64,36)→27`, `(63,64,35)→28`, `(64,63,35)→29`, `(64,64,34)→30`, `(32,80,20)→31`, `(96,48,50)→-885`, `(20,30,10)→-949`, `(100,100,60)→-910` |

These interior values are read directly from the committed file and prove the
byte decoding + x-fastest indexing (the uniform corner values alone cannot).
The values fit in int32 → float32 losslessly, so the loader reproduces them
exactly.

## Volume memory budget (SPEC §5)

The v1 cap is `128^3 = 2,097,152` voxels; float32 storage is `128^3 × 4 =
8,388,608` bytes (8 MiB). `sample_ct.nrrd` (128x128x70) sits comfortably under
it, and the loader refuses any file whose dims exceed it (`BudgetExceeded`).
