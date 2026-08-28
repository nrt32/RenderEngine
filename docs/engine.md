# docs/engine.md — Engine facade (`viz::Engine`) for visualization consumers (SPEC §3, TASKS T1)

> The 80% case — loading a mesh or volume and drawing it into a window or
> offscreen framebuffer — previously required wiring `AppContext` plus
> `SceneStore` plus `View` plus `Camera` plus the broker façade sequence
> `sync → renderAll → presentAll` plus the `fitPerspectiveViewToPixels` Rect /
> Camera ceremony. `viz::Engine` hides that behind one object that owns its
> `AppContext` (and the `SceneStore` inside it) and forwards to the existing
> broker path, so all `FR-render.*` / `FR-app.*` gates stay via regression lock
> R3. Advanced users keep full access via `appContext()` / `store()`.

## Facade header

```
include/render_engine/engine.hpp   ->   re::viz::Engine   (alias viz::Engine)
```

The header hides `SceneStore` / `Broker` / `AppContext` / `TranslateContext` /
persistence internals for the 80% path and never mentions the persistence key
(`CompositeKey`) — it stays inside `broker/view_synchronizer.*`. The advanced
path stays: `engine.appContext()` gives the full `IViewBridge`, `engine.store()`
gives the `SceneStore`.

Single-site helper `Engine::createView` centralizes the `Rect + Camera + aspect`
ceremony that every sample previously repeated through
`app::fitPerspectiveViewToPixels` plus manual `Rect` plus `Camera` wiring.
No file duplicates that helper.

## Minimal usage (copy-paste, 22 lines — the first file a viz project copies)

`examples/minimal.cpp` is the 22-line copy-paste that a visualization project
starts from — it builds via the installed `RenderEngineConfig.cmake` (`cmake -S
examples -B /tmp/min && cmake --build /tmp/min` green, `find_package(RenderEngine
0.1)` probe). One `Engine` occurrence, `wc -l ==22` committed exact, not a cap.

```cpp
// examples/minimal.cpp — 22 lines, headless offscreen (no Window)
#include "render_engine/engine.hpp"
#include "scene/camera.hpp"
#include "render/offscreen.hpp"
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
int main() {
    re::viz::Engine e;
    auto r = e.addMesh("data/meshes/bunny.obj");
    if (r.failed()) return 1;
    auto id = *r;
    re::scene::Camera cam(glm::vec3(0,0,3), glm::vec3(0,0,0), glm::vec3(0,1,0));
    cam.setPerspective(60, 800.0f/600.0f, 0.1f, 10.0f);
    e.setView({{0,0,800,600}, cam, {id}});
    auto v = e.views().front();
    auto img = re::render::renderOffscreen(800, 600, std::span<const re::scene::View>(&v, 1), e.store());
    if (img.failed()) return 2;
    auto &im = *img;
    (void)im.width(); (void)im.height(); (void)im.pixels().size();
    return 0;
}
```

Window path (samples) — same `Engine` plus a visible `Window`:

```cpp
#include "render_engine/engine.hpp"
#include "core/window.hpp"
auto window = re::core::Window::create(800, 600, "Engine minimal").value();
re::viz::Engine engine;
auto id = *engine.addMesh("data/meshes/bunny.obj", glm::mat4(1.0f), glm::vec4(0.85f,0.45f,0.15f,1.0f));
re::scene::Camera cam(glm::vec3(0,0,3), glm::vec3(0,0,0), glm::vec3(0,1,0));
cam.setPerspective(60.0f, 800.0f/600.0f, 0.1f, 10.0f);
engine.setView({{0,0,800,600}, cam, {id}});
// Or via helper: auto view = re::viz::Engine::createView({0,0,800,600}, cam, {id}); engine.setView(view);
while (!window.shouldClose()) { window.pollEvents(); engine.render().value(); window.swapBuffers(); }
```

Offscreen headless path (tests / server-side visualization) is the same
`renderOffscreen` facade that the T13 smoke gate compares to the direct
`AppContext` oracle within 1/255 on N>=3 consecutive runs (analytic, not
`non-empty`):

```cpp
re::viz::Engine engine;
auto id = *engine.addMesh("data/meshes/bunny.obj", glm::mat4(1.0f), glm::vec4(0.2f,0.4f,0.8f,1.0f));
re::scene::Camera cam(glm::vec3(0,0,3), glm::vec3(0,0,0), glm::vec3(0,1,0));
cam.setPerspective(60.0f, 1.0f, 0.1f, 10.0f);
engine.setView({{0,0,64,64}, cam, {id}});
auto tex = re::core::Texture2D::create().value();
auto fb  = re::core::Framebuffer::create().value();
// allocate tex, attach, then:
engine.render(fb).value();
```

T13 smoke gate: `examples/minimal` via `renderOffscreen` center pixel within
1/255 of the `AppContext` oracle that does the 4-step ceremony manually
(`loadObjMesh → shared_ptr<Mesh> → MeshObject → addMeshObject → View → sync/
renderAll/presentAll`) on N>=3 runs — the analytic headlight color
`(0.85,0.45,0.15)` at the center probe, not `>0`.

