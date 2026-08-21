#pragma once

// render/itransparency_pipeline.hpp — order-independent transparency pipeline
// interface (SPEC §3, FR-render.3).
//
// OIT is a characteristic of the scene, not a peer renderer: transparency
// lives on IMaterial, and MeshRenderer auto-engages the injected
// ITransparencyPipeline only when some mesh's material is transparent. An
// opaque-only scene never engages it (FR-render.3). The interface is swappable
// (open/closed, dependency inversion) so future OIT variants need no renderer
// changes. v1 supplies the LinkedListOIT implementation (T10).
//
// T7 defines the interface so an injectable spy (test double) can confirm the
// pipeline stays off for opaque scenes; LinkedListOIT arrives in T10.

namespace re::render {

// Forward declarations of the scene structs shared with MeshRenderer.
struct Camera;
struct RenderTarget;

/// Abstraction over an order-independent transparency compositor.
///
/// A pipeline is engaged for the duration of a frame: MeshRenderer calls
/// begin() before drawing a scene that contains transparent materials and
/// end() after, so the pipeline can capture fragments, depth-sort them, and
/// composite the result into `target` (SPEC §3).
class ITransparencyPipeline {
   public:
    virtual ~ITransparencyPipeline() = default;

    /// Begin a frame of order-independent compositing into `target`. Called by
    /// MeshRenderer once per render when any material in the scene is
    /// transparent (FR-render.3).
    virtual void begin(const Camera& camera, const RenderTarget& target) = 0;

    /// Finish the frame and present the composited result to `target`.
    virtual void end(const Camera& camera, const RenderTarget& target) = 0;

    /// True while a begin()/end() frame is in progress (engaged).
    virtual bool isEngaged() const noexcept = 0;
};

} // namespace re::render
