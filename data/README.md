# data/ — RenderEngine datasets

This directory holds the committed assets and golden fixtures used by the
RenderEngine samples and unit tests. Everything here is redistributed under a
permissive license, recorded in the LICENSE file beside each dataset directory
(SPEC section 6 "Asset/data licensing"). The full source URLs and SHA256
checksums are pinned in SPEC section 7.

## Layout

| Path | Contents | License |
|---|---|---|
| `data/meshes/` | `bunny.obj`, `teapot.obj` — sample mesh OBJs | Public domain (see `data/meshes/LICENSE`) |
| `data/volumes/` | `sample_ct.nrrd` — downsampled CT sample volume | CC-BY-SA 4.0 (see `data/volumes/LICENSE`) |
| `data/fixtures/` | hand-authored golden files for io/data tests | Project-owned (this repo) |

## Meshes

### `data/meshes/bunny.obj` — Stanford bunny

- **Source:** Stanford 3D Scanning Repository
  (http://graphics.stanford.edu/data/3Dscanrep/), mirrored at
  `alecjacobson/common-3d-test-models`.
- **Pinned URL:** https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/stanford-bunny.obj
- **License:** Public domain (Stanford Computer Graphics Laboratory scan data).
- **SHA256 (committed file):** `1eb35d1e21ce99e5ce911353b6be278990713448dd9e8f5c9387f9de39b32205`
- **Hand-counted constants:** 35,947 `v` vertices; 69,451 `f` faces (12 = 69,451
  mod 7; each line is a single face reference). Consumed by the T2 gate
  (vertex count) and the io/ loader test (FR-io.1).

### `data/meshes/teapot.obj` — Utah teapot

- **Source:** Martin Newell (1975), mirrored at
  `alecjacobson/common-3d-test-models`.
- **Pinned URL:** https://raw.githubusercontent.com/alecjacobson/common-3d-test-models/master/data/teapot.obj
- **License:** Public domain.
- **SHA256 (committed file):** `1b5396fedd74b577e32cef41146582c2f2e1a050d5b4915193c0ac1ad4187ed4`
- **Hand-counted constants:** 3,644 `v` vertices; 6,320 `f` faces.

## Volumes

### `data/volumes/sample_ct.nrrd` — downsampled CT chest sample

- **Source:** `CT-chest.nrrd` (Medical Decathlon test data), hosted by the 3D
  Slicer testing-data mirror `Slicer/SlicerTestingData` (content-addressed
  release `SHA256`).
- **Pinned URL:** https://github.com/Slicer/SlicerTestingData/releases/download/SHA256/4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e
- **License:** CC-BY-SA 4.0. Attribution (dataset, paper citation, DOI,
  change indication) recorded in `data/volumes/LICENSE`.
- **Source SHA256:** `4507b664690840abb6cb9af2d919377ffc4ef75b167cb6fd0f747befdb12e38e`
- **Committed file SHA256:** `816375cdcbb3a00abb87fcbd14075f78287aaf7e05eb751082b5c900f2df7865`
- **Format:** NRRD (text header + raw, uncompressed voxel block), `type: int`
  (32-bit signed; the source `CT-chest.nrrd` is int32 and
  `tools/convert_nrrd.py` preserves the source type — the T2 gate asserts the
  header type), `sizes: 128 128 70` (all axes <= 128, SPEC section 5 memory
  budget cap). Produced by downsampling the source with
  `tools/convert_nrrd.py` at setup time (SPEC section 7). Consumed by the T2
  gate (dims + type) and the io/ volume loader test (FR-io.2).
- **Voxel layout:** x-fastest (index = x + 128*y + 128*128*z).

## Golden fixtures

The fixtures under `data/fixtures/` are hand-authored in this repo (project
owned, no external license). Each is used by an io/data test whose acceptance
constant is hand-counted from the committed file. Checksums below are of the
committed files.

### `data/fixtures/golden_box.obj` — triangulated unit cube

- **Constants:** 8 vertices; 12 triangle faces; axis-aligned bounding box
  min `(0,0,0)`, max `(1,1,1)`.
- **Vertex table** (`v` order): `1=(0,0,0) 2=(1,0,0) 3=(1,1,0) 4=(0,1,0)
  5=(0,0,1) 6=(1,0,1) 7=(1,1,1) 8=(0,1,1)`.
- **Normals:** each face is wound so the right-hand-rule normal
  `cross(b-a, c-a)` is the outward unit normal (verified for all 12 faces).
- **SHA256:** `9e0bf449cdf212ab0cf77a1fa51ff2147f2944f22775970331abb37397a1612a`
- **Used by:** io/ mesh loader (FR-io.1), Mesh AABB + face normal
  (FR-data.1/2), MPR contour (FR-app.3).

### `data/fixtures/golden_volume.nrrd` — 2x2x2 int16 volume

- **Constants:** dims `2x2x2`; 16-byte raw block; voxel at `(x,y,z)` equals the
  closed-form `x + 2*y + 4*z` (x-fastest), so the 8 values are
  `0,1,2,3,4,5,6,7`. Used by the trilinear-sampling test (FR-data.3) where an
  interior sample equals the closed-form interpolant of the 8 corners.
- **SHA256:** `481f61987d9fc59e0a18511be002cb9a97c8933d9325753b8b9d1c63ce4f7e01`
- **Used by:** io/ volume loader (FR-io.2), trilinear sampling (FR-data.3).

### `data/fixtures/golden_image.png` — 8x8 RGB test image

- **Constants:** dims `8x8`, 8-bit RGB; pixel at `(x,y)` equals the closed-form
  `(32*x, 32*y, 128)`. Corners: `(0,0)->(0,0,128)`, `(7,0)->(224,0,128)`,
  `(0,7)->(0,224,128)`, `(7,7)->(224,224,128)`; center `(3,3)->(96,96,128)`,
  `(4,4)->(128,128,128)`.
- **SHA256:** `26033d298e625be34fb18797154d047ca36381dbda96495333f9e2cca8605432`
- **Used by:** io/ image loader (FR-io.3), plane texture sample (FR-render.5).
