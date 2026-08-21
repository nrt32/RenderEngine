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

### Core capabilities (v1)
1. **Mesh rendering** — shaded triangle meshes.
2. **Volume rendering** — basic ray casting (front-to-back compositing) of a
   volumetric dataset.
3. **Plane rendering** — textured/shaded planes.
4. **Mesh slice rendering** — a planar slice through a mesh (cross-section).
5. **Transparency / OIT** — correct order-independent transparency compositing.
6. **MPR view** — a single window with 3 orthogonal slice views (Transverse,
   Coronal, Sagittal) and a 3D rendering view.

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

### Non-goals (v1)
- **PBR / advanced materials** — Phong only; the interface must allow later models.
- **HDR / post-processing pipeline** — no bloom, tonemapping, SSAO, shadows.
- **Asset import formats** — a single bundled/OBJ-style loader only; no glTF/fbx.
- **Scene graph / transform hierarchy** — per-object transforms only (model
  matrix); no parent/child trees. **Skeletal animation is out of scope.**
- **Out-of-core / streaming** — all data loaded fully into memory.
- **Multi-window / headless rendering** — single window, single GL context,
  always GUI-attached.