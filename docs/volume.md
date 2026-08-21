# volume/ — pure ray-casting math

`volume/` is the **pure-math module** (SPEC §3): the closed-form quantities a
volume ray caster needs, with **no GL**. Everything here is headless-testable.
`render/` consumes these from the `VolumeRenderer` (T9); nothing in this module
touches a GL object. It is the **T6 deliverable**, part of the `docs/volume.md`
documentation map (T6).

> The `data/` layer already provides the scalar sample source
> (`VolumeDataset::sampleTrilinear`, T5); `volume/` turns those samples into
> colors and composites them along a ray.

## Components

### RgbaColor (`volume/color.hpp`)

A plain four-float RGBA color. Its meaning depends on the API that consumes it:

- **TransferFunction control-point colors** are *straight* (non-premultiplied)
  RGBA — the ramp interpolates each channel independently (FR-vol.1);
- **`compositeFrontToBack` output** is *premultiplied* — RGB is already
  weighted by alpha (FR-vol.2).

### TransferFunction (FR-vol.1)

`volume::TransferFunction` maps a scalar value (e.g. a sampled voxel
intensity) to an RGBA color through a **piecewise-linear ramp** defined by
control points, each a `(value, straight-RGBA-color)` pair:

- **Exact at control points** — sampling a control-point value returns that
  point's color exactly;
- **Linear between them** — for two adjacent breakpoints `v0` and `v1`, every
  channel lerps with `t = (value - v0) / (v1 - v0)`; a single control point
  returns that color for every input;
- **Clamps outside the range** — values below the first (or above the last)
  breakpoint return the nearest endpoint color, so the ramp never extrapolates
  out of `[0, 1]`.

The constructor takes a vector of `ControlPoint`s. **Precondition:** points are
non-empty and sorted by strictly increasing `value` (a duplicate value would
make the ramp denominator zero). Accessors: `controlPoints()`, `size()`, and
`sample(value)`.

#### Acceptance constants (FR-vol.1, docs/volume.md)

Control points `(0 -> {1,0,0,1})`, `(0.5 -> {0,1,0,0.5})`, `(1 -> {0,0,1,0.25})`:

| Sample | Expected color | Where it comes from |
|---|---|---|
| `sample(0)`, `sample(0.5)`, `sample(1)` | `{1,0,0,1}`, `{0,1,0,0.5}`, `{0,0,1,0.25}` | exact at control points |
| `sample(0.25)` | `{0.5, 0.5, 0, 0.75}` | ramp on `[0,0.5]`, `t=2v`: `r=1-2v, g=2v, b=0, a=1-v` |
| `sample(0.75)` | `{0, 0.5, 0.5, 0.375}` | ramp on `[0.5,1]`, `t=2(v-0.5)`: `r=0, g=1-t, b=t, a=0.5-0.25t` |
| `sample(-1)`, `sample(2)` | `{1,0,0,1}`, `{0,0,1,0.25}` | clamped to nearest endpoint |

The alpha ramp is verified independently with a `(0 -> opaque white,
1 -> transparent white)` ramp: at `v=0.4` the alpha is `1 - 0.4 = 0.6` (RGB
stays `1`), proving the alpha channel lerps and not just RGB.

### Ray/AABB intersection + sampling steps (FR-vol.3)

`volume::intersectRayAabb(ray, aabb, tEntry, tExit)` is the **slab method**
closed form for the parametric entry/exit of a ray through an axis-aligned
box. On a hit, `tEntry`/`tExit` are the parametric distances of entry and
exit (`tEntry == 0` when the origin is inside the box). It returns false when
the ray misses the box or when the box lies entirely behind the origin
(`tExit < 0`). A ray parallel to a slab axis hits only if its origin lies
inside that slab.

`volume::computeRaySampleSteps(ray, aabb, stepLength)` returns the analytic
step positions along the ray/box segment, at the **centers** of the steps:

```
count = floor((tExit - tEntry) / stepLength);
t[k]  = tEntry + (k + 0.5) * stepLength     for k in [0, count).
```

Every position lies strictly inside the segment and consecutive positions are
exactly `stepLength` apart. A miss (or `stepLength <= 0`) yields an empty
result with `tEntry == tExit == 0`.

#### Acceptance constants (FR-vol.3, docs/volume.md)

Unit cube `[0,1]^3`, ray origin `(-1, 0.5, 0.5)` travelling `+x`:
`tEntry = 1`, `tExit = 2`.

| stepLength | count | positions |
|---|---|---|
| `0.25` | 4 | `1.125, 1.375, 1.625, 1.875` |
| `0.30` | 3 (floor, trailing partial dropped) | `1.15, 1.45, 1.75` |

Origin inside the box `(0.5,0.5,0.5)` moving `+z`: `tEntry = 0`, `tExit = 0.5`;
with `stepLength = 0.25` → positions `0.125, 0.375`.

Misses: a ray at `y = -1` outside the y-slab yields no steps; a ray pointing
away from the box (box behind origin) misses via `tFar < 0`.

### Front-to-back compositing (FR-vol.2)

`volume::compositeFrontToBack(samples)` accumulates a sequence of *straight*
`(color, alpha)` samples **front-to-back** (the caller supplies them in
nearest-first order) with the premultiplied-alpha rule

```
out += (1 - out.a) * (s.a * s.rgb, s.a),
```

whose closed form over a sequence `(Ci, Ai)`, `i = 0..n-1`, is

```
out.a   = 1 - prod_i (1 - Ai)
out.rgb = sum_i [ prod_{j<i} (1 - Aj) * Ai * Ci ].
```

The returned color is **premultiplied** (RGB already weighted by alpha);
divide by `.a` for the straight color (undefined for an all-transparent
sequence, which yields `(0,0,0,0)`).

#### Acceptance constants (FR-vol.2, docs/volume.md)

Samples `A={1,0,0,0.25}`, `B={0,1,0,0.5}`, `C={0,0,1,1}`:

| Blend order | Expected premultiplied result |
|---|---|
| `A, B, C` | `{0.25, 0.375, 0.375, 1}` |
| `C, B, A` | `{0, 0, 1, 1}` (C first, opaque) |

Geometric-opacity check over `N = 8` uniform `{1,1,1,0.1}` samples:
`out.a = 1 - 0.9^8 = 1 - 0.43046721 = 0.56953279`, with `rgb == out.a`
(white). An empty sequence is the identity `(0,0,0,0)`.

## Guardrails observed

- **GL-free**: no raw GL call in `volume/` (guardrail `gpu_api_ownership`);
  it links only `glm` (pure math) + `re_project_warnings`.
- **Typed, deterministic, single-threaded**: pure functions of their inputs;
  no global state, no concurrency (SPEC §5).
- **No exceptions**: everything is value-returning; preconditions are
  documented (transfer-function sort order, `stepLength > 0`).
- **Doxygen** on all public API (SPEC §5).
