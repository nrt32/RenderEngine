# SPEC §1 — Goals & non-goals

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §1" mean this file.

## 1. Goals & non-goals

### Product
A C++ real-time rendering engine built on **OpenGL (GL 4.6 core)** with **CMake**
as the build system, developed and run on **Ubuntu inside WSL on Windows**.
Version 1 is a capability-focused engine that renders meshes, volumes (basic
ray casting), planes, mesh slices, and Order-Independent-Transparency (OIT)
compositing, with a Multi-Planar Reconstruction (MPR) sample that shows
Transverse/Coronal/Sagittal views plus a 3D view in one window.

Each capability ships as:
- an engine-side implementation module, and
- one sample application demonstrating how to drive it.

### Core capabilities (v1 + V7 extensions)
1. **Mesh rendering** — shaded triangle meshes.
2. **Volume rendering** — basic ray casting (front-to-back compositing) of a
   volumetric dataset.
3. **Plane rendering** — textured/shaded planes.
4. **Mesh slice rendering** — a planar slice through a mesh (cross-section).
5. **Transparency / OIT** — correct order-independent transparency compositing.
6. **MPR view** — a single window with 3 orthogonal slice views (Transverse,
   Coronal, Sagittal) and a 3D rendering view.
7. **CSG (V7)** — GPU `Cube(2) − Sphere(0.6)` hole via Puxel 2-stage SSBO `CsgOitStage` capture→sort→filter→`csgResolved` then `LinkedListOIT::endWithCsg` k-way merge (Kauker Puxels, no CPU boolean; `B`'s material drives hole, `paintInterior` true→volume recolor, false→surface strip) — measured by `FR-render.7` per `docs/spec/frs.md` §4.7 `Cube(2)−Sphere(0.6)` `1/255` hole.
8. **Points (V7)** — `PointObject`/`PointCloudObject` 100s points, `worldUnits` toggle, `3D` single → `MeshRenderer` `Sphere` reuse, `2D`/cloud → `PointRenderer` impostor `gl_FragDepth` ray sphere (`Hollow`/`GridDashed` fills) — measured by `FR-render.8` `1/255`.
9. **Lines (V7)** — `LineObject`/`PolylineObject` `SSBO+gl_VertexID` 6-vert view-quad strip, analytic `fwidth` AA, `Rougier mod(s,patternLen)` dash `miterLimit 4→bevel`, `round/square` caps, `worldUnits` toggle (own `LineRenderer`, not `ContourRenderer` GS) — measured by `FR-render.9` `90% within 2px` `1/255`; `FR-app.4` `addCsg/addPoint/addLine` via `Engine` `1/255` smoke.

### Materials
- **Phong** material model for v1, integrated through a **modular material
  interface** designed so additional models (PBR, toon, …) can be added without
  touching the renderer core (SOLID: open/closed, dependency inversion).

### GUI
- Lightweight **Dear ImGui** (immediate-mode) with GLFW/OpenGL3 backend.
- **GLFW + glad2** windowing and GL function loading.

### Success criteria
1. All sample applications run and display the expected capability.
2. All unit tests pass with strong, explainable assertions (no tolerance-abuse).
3. No memory leaks — test binaries build with **ASan+UBSan** and run clean.

### Non-goals (v1) — updated V5 (T3/T11/T15 landed)
- **PBR / advanced materials** — Phong only; the interface must allow later models. **Exception (V5 T15): minimal per-View `Light{Directional}` value-type surface is IN-SCOPE** for visualization reuse — empty `lights` = fixed headlight (FR-render.1 preserved), one `Directional` → `broker/light_mapper` upload (see `docs/spec/materials_lights.md` §12.3 + `TASKS.md T15`); full `PBR` / `SliceMaterial` / `ContourMaterial` + `ILight` hierarchy (3 `Light` types + `Point`/`Spot`) remains deferred per SPEC §12.2-12.3 / §1 non-goal.
- **HDR / post-processing pipeline** — no bloom, tonemapping, SSAO, shadows.
- **Asset import formats** — a single bundled/OBJ-style loader only; no glTF/fbx.
- **Scene graph / transform hierarchy** — per-object transforms only (model
  matrix); no parent/child trees. **Skeletal animation is out of scope.**
- **Out-of-core / streaming** — **No cap streaming (per Q4):** committed `sample_ct.nrrd` is just example `128×128×70` `≤128³`; **product loader has no `≤128³` cap** — any dims via `core::Caps` tiled/downsampled streaming (`maxTexture3DSize` probe, `TODO(RHI)` → `IRHIContext::capabilities()` after T10, see `TASKS.md T11a` + `NFR §5` `maxTexture3DSize`), not `BudgetExceeded` for `>128³` alone (only probe-fail path). Out-of-core *streaming* is thus IN-SCOPE via `core::Caps`, not non-goal.
- **Multi-window interactive** — single interactive `Window` remains; **headless/offscreen rendering via `utils::OffscreenContext` + `renderOffscreen` / `core::loadCoreGl` is IN-SCOPE** for server-side visualization and headless tests (see SPEC §3 T3, `scene/` disposition, `TASKS.md T3/T4` harness decoupling + `docs/render.md` offscreen). Multi-window *interactive* (N windows with shared context) stays deferred.