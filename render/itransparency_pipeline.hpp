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
// Every lifecycle call returns a typed data::Result so pipeline failures
// (storage allocation, shader build, draw issue) surface as typed diagnostics
// instead of failing silently (SPEC §5 "never silent").
//
// Renderers never read back from the GPU through this interface (guardrail
// no_production_readback): pixel/SSBO readbacks are core/ wrappers consumed by
// tests only.

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include "data/result.hpp"
#include "render/types.hpp" // render::Camera / render::RenderTarget

namespace re::render {

class MeshGeometry;

/// Abstraction over an order-independent transparency compositor.
///
/// A pipeline is engaged for the duration of a frame: MeshRenderer calls
/// begin() before drawing a scene that contains transparent materials, hands
/// each transparent mesh to drawTransparent() (capture), and calls end() after
/// (depth-sort + composite into `target`), per SPEC §3.
class ITransparencyPipeline {
   public:
    virtual ~ITransparencyPipeline() = default;

    /// Begin a frame of order-independent compositing into `target`. Called by
    /// MeshRenderer once per render when any material in the scene is
    /// transparent (FR-render.3). The pipeline may (re)allocate its capture
    /// storage to fit `target`'s pixel size.
    ///
    /// Returns a typed error if the capture storage cannot be prepared; on
    /// failure the pipeline stays un-engaged and the frame renders without OIT
    /// (the error propagates to the caller, SPEC §5).
    virtual data::Result<void> begin(const Camera& camera,
                                     const RenderTarget& target) = 0;

    /// Capture `geometry` (a transparent mesh) into the pipeline with
    /// `baseColor` (straight RGBA; the pipeline premultiplies) and `model`
    /// transform, viewed from `camera`. Called by MeshRenderer for every
    /// transparent mesh between begin() and end().
    ///
    /// Returns a typed error if the capture draw cannot be issued (SPEC §5).
    virtual data::Result<void> drawTransparent(const MeshGeometry& geometry,
                                               const glm::vec4& baseColor,
                                               const glm::mat4& model,
                                               const Camera& camera) = 0;

    /// Finish the frame: depth-sort the captured fragments and composite the
    /// result over the current contents of `target` (order-independent).
    ///
    /// Returns a typed error if the composite pass cannot be drawn (SPEC §5).
    /// Draw state (blend, image bindings) is restored whether or not the pass
    /// succeeds.
    virtual data::Result<void> end(const Camera& camera,
                                   const RenderTarget& target) = 0;

    /// True while a begin()/end() frame is in progress (engaged).
    virtual bool isEngaged() const noexcept = 0;
};

} // namespace re::render
