#include "scene/view.hpp"

// scene/view.cpp — View setters with per-field generation bumps (SPEC §10.4, T8a).
//
// Each setter bumps its dedicated per-field generation plus the coarse generation only
// when the value actually changes (change-guarded, so repeated same-size calls are free).
// Camera changes are via setCamera(Camera) replacing the former lambda (T8a):
// the caller builds a new Camera value and passes it in, the View compares viewGen/projGen
// and eye/center/up to detect a real change, then bumps cameraGen. This keeps View SRP
// (View owns rect/plane/camera/items/clearColor/lights) and the store's bump(FieldId)
// remains the single mutation entry for persistent stores (ViewStore::bump) while View's
// own setters handle the value-object case without a store indirection.

namespace re::scene {

void View::setRect(Rect r) noexcept {
    if (rect != r) {
        rect = r;
        ++rectGen;
        ++generation;
    }
}

void View::setPlane(std::optional<PlaneDesc> p) noexcept {
    if (plane != p) {
        plane = std::move(p);
        ++planeGen;
        ++generation;
    }
}

void View::setItemIds(std::vector<uint64_t> ids) noexcept {
    if (itemIds != ids) {
        itemIds = std::move(ids);
        ++itemsGen;
        ++generation;
    }
}

void View::setCamera(const Camera& cam) noexcept {
    const bool viewChanged = camera.viewGen() != cam.viewGen() || camera.eye() != cam.eye() ||
                             camera.center() != cam.center() || camera.up() != cam.up();
    const bool projChanged = camera.projGen() != cam.projGen() || camera.projMatrix() != cam.projMatrix();
    if (viewChanged || projChanged) {
        camera = cam;
        ++cameraGen;
        ++generation;
    }
}

void View::setCamera(Camera&& cam) noexcept {
    const bool viewChanged = camera.viewGen() != cam.viewGen() || camera.eye() != cam.eye() ||
                             camera.center() != cam.center() || camera.up() != cam.up();
    const bool projChanged = camera.projGen() != cam.projGen() || camera.projMatrix() != cam.projMatrix();
    if (viewChanged || projChanged) {
        camera = std::move(cam);
        ++cameraGen;
        ++generation;
    }
}

void View::setClearColor(glm::vec4 c) noexcept {
    if (clearColor != c) {
        clearColor = c;
        ++clearColorGen;
        ++generation;
    }
}

void View::setLights(std::vector<Light> ls) noexcept {
    if (lights != ls) {
        lights = std::move(ls);
        ++lightsGen;
        ++generation;
    }
}

} // namespace re::scene
