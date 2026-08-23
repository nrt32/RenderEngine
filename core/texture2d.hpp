#pragma once

// core/texture2d.hpp — RAII 2D texture wrapper.
//
// core/ is the SOLE owner of raw GL calls (SPEC S3, guardrail
// gpu_api_ownership); this header is GL-call-free. The raw glGenTextures /
// glTexImage2D calls live in texture2d.cpp.

#include <cstdint>

#include "data/result.hpp"

namespace re::core {

/// RAII wrapper for a GL 2D texture (GL_TEXTURE_2D).
///
/// v1 textures are RGBA8 (4 bytes per pixel). The default min/mag filters are
/// GL_LINEAR and the wrap mode is GL_CLAMP_TO_EDGE (set at upload time), so a
/// texture uploaded without mipmaps is complete and directly attachable to a
/// framebuffer. Movable but not copyable; the GL object is deleted on
/// destruction.
class Texture2D {
   public:
    /// Create a texture name (glGenTextures). Returns an error if no GL
    /// context is current.
    static data::Result<Texture2D> create();

    Texture2D() noexcept = default;

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;

    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    ~Texture2D();

    /// Bind the texture to `unit` (0..15 for GL_TEXTURE0..15).
    void bind(std::uint32_t unit) const noexcept;

    /// Unbind the texture from `unit` (bind 0).
    void unbind(std::uint32_t unit) const noexcept;

    /// Allocate storage and upload an RGBA8 image (4 bytes per pixel,
    /// row-major, bottom-up GL convention: the first row is the bottom).
    /// Also sets GL_LINEAR filtering and GL_CLAMP_TO_EDGE wrapping. The
    /// texture must be bound.
    void upload(std::uint32_t width, std::uint32_t height,
                const std::uint8_t* rgba8Data) const noexcept;

    /// Allocate storage and upload a GL_R32UI image (one 32-bit unsigned
    /// integer per pixel) — the head-pointer texture of the OIT pipeline
    /// (render/linked_list_oit.cpp). Integer textures are sampled with
    /// GL_NEAREST and wrapped with GL_CLAMP_TO_EDGE. The texture must be bound.
    void uploadR32UI(std::uint32_t width, std::uint32_t height,
                     const std::uint32_t* data) const noexcept;

    /// Clear the whole texture's level 0 to `value` (glClearTexImage, GL 4.4+).
    /// The texture must not be bound to an image unit. Used by the OIT pipeline
    /// to reset the per-pixel head pointers to the null sentinel each frame.
    void clearToU32(std::uint32_t value) const noexcept;

    /// The GL object name (non-zero for a valid generated name).
    std::uint32_t id() const noexcept {
        return id_;
    }

    /// True if this object owns a GL texture name (id != 0).
    bool valid() const noexcept {
        return id_ != 0u;
    }

   private:
    explicit Texture2D(std::uint32_t id) noexcept : id_(id) {}

    std::uint32_t id_{0u};
};

} // namespace re::core
