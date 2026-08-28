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

## Minimal usage (copy-paste, ~20 lines)

```cpp
#include "render_engine/engine.hpp"
#include "core/window.hpp"
#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"

int main() {
    // Visible window path (samples) — 20-line copy-paste:
    auto window = re::core::Window::create(800, 600, "Engine minimal").value();
    re::viz::Engine engine;
    auto id = engine.addMesh("data/meshes/bunny.obj",
                             glm::mat4(1.0f),
                             glm::vec4(0.85f, 0.45f, 0.15f, 1.0f)).value();
    re::scene::Camera cam(glm::vec3(0,0,3), glm::vec3(0,0,0),
                          glm::vec3(0,1,0));
    cam.setPerspective(60.0f, 800.0f/600.0f, 0.1f, 10.0f);
    engine.setView({{0,0,800,600}, cam, {id}});
    // Or via helper: auto view = re::viz::Engine::createView({0,0,800,600}, cam, {id});
    // engine.setView(view);
    while (!window.shouldClose()) {
        window.pollEvents();
        engine.render().value(); // sync+render+present to default FBO
        window.swapBuffers();
    }
}
```

Offscreen headless path (tests / server-side visualization):

```cpp
re::viz::Engine engine;
auto id = engine.addMesh("data/meshes/bunny.obj",
                         glm::mat4(1.0f),
                         glm::vec4(0.2f, 0.4f, 0.8f, 1.0f)).value();
re::scene::Camera cam(glm::vec3(0,0,3), glm::vec3(0,0,0),
                      glm::vec3(0,1,0));
cam.setPerspective(60.0f, 1.0f, 0.1f, 10.0f);
engine.setView({{0,0,64,64}, cam, {id}});
auto tex = re::core::Texture2D::create().value();
auto fb  = re::core::Framebuffer::create().value();
// allocate tex, attach, then:
engine.render(fb).value();
```

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
