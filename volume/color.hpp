#pragma once

// volume/color.hpp — RGBA color for the volume/ pure-math module (SPEC §3,
// FR-vol.1/2).
//
// volume/ is GL-free: a color is four plain floats, not a GL type. RgbaColor
// is shared by the TransferFunction (control-point colors, FR-vol.1) and the
// ray-cast compositing math (sample colors, FR-vol.2), so it lives in its own
// tiny header with no source file.

namespace re::volume {

/// An RGBA color with float channels in [0, 1].
///
/// Semantics depend on the consuming API (documented per use):
///   - TransferFunction control-point colors are *straight*
///     (non-premultiplied) RGBA — the ramp interpolates each channel
///     independently;
///   - `compositeFrontToBack` (ray_caster.hpp) returns a *premultiplied*
///     result — RGB is already weighted by alpha.
struct RgbaColor {
    float r{0.0f}; ///< Red channel in [0, 1].
    float g{0.0f}; ///< Green channel in [0, 1].
    float b{0.0f}; ///< Blue channel in [0, 1].
    float a{0.0f}; ///< Alpha channel in [0, 1].
};

} // namespace re::volume
