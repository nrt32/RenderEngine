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

/// Phong material description (SPEC §12.2 MeshMaterialDesc).
struct PhongDesc {
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec3 specular{0.5f, 0.5f, 0.5f};
    float shininess{32.0f};
    bool doubleSided{false};

    /// True if transparent by baseColor alpha (SPEC §12.2 isTransparent ⇔ a<1).
    bool isTransparent() const noexcept { return baseColor.a < 1.0f; }
};

/// Mesh material desc — variant of Phong (+ future PBR). Single alternative for V3.1.
struct MeshMaterialDesc {
    PhongDesc phong{};
};

/// Volume material desc for VolumePresentation (SPEC §12.2).
struct VolumeMaterialDesc {
    glm::vec4 tint{1.0f};
    bool shading{true};
};

/// App-side material desc variant (closed type set, OCP via visitor — SPEC §12.2).
/// V3.1: Phong-only minimal (Mesh + Volume).
using MaterialDesc = std::variant<MeshMaterialDesc, VolumeMaterialDesc>;

/// Volume presentation — TransferFunction lives BESIDE VolumeMaterial (SPEC §12.5 decision).
/// The TF itself is volume::TransferFunction (pure math); stored by value.
struct VolumePresentation {
    VolumeMaterialDesc material{};
    float stepLength{0.01f};
    bool shading{true};
    // TransferFunction is stored separately by volume_object; see object.hpp for
    // the combined holder that also carries the TF.
};

} // namespace re::scene
