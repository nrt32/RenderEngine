#include "render_engine/engine.hpp"
#include "scene/camera.hpp"
#include "render/offscreen.hpp"
#include "utils/asset_utils.hpp"
#include <glm/vec3.hpp>
int main() {
    re::viz::Engine e;
    auto mr = re::utils::loadMeshAsset("data/meshes/bunny.obj");
    if (mr.failed()) return 1;
    auto id = e.addMesh(*mr);
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
