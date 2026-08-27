#pragma once

// render/linked_list_oit.hpp — per-pixel linked-list OIT pipeline (SPEC §3,
// FR-render.2/3).
//
// LinkedListOIT is the v1 ITransparencyPipeline implementation. It implements
// the classic per-pixel linked-list order-independent transparency algorithm
// (capture -> depth-sort -> composite):
//
//   1. begin()   sizes the capture storage to the target's pixel dimensions,
//                clears the head-pointer texture and the node allocator.
//   2. capture   each transparent mesh's fragments are written to a GPU node
//                buffer (SSBO) with an atomically allocated node index; the
//                per-pixel head-pointer texture stores the newest node.
//   3. end()     a full-screen pass reads each pixel's linked list, insertion-
//                sorts the nodes by depth (near -> far), composites back-to-
//                front with the premultiplied-alpha "over" operator, and blends
//                the accumulated transparent color over the target's existing
//                (opaque) contents.
//
// render/ is GL-call-free: all raw GL (SSBOs, image store, memory barriers,
// blend state) goes through core/ RAII objects and the core::Draw API
// (guardrail gpu_api_ownership).

#include <cstddef>
#include <cstdint>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>
#include <optional>

#include "core/shader_program.hpp"
#include "core/storage_buffer.hpp"
#include "core/texture2d.hpp"
#include "render/itransparency_pipeline.hpp"
#include "render/screen_quad.hpp"

namespace re::render {

/// v1 per-pixel linked-list order-independent transparency pipeline.
///
/// The pipeline is swappable (SPEC §3) and owns only GL resources: two shader
/// programs (capture + composite), a shared full-screen quad, one R32UI
/// head-pointer texture (per-pixel linked-list heads, updated atomically via
/// imageAtomicExchange), and two SSBOs (node buffer + node allocator).
/// Non-copyable; movable not needed by MeshRenderer (injected by pointer).
class LinkedListOIT final : public ITransparencyPipeline {
   public:
    /// Construct with the maximum number of captured fragments per pixel
    /// (the composite pass sorts at most this many nodes per pixel). The value
    /// is clamped to [1, 16] (the composite shader's fixed local array size).
    explicit LinkedListOIT(std::uint32_t maxFragmentsPerPixel = 16u);

    LinkedListOIT(const LinkedListOIT&) = delete;
    LinkedListOIT& operator=(const LinkedListOIT&) = delete;

    data::Result<void> begin(const Camera& camera,
                             const RenderTarget& target,
                             core::REContext& ctx) override;
    data::Result<void> drawTransparent(const MeshGeometry& geometry,
                                       const glm::vec4& baseColor,
                                       const glm::mat4& model,
                                       const Camera& camera) override;
    data::Result<void> end(const Camera& camera,
                           const RenderTarget& target,
                           core::REContext& ctx) override;
    bool isEngaged() const noexcept override;

    /// Test-consumed readback (guardrail no_production_readback): read the
    /// node-allocator counter back from the GPU — the number of fragments
    /// captured during the most recent frame. The render path never reads
    /// back; tests call this after end() for FR-render.2 evidence. Returns a
    /// typed error if no frame has been begun yet or no GL context is current.
    data::Result<std::uint32_t> readCapturedFragmentCount();

    /// The maximum fragments-per-pixel this pipeline captures/sorts.
    std::uint32_t maxFragmentsPerPixel() const noexcept {
        return maxFragmentsPerPixel_;
    }

   private:
    /// Build (and cache) the capture program. Non-null on success.
    data::Result<core::ShaderProgram*> captureProgram();

    /// Build (and cache) the composite program. Non-null on success.
    data::Result<core::ShaderProgram*> compositeProgram();

    /// Build (and cache) the shared full-screen quad. Non-null on success.
    /// @note lifetime: non-owning view of pipeline-owned storage (the
    /// screenQuad_ `optional<>` member) — valid while this pipeline is.
    data::Result<core::VertexArray*> screenQuad();

    /// Reallocate (if needed) the head texture and node/counter SSBOs to fit
    /// `width` x `height` pixels. Returns an error if any GL object cannot be
    /// created.
    data::Result<void> ensureCapacity(std::uint32_t width,
                                      std::uint32_t height);

    /// The current node-buffer capacity: width * height * maxFragmentsPerPixel
    /// (the capture shader's node-allocator bound). 0 before first begin().
    std::uint32_t nodeCapacity() const noexcept {
        return width_ * height_ * maxFragmentsPerPixel_;
    }

    std::uint32_t maxFragmentsPerPixel_{16u};

    std::optional<core::ShaderProgram> captureProgram_;
    std::optional<core::ShaderProgram> compositeProgram_;

    std::optional<ScreenQuad> screenQuad_;

    std::optional<core::Texture2D> headTexture_;
    std::optional<core::ShaderStorageBuffer> nodeBuffer_;
    std::optional<core::ShaderStorageBuffer> counterBuffer_;

    std::uint32_t width_{0u};
    std::uint32_t height_{0u};
    bool engaged_{false};
};

} // namespace re::render
