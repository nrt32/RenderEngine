// render/phong_material.cpp — PhongMaterial implementation (SPEC §1, §3).

#include "render/phong_material.hpp"

#include <glm/glm.hpp>

namespace re::render {

PhongMaterial::PhongMaterial(glm::vec4 baseColor)
    : baseColor_(glm::clamp(baseColor, 0.0f, 1.0f)) {}

bool PhongMaterial::isTransparent() const noexcept {
    // Transparency is a material property: any base alpha below 1.0 makes the
    // surface transparent (SPEC §3).
    return baseColor_.a < 1.0f;
}

glm::vec4 PhongMaterial::baseColor() const noexcept {
    return baseColor_;
}

} // namespace re::render
