#pragma once

// render/phong_material.hpp — Phong material model (SPEC §1 "Materials").
//
// The v1 material model, integrated through the modular IMaterial interface.
// Holds the classic Phong shading parameters: a straight RGBA base (diffuse)
// color whose alpha carries opacity (transparency), a specular color, and a
// shininess exponent. isTransparent() is derived from the alpha channel
// (alpha < 1.0), so transparency is a material property (SPEC §3).

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "render/imaterial.hpp"

namespace re::render {

/// Phong material model (ambient + diffuse + specular), with transparency as
/// a material property carried by the alpha channel of the base color.
class PhongMaterial final : public IMaterial {
   public:
    /// Ambient factor (in [0, 1]) scaling the base color under ambient light.
    float ambient = 0.0f;

    /// Diffuse factor (in [0, 1]) scaling the base color under a directional
    /// light (Lambertian term).
    float diffuse = 1.0f;

    /// Specular color (additive highlight term).
    glm::vec3 specular{0.0f};

    /// Phong shininess exponent (>= 1 for a tight highlight).
    float shininess = 32.0f;

    /// Construct with the given straight RGBA base color. Alpha < 1.0 makes
    /// the material transparent.
    explicit PhongMaterial(glm::vec4 baseColor);

    /// True when the base color's alpha is below 1.0 (transparency engaged).
    bool isTransparent() const noexcept override;

    /// The straight RGBA base color.
    glm::vec4 baseColor() const noexcept override;

   private:
    glm::vec4 baseColor_{0.0f, 0.0f, 0.0f, 1.0f};
};

} // namespace re::render
