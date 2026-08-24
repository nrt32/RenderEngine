#pragma once

// core/texture3d.hpp — RAII 3D texture wrapper (GL_TEXTURE_3D).
//
// core/ is the SOLE owner of raw GL calls (SPEC §3, guardrail
// gpu_api_ownership); this header is GL-call-free. The raw glGenTextures /
// glTexImage3D calls live in texture3d.cpp.
//
// The VolumeRenderer (T9) uploads a data::VolumeDataset's float32 voxel grid as
// a GL_R32F 3D texture and samples it with GL_LINEAR (trilinear) filtering and
// GL_CLAMP_TO_EDGE wrapping, so a shader's texture(volume, uvw) reproduces the
// dataset's CPU trilinear sampling when the texture coordinate maps index
// space to texel space as u = (idx + 0.5) / N (see docs/render.md).

#include <cstdint>

#include "data/result.hpp"

namespace re::core {

/// RAII wrapper for a GL 3D texture (GL_TEXTURE_3D).
///
/// v1 volume textures are single-channel float32 (GL_R32F): the dataset's
/// scalar voxel values. The default min/mag filters are GL_LINEAR and the wrap
/// mode is GL_CLAMP_TO_EDGE (set at upload time), so a texture uploaded
/// without mipmaps is complete. Movable but not copyable; the GL object is
/// deleted on destruction.
class Texture3D {
   public:
    /// Create a texture name (glGenTextures). Returns an error if no GL
    /// context is current.
    static data::Result<Texture3D> create();

    Texture3D(const Texture3D&) = delete;
    Texture3D& operator=(const Texture3D&) = delete;

    Texture3D(Texture3D&& other) noexcept;
    Texture3D& operator=(Texture3D&& other) noexcept;

    ~Texture3D();

    /// Bind the texture to `unit` (0..15 for GL_TEXTURE0..15).
    void bind(std::uint32_t unit) const noexcept;

    /// Unbind the texture from `unit` (bind 0).
    void unbind(std::uint32_t unit) const noexcept;

    /// Allocate storage and upload a single-channel float32 volume
    /// (depth * height * width floats, x-fastest order: index =
    /// x + width*y + width*height*z). Also sets GL_LINEAR filtering and
    /// GL_CLAMP_TO_EDGE wrapping. The texture must be bound.
    void upload(std::uint32_t width, std::uint32_t height, std::uint32_t depth,
                const float* floatData) const noexcept;

    /// The GL object name (non-zero for a valid generated name).
    std::uint32_t id() const noexcept {
        return id_;
    }

    /// True if this object owns a GL texture name (id != 0).
    bool valid() const noexcept {
        return id_ != 0u;
    }

   private:
    explicit Texture3D(std::uint32_t id) noexcept : id_(id) {}

    std::uint32_t id_{0u};
};

} // namespace re::core