## API surface (one class, 80% path)

```
re::viz::Engine

  // Asset loading — typed Result<ObjectId>, domain MeshIo/VolumeIo on failure:
  Result<ObjectId> addMesh(path, mat4, MeshMaterialDesc)   // 4-step ceremony hidden
  Result<ObjectId> addMesh(path, mat4, vec4 baseColor)     // solid-color convenience
  Result<ObjectId> addMesh(path)                           // identity + white (minimal)
  Result<ObjectId> addVolume(path, mat4, TransferFunction)
  Result<ObjectId> addVolume(path, mat4)                   // opaque ramp
  Result<ObjectId> addVolume(path)

  // Views — plain descriptor or full View:
  void setView(ViewDescriptor{rect, camera, ids})          // aggregate {{0,0,w,h}, cam, {id}}
  void setView(View)                                       // full View (lights, clearColor)
  void setViews(vector<View> / span<View>)                 // MPR etc.

  // Single-site view factory — hides Rect+Camera+aspect ceremony:
  static View createView(Rect, Camera, vector<ObjectId>)
  static View createView(int x,int y,int w,int h, Camera, vector<ObjectId>)
  static View createView(int w,int h, Camera, vector<ObjectId>)              // rect 0,0,w,h
  static View createView(int w,int h, float fov,float near,float far, Camera, vector<ObjectId>)

  // Render — forwards to AppContext::bridge().sync/renderAll/presentAll
  Result<void> render(Framebuffer& target)                  // offscreen FBO
  Result<void> render(Framebuffer* target)                  // nullptr = window default FBO
  Result<void> render()                                     // window default FBO

  // Escape hatches (advanced, broker path stays):
  AppContext&  appContext()  / store()   // full SceneStore + Broker
  const vector<View>& views()            // current view list snapshot
```

`Engine` owns one `AppContext` plus one `vector<View>` plus a monotonic view-id
counter. `render()` is the broker triple `sync(views, store) → renderAll() →
presentAll(target)` — byte-identical to the direct `AppContext` oracle the gate
compares against within 1/255 at the analytic center pixel on `N>=3` consecutive
runs. No persistence key appears in the public header.

## Relation to the broker path

```
80% path:   Engine e; e.addMesh(...); e.setView(...); e.render(fb);
advanced:  auto& ctx = e.appContext(); auto& store = e.store();
           ctx.bridge().sync(views, store); ctx.bridge().renderAll(); ...
```

The engine does not replace the broker — it forwards to it. Existing
`samples` and `tests` that use `broker::AppContext` and `scene::SceneStore`
directly remain green; `Engine` is additive.

## Build integration

`include/` is the installed header prefix (`re_engine` interface target
in `CMakeLists.txt`) so consumers write `#include "render_engine/engine.hpp"`:

```cmake
find_package(RenderEngine) # after T2 install/export lands
target_link_libraries(my_app PRIVATE re_engine)
```

Until `T2` export lands, in-tree consumers link `re_engine`:

```cmake
target_link_libraries(my_app PRIVATE re_engine)
```

## Lights — minimal per-View surface (V5 T15, SPEC §12.3 — Phong-only non-goal preserved)

`scene/light.hpp` `re::scene::Light` is the single-struct value type (no `render/light/` hierarchy this iteration — one struct keeps `View::lights` trivial; `Point`/`Spot` remain spec-deferred per §12.3). `View::lights` is `vector<Light>` inline — empty vector = **fixed headlight preserved** (`max(dot(n,(0,0,1)),0)` in `render/shaders/mesh_opaque.frag.glsl` with `uLightDir=(0,0,1)`), so all `FR-render.*` gates stay byte-identical within 1/255 on `N>=3` runs; a mesh rendered with `lights=={}` is pixel-identical to the historical Phong headlight path. `ViewBuilder::withLights(lights)` and `Engine::setLights(viewId, lights)` publish the same value type end-to-end (the 80% viz case): `scene::Light{Directional, dir{-1,-1,-1}}` → `broker::LightMapper` world-space forwarding (normalizes `dir` to `dirWS`, `SPEC §12.5` single responsibility is translation, not view-space multiplication) → `render::ReLight` (`render/light.hpp` uniform-ready) uploaded once per `View` before the `drawLayer` loop via `ViewSynchronizer` per-field `lightsGen` cache (one upload per view, not per item, `SPEC §10.4`). `broker/light_mapper.hpp` is the single Directional minimal path; `Point`/`Spot` fields are carried but unused this iteration (stretch).

`Engine` facade:

