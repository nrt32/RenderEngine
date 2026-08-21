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