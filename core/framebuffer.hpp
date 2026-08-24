#pragma once

// core/framebuffer.hpp — RAII framebuffer object (FBO) wrapper.
//
// core/ is the SOLE owner of raw GL calls (SPEC §3, guardrail
// gpu_api_ownership); this header is GL-call-free. The raw
// glGenFramebuffers / glFramebufferTexture2D calls live in framebuffer.cpp.

#include <cstdint>

#include "core/texture2d.hpp"
#include "data/result.hpp"

namespace re::core {

/// RAII wrapper for a GL framebuffer object (FBO).
///
/// An FBO is color-only by default: a color attachment (texture) with no depth
/// buffer — that remains the deterministic-gate default configuration of every
/// analytic pixel test (software-GL safe, painter's-order semantics). For
/// targets that need true occlusion, a DEPTH_COMPONENT24 texture can be added
/// as a second attachment via attachDepth(); a color+depth FBO is
/// GL_FRAMEBUFFER_COMPLETE only when BOTH attachments are complete, which
/// isComplete() checks. The default draw/read buffers are
/// GL_COLOR_ATTACHMENT0 / GL_COLOR_ATTACHMENT0 respectively, so a texture
/// attached via attachColor() is rendered to. Movable but not copyable; the GL
/// object is deleted on destruction.
class Framebuffer {
   public:
    /// Create an FBO (glGenFramebuffers). Returns an error if no GL context is
    /// current.
    static data::Result<Framebuffer> create();

    Framebuffer() noexcept = default;

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;

    ~Framebuffer();

    /// Bind as the draw+read framebuffer (GL_FRAMEBUFFER).
    void bind() const noexcept;

    /// Unbind (bind the default framebuffer 0).
    void unbind() const noexcept;

    /// Attach `texture` to GL_COLOR_ATTACHMENT0. The framebuffer must be
    /// bound. The texture must be complete (upload() provides that).
    void attachColor(const Texture2D& texture) const noexcept;

    /// Attach `texture` (DEPTH_COMPONENT24 storage from uploadDepth()) as the
    /// depth attachment GL_DEPTH_ATTACHMENT. The framebuffer must be bound.
    /// After this call isComplete() requires the depth attachment to be valid
    /// too, so an enabled-depth target asserts completeness WITH its depth
    /// attachment — the color-only check alone would no longer describe the
    /// target.
    void attachDepth(const Texture2D& texture) const noexcept;

    /// True if glCheckFramebufferStatus returns GL_FRAMEBUFFER_COMPLETE for
    /// this FBO (the framebuffer must be bound).
    bool isComplete() const noexcept;

    /// The GL object name (non-zero for a valid generated name).
    std::uint32_t id() const noexcept {
        return id_;
    }

    /// True if this object owns a GL FBO name (id != 0).
    bool valid() const noexcept {
        return id_ != 0u;
    }

   private:
    explicit Framebuffer(std::uint32_t id) noexcept : id_(id) {}

    std::uint32_t id_{0u};
};

} // namespace re::core
