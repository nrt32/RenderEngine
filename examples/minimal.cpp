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
    (void)im.width();
    (void)im.height();
    (void)im.pixels().size();
    return 0;
}