```cpp
re::viz::Engine e;
auto id = *e.addMesh("data/meshes/bunny.obj", I, mat);
re::scene::Camera cam(glm::vec3(0,0,3), glm::vec3(0,0,0), glm::vec3(0,1,0));
cam.setPerspective(60, 1, 0.1f, 10.0f);
e.setView({{0,0,800,600}, cam, {id}}); // creates view id 1 with headlight
re::scene::Light l; l.type = re::scene::LightType::Directional; l.dir = glm::vec3(-1,-1,-1);
e.setLights(e.views().front().id, {l}); // one Directional shifts diffuse ≥5/255 deterministically
e.render(fb);
```

`SceneViewBuilder` chaining:

```cpp
auto view = re::scene::SceneViewBuilder{1, {0,0,800,600}}
                .withCamera(cam).withItems({id}).withLights({l}).build();
e.setView(view);
```

Gate: `e.setLights(viewId, {Directional dir{-1,-1,-1}})` renders within 1/255 of direct `scene::View::setLights` + `broker::ViewSynchronizer` path (`N>=3` offscreen parity, analytic not `>0`); empty preserves headlight pixel, one Directional shifts `diffuse` ≥5/255 at the analytic center probe (front-facing `n=(0,0,1)` with headlight shade 1.0 vs `normalize(-1,-1,-1)` shade 0.0 → baseColor `0.85→0.00` delta `217` >>5). `grep -c "class Light" scene/light.hpp ==1` and `grep -c "setLights" include/render_engine/engine.hpp ==1` (analytic `==1`, not `>=1`).

Keep `render::IMaterial→PhongMaterial` single path; this task only wires the value type end-to-end for the 80% viz case — no new `render/light/` hierarchy, no PBR.

## Depth — default divergence (V5 T8b/T17 G4, SPEC §3.1)

Low-level `scene::View`/`render::View` keep `DepthConfig{false}` / `setDepthTest(false)` as default — **color-only deterministic** (`DepthMode::ColorOnly`, depth test disabled, painter's order). This is the configuration every analytic gate was baked against on `GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6` (llvmpipe software GL, no hidden depth interference, byte-identical 1/255 center pixels). `View::depthConfig` is the `scene/depth_config.hpp` value object `DepthConfig{bool enabled{false}; float clearDepth{1.0f};}` owned by `View` (composition, OCP for future `func/writeMask/clearDepth/stencil`); `setDepthConfig(DepthConfig)` bumps `depthTestGen`/`generation` and keeps `depthTest` bool in sync (both spellings stay coherent). `ViewTarget` with `DepthMode::Enabled` owns a `GL_DEPTH_COMPONENT24` texture at `GL_DEPTH_ATTACHMENT` and `REContext::beginPass(depthConfig)` does `if(enabled){enableDepthTest; clearDepth 1.0} else disableDepthTest` once per `View::render` before the `drawLayer` loop; `OIT` capture/composite explicitly `disableDepthTest` on the same `REContext`.

`viz::Engine` diverges for **viz correctness**: `Engine::setView(ViewDescriptor)` / `Engine::createView` default to `DepthConfig{true}` for mesh-containing views (the `applyMeshDepthDefault` single site in `include/render_engine/engine.hpp` — `v.setDepthConfig(DepthConfig{true})` — audit `engine_depth_default` `require_grep DepthConfig\{true` enforces it). Visualization consumers expect true occlusion (nearer fragment wins regardless of draw order) when meshes overlap; low-level tests that assert painter's order keep `false` and stay deterministic. The `Engine` facade documents this divergence so `FR-render.*` gates (color-only) and viz correctness (depth-on) coexist without hidden coupling.

Audit: `tools/audit.rules` `engine_depth_default` (`require_grep DepthConfig\{true`) enforces the facade wiring; `grep -c "setDepthConfig" include/render_engine/engine.hpp ==1 && grep -c "DepthConfig{true" include/render_engine/engine.hpp ==1` (analytic `==1`, not `>=1`). `grep -c "DepthConfig" scene/ ==1` keeps the value object single-site.

Gate: depth-enabled `ViewTarget` `isComplete()==true` with `GL_DEPTH_ATTACHMENT` bound; flag flip `setDepthConfig({true})` → next `ensureTarget()` owns depth, `resize()` preserves; **near-wins 1/255:** two full-screen opaque quads anti-painter order (near FIRST, far LAST) at z=0 vs z=-1, orthographic `[-1,1]^2` 64×64, `depthConfig.enabled==true` → overlap pixel `{0,128,0}` (near wins) vs color-only `{128,0,0}` (far wins) — proves occlusion semantics within 1/255 (`N>=3` via offscreen fixture).

Naming sweep (G6): `docs/spec/*.md` + task comments still cited the legacy prefixed MeshObject — swept to `scene::MeshObject` (`re::scene` namespace is prefix per `NAMING_CONVENTIONS.md §6`); `PerspectiveFraming` already removed T7, no prefixed legacy remains outside `broker/README.md` ACL wording (scoped `app::` generic template params `AppT`/`ReT` kept as placeholder names, not legacy prefixed types). `README.md` module list now enumerates `scene/ broker/ utils/ test_utils/` matching `AUDIT_SOURCE_DIRS` (`io data volume scene core broker render app utils test_utils tests`).
