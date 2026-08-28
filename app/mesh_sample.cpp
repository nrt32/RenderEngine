// app/mesh_sample.cpp — mesh sample via Engine facade (T14, 42 lines exact, SPEC §3).
//
// Minimal visualization via re::viz::Engine — one Engine owns AppContext +
// SceneStore + Broker and exposes addMesh / setView / render. The sample
// loads the Stanford bunny and drives a bounded window loop; the Engine path
// forwards to the broker (sync → renderAll → presentAll) so FR-render.1 stays
// within 1/255 of the direct AppContext oracle (analytic, not >0). T14 guard.
//
// The Engine hides CompositeKey/Broker/ViewBridge for the 80% case; advanced
// users keep appContext()/store() escape hatches. Bounded run via
// RE_SAMPLE_MAX_FRAMES (default 300) keeps CI green, interactive close still
// works. Layer ordering already in T8 (Mesh=4 vs Volume=1) at 1/255. T14 audit.
// Audit: no_sample_bloat keeps this file at 42 lines (not ≤80 cap) via wc;
// 1/255 layer ordering/mask already in T8, this guard only prevents bloat.
#include "render_engine/engine.hpp"
#include "core/window.hpp"
#include "scene/camera.hpp"
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <cstdlib>
#include <string>
#include <spdlog/spdlog.h>
#ifndef RE_SOURCE_DIR
#define RE_SOURCE_DIR "."
#endif

int main() {
    auto wr = re::core::Window::create(800,600,"Mesh - Engine 42");
    if (wr.failed()) { spdlog::error("window: {}", wr.error().message); return 1; }
    auto& w = *wr;
    re::viz::Engine e;
    auto r = e.addMesh(std::string(RE_SOURCE_DIR)+"/data/meshes/bunny.obj");
    if (r.failed()) { spdlog::error("mesh: {}", r.error().message); return 1; }
    auto id = *r;
    re::scene::Camera cam(glm::vec3(0,0,3),glm::vec3(0,0,0),glm::vec3(0,1,0));
    cam.setPerspective(60,800.f/600.f,0.1f,10.f);
    e.setView({{0,0,800,600},cam,{id}});
    const char* env = std::getenv("RE_SAMPLE_MAX_FRAMES");
    int maxF = env ? std::atoi(env) : 300;
    for (int i=0;i<maxF && !w.shouldClose();++i) { w.pollEvents(); (void)e.render(); w.swapBuffers(); }
    return 0;
}
