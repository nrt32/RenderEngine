#pragma once

// scene/depth_config.hpp — DepthConfig value object for per-View depth opt-in (SPEC §3.1, V5 T8b).
//
// Depth correctness for visualization consumers needs true occlusion (nearer fragment wins regardless of draw order) but the deterministic llvmpipe analytic gates need color-only painter's order to be reproducible across drivers. The two needs diverge by design: low-level `scene::View`/`render::View` default to color-only (`enabled=false`) so every existing gate that asserts a 1/255 center pixel stays byte-identical without hidden depth interference, while the high-level `viz::Engine` facade (T1) defaults `enabled=true` for mesh-containing views because visualization correctness (viz) wants depth when meshes overlap. DepthConfig is a value object (SRP via composition, OCP for future `func/writeMask/clearDepth/stencil`) so adding `depthFunc` or `clearDepth` later needs only one field here, not a new View boolean plus migration of every caller; View owns it, Renderer stays stateless `drawLayer` and never allocates an FBO.

namespace re::scene {

/// Per-View depth configuration — value object owned by View (T8b, T17 G4).
///
/// Default `enabled=false` is the color-only deterministic configuration every analytic gate was baked against on llvmpipe (`GALLIUM_DRIVER=llvmpipe` `MESA_GL_VERSION_OVERRIDE=4.6`): no depth attachment, depth test disabled, painter's order. `enabled=true` requests a `ViewTarget` with `DepthMode::Enabled` (depth 24-bit at depth attachment, completeness-checked) and a pass prologue that enables + clears depth (`REContext::beginPass(depthConfig)` pattern after T8b).
struct DepthConfig {
    bool enabled{false};
    float clearDepth{1.0f};

    bool operator==(const DepthConfig& o) const noexcept {
        return enabled == o.enabled && clearDepth == o.clearDepth;
    }
    bool operator!=(const DepthConfig& o) const noexcept { return !(*this == o); }
};

} // namespace re::scene
