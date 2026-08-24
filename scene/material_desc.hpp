#pragma once

// scene/material_desc.hpp — material descriptions for scene value library (SPEC §12.2, V3.1).
//
// Pure value types, GL-free, RE-free. Minimal Phong-only this iteration (SPEC §12 V3.7
// deferred — PBR/Slice/Contour not landed). TransferFunction lives beside
// VolumeMaterial in VolumePresentation per §12.5.

#include <cstdint>
#include <variant>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace re::scene {

/// Phong material parameters for a mesh surface: base color (RGBA, the alpha
/// channel doubles as opacity), specular reflection color and exponent, and
/// double-sided rendering. These are plain values — the render side owns the
/// lighting model that consumes them (SPEC §12.2 MeshMaterialDesc).
struct PhongDesc {
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 specular{0.5f, 0.5f, 0.5f};
    float shininess{32.0f};
    bool doubleSided{false};

    /// Transparency is DERIVED from the material value, never stored: any
    /// alpha below 1 means the surface blends over what is behind it. Keeping
    /// this a function of `baseColor.a` makes it impossible for the flag to
    /// disagree with the color actually uploaded (SPEC §12.2 isTransparent ⇔
    /// alpha < 1).
    bool isTransparent() const noexcept { return baseColor.a < 1.0f; }
};

/// Mesh material description: a closed single-alternative variant so adding
/// another lighting model later (e.g. PBR) is an additive change here without
/// touching MeshObject or its mappers. This iteration ships only Phong.
struct MeshMaterialDesc {
    PhongDesc phong{};
};

/// Volume appearance: a per-object tint multiplied into the ray-cast samples,
/// plus a toggle for gradient-based shading. Kept separate from mesh materials
/// because volume rendering has no surface/specular notion (SPEC §12.2).
struct VolumeMaterialDesc {
    glm::vec4 tint{1.0f};
    bool shading{true};
};

/// Closed set of material descriptions a scene object can carry; visitors
/// switch on the alternative instead of the type adding new subclasses each
/// time (open/closed via std::visit). This iteration is Phong-only minimal:
/// exactly Mesh + Volume alternatives (SPEC §12.2).
using MaterialDesc = std::variant<MeshMaterialDesc, VolumeMaterialDesc>;

/// Volume presentation — TransferFunction lives BESIDE VolumeMaterial (the
/// SPEC §12.5 decision: the TF maps scalar values to colors and belongs with
/// the volume's appearance, not inside it). The TF itself is
/// volume::TransferFunction (pure math); stored by value.
struct VolumePresentation {
    VolumeMaterialDesc material{};
    float stepLength{0.01f};
    bool shading{true};
    // TransferFunction is stored separately by volume_object; see object.hpp for
    // the combined holder that also carries the TF.
};

} // namespace re::scene
