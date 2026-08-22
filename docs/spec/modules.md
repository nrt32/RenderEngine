# SPEC §3 — Module blueprint

> Part of the RenderEngine spec (see `SPEC.md` for the index). Section numbers
> are stable; references like "SPEC §3" mean this file.

## 3. Module blueprint

Layer-first, not capability-first. Everything drawable is an `IRenderer`;
views compose renderers. **MPR is app-level composition, not a module.**

```
io/          loaders ONLY, no GL        (mesh/ volume/ image/)
data/        CPU containers, no GL      (Mesh, VolumeDataset, Image, TransferFunction)
             + GL-free typed Result<T,E> (data/result.hpp, shared by all layers)
volume/      pure math: dataset sampling/interp, transfer function,
             ray-cast compositing math  <- NO GL, headless-testable
core/        GL foundation: GLFW context, RAII GL objects, ShaderProgram,
             thin core::Draw API, raw-GL anchors (core::loadCoreGl,
             core::readRgba8)           <- SOLE owner of raw GL calls
utils/       test-support + windowing, NO raw GL (offscreen GL context via
             GLFW/EGL, pixel reader facade) — delegates the raw parts to the
             core/ anchors (SPEC §9 V2.1)
render/      ONE class per rendering technique, unified IRenderer interface
             |- IMaterial + PhongMaterial      (transparency = material property)
             |- ITransparencyPipeline + LinkedListOIT  (swappable OIT impl)
             |- MeshRenderer   (opaque forward pass + AUTO-engaged OIT for
             |                  transparent materials; single mesh entry point)
             |- SliceRenderer  (mesh-family, GEOMETRY-SHADER plane clip; GPU-only;
             |                  reuses mesh geometry handling + materials; NO OIT in v1)
             |- PlaneRenderer  (textured quads/planes — feeds MPR)
             |- VolumeRenderer (ray-cast GL draw; volume/ provides the pure math)
app/         compositions + samples + ImGui overlay
             |- SceneView  (composes MeshRenderer + VolumeRenderer +
             |              optional Slice/Plane)
             |- MPRView    (composes 3x PlaneRenderer for T/C/S + 1x SceneView 3D)
tests/       headless unit tests (consume core/ wrappers + the utils/ fixture)
```

### Design principles (SOLID)
- **OIT is a characteristic, not a peer renderer.** Transparency lives on
  `IMaterial`; `MeshRenderer` auto-engages the injected `ITransparencyPipeline`
  (v1: per-pixel linked list, capture → depth-sort → composite) when any mesh's
  material is transparent. The pipeline interface is swappable (open/closed,
  dependency inversion) so future OIT variants need no renderer changes.
- **Slicing is geometry, not compositing.** `SliceRenderer` is a mesh-family
  technique using a geometry shader to clip against a plane (pure GPU). It
  shares mesh geometry handling and the material system but does **not** use
  OIT in v1.
- **Stateless renderers.** `IRenderer::render(scene, camera, target)` receives
  its data per call; renderers own only GL resources. One mesh can be drawn by
  both SceneView and MPR views without duplication. Data lives in `data/` +
  app-level scene structs.
- **Dependency inversion:** renderers depend on `IMaterial` /
  `ITransparencyPipeline` abstractions, never concrete material/OIT classes.
- **GL ownership:** raw `glXxx(...)` calls appear ONLY under `core/` (RAII GL
  objects + thin `core::Draw` API). `render/` draw passes, `app/`, `tests/`,
  and `utils/` use `core/` wrappers; `utils/` additionally hosts the offscreen
  context + pixel reader, delegating their raw parts to the `core/` anchors
  (`core::loadCoreGl`, `core::readRgba8`). `io/`, `data/`, `volume/` are
  GL-free. This makes the GL-ownership audit rule mechanically enforceable
  (single-dir anchor).