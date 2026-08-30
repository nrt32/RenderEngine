# ReMaterial → ShaderProgram Branch Table (SPEC §12.2, V7 T9)

> Binding table per `docs/spec/materials_lights.md:166` `Re*` inventory and
> `docs/spec/guardrails.md` RE-minimal rule: each `ReMaterial` dispatches to
> exactly one `ShaderProgram` branch without per-draw switch on `MaterialKind`.
> Renderers take concrete `*Material` pointers (LSP-safe) and the broker's
> `MaterialMapper` visitor selects the branch via `Broker::registerMapper` OCP.
> This file enumerates the active branches for V7 (Phong/PBR/Point/Line/Csg)
> and their shader files under `render/shaders/` (SPEC §9 V2.6 `RE_GLSL_VERSION`).

| ReMaterial (RE-minimal) | ShaderProgram branch | Vertex shader | Fragment shader | Geometry shader | Dispatch renderer |
|---|---|---|---|---|---|
| `PhongMaterial` (`MeshMaterial: IColorMaterial`, baseColor.a drives `isTransparent`) | `mesh_opaque` | `mesh_opaque.vert.glsl` | `mesh_opaque.frag.glsl` | — | `MeshRenderer::drawLayer` (opaque + OIT capture via `LinkedListOIT`) |
| `PBRMaterial` (`MeshMaterial: IColorMaterial`, albedo/metallic/roughness, IBL) | `mesh_pbr` (deferred, spec-only) | `mesh_opaque.vert.glsl` (shared) | `mesh_pbr.frag.glsl` (future) | — | `MeshRenderer` PBR variant (deferred per SPEC §1 non-goal, table reserves branch) |
| `VolumeMaterial` (`IVolumeMaterial`, ReTfUniforms beside mat) | `volume_raycast` | `volume_raycast.vert.glsl` | `volume_raycast.frag.glsl` | — | `VolumeRenderer::drawLayer` |
| `SliceMaterial` (`IColorMaterial`, capColor/capping) | `slice_clip` | `slice.vert.glsl` | `slice_clip.frag.glsl` | `slice_clip.geom.glsl` | `SliceRenderer::drawLayer` (clip) |
| `ContourMaterial` (`ILineMaterial`, lineWidth/stipple) | `contour` | `contour.vert.glsl` | `contour.frag.glsl` | `contour.geom.glsl` | `ContourRenderer::drawLayer` |
| `PointMaterial` (`IColorMaterial`, baseColor+radius+worldUnits+PointFill) | `point_impostor` | `point_impostor.vert.glsl` | `point_impostor.frag.glsl` | — | `PointRenderer::drawLayer` (impostor billboard + `MeshRenderer` sphere delegate for single 3D) |
| `LineMaterial` (`ILineMaterial`, baseColor+width+worldUnits+cap/join+DashPattern) | `line` | `line.vert.glsl` | `line.frag.glsl` | — | `LineRenderer::drawLayer` (SSBO+gl_VertexID 6-vert view-quad strip, Rougier `mod(s)` dash, `fwidth` AA, `miterLimit 4→bevel`) |
| `CsgMaterial` (`IColorMaterial`, base+cap MeshMaterial, CsgOp) | `csg_resolve` + `csg_capture` | `csg_capture.vert.glsl` | `csg_capture.frag.glsl` / `csg_resolve.frag` | — | `CsgRenderer::drawCsg` (capture via `imageAtomicExchange`) + `CsgOitStage::resolve` (`csg_resolve.frag` sort+classify → `csgResolved` SSBO) |

## Notes

- **Branch per concrete material** — no `switch(MaterialKind)` in renderers; `Broker` visitor `std::visit(overloaded{ [&](MeshMaterialDesc&)... })` routes to the branch at map time, and `RenderStack::techniqueOrder` `[Volume,VolumeSlice,Plane,Csg,Mesh,MeshSlice,Point,Line,Contour]` size 9 governs per-layer ordering (Layer::Count stays 8). This preserves LSP (no `baseColor()` on `VolumeMaterial`) and ISP (`IColor`/`IVolume`/`ILine` segregation per `materials_lights.md:57`).
- **Shader externalization** — every branch loads via `core::ShaderProgram::createFromFiles` with baked `RE_SHADER_DIR` (`render/CMakeLists.txt`); line-count preserves `ERROR: 0:7` golden.
- **GLSL version** — all `.glsl` head with `RE_GLSL_VERSION_LINE` (`#version 450 core` portable, `460` hardware), verified by `t8_v2_glsl_version_test.cpp`.
- **RHI-ready** — `ReMaterial*` stays an `IRHI*` handle carrier after `core/rhi/gl/` lands; the branch table is the OCP seam for new `ToonMaterial` (add file, no edit to existing branches).
- **Guardrails** — `render/` never includes `scene/` (disposition_render), `asset_indirection` forbids `data::Mesh` positions in `render/re_scene/`; this table is doc-only and carries no GL includes.
