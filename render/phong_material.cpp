// render/phong_material.cpp — PhongMaterial implementation: the single
// material model this engine ships (PBR etc. are deferred non-goals). The
// class is a pure value holder over the Phong parameters; transparency is
// DERIVED from the base alpha so the flag can never disagree with the color
// that is actually uploaded to the shader.

#include "render/phong_material.hpp"

#include <glm/glm.hpp>

namespace re::render {

PhongMaterial::PhongMaterial(glm::vec4 baseColor)
    : baseColor_(glm::clamp(baseColor, 0.0f, 1.0f)) {}

bool PhongMaterial::isTransparent() const noexcept {
    // Transparency is derived, never stored: any base alpha below 1.0 means
    // the surface blends over what is behind it. Keeping the predicate a
    // pure function of baseColor_ makes it impossible for a stale boolean
    // flag to contradict the alpha actually used by the shader.
    return baseColor_.a < 1.0f;
}

glm::vec4 PhongMaterial::baseColor() const noexcept {
    return baseColor_;
}

} // namespace re::render
