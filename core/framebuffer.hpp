#pragma once

// core/framebuffer.hpp — RAII framebuffer object (FBO) wrapper.
//
// core/ is the SOLE owner of raw GL calls (SPEC S3, guardrail
// gpu_api_ownership); this header is GL-call-free. The raw
// glGenFramebuffers / glFramebufferTexture2D calls live in framebuffer.cpp.

#include <cstdint>

#include "core/texture2d.hpp"
#include "data/result.hpp"

namespace re::core {

/// RAII wrapper for a GL framebuffer object (FBO).
///
/// v1 FBOs are color-only: a color attachment (texture) with no depth buffer.
/// The default draw/read buffers are GL_COLOR_ATTACHMENT0 /
/// GL_COLOR_ATTACHMENT0 respectively, so a texture attached via attachColor()
/// is rendered to. A color-only FBO whose attachment is a complete texture is
/// GL_FRAMEBUFFER_COMPLETE (checked by isComplete()). Movable but not
/// copyable; the GL object is deleted on destruction.
class Framebuffer {
   public:
    /// Create an FBO (glGenFramebuffers). Returns an error if no GL context is
    /// current.
    static data::Result<Framebuffer> create();

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
