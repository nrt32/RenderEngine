#pragma once

// render/imaterial.hpp — modular material interface (SPEC §3, §1 "Materials").
//
// Materials are integrated through a modular interface (SOLID: open/closed,
// dependency inversion) so additional models (PBR, toon, …) can be added
// without touching the renderer core. Transparency is a property of the
// material: a transparent material (isTransparent() == true) is drawn through
// the injected ITransparencyPipeline; an opaque one is drawn by the plain
// forward pass (SPEC §3, FR-render.3).

#include <glm/vec4.hpp>

namespace re::render {

/// Abstraction over a surface material. Renderers depend on this interface,
/// never on a concrete material class (dependency inversion, SPEC §3).
class IMaterial {
   public:
    virtual ~IMaterial() = default;

    /// True if the material has transparency (alpha < 1) and therefore must
    /// be composited order-independently (SPEC §3 "OIT is a characteristic").
    virtual bool isTransparent() const noexcept = 0;

    /// The material's straight (non-premultiplied) RGBA base/diffuse color.
    /// The alpha channel carries the material's opacity: alpha == 1.0 is a
    /// fully opaque surface.
    virtual glm::vec4 baseColor() const noexcept = 0;
};

} // namespace re::render
