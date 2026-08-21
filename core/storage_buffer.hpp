#pragma once

// core/storage_buffer.hpp — RAII shader storage buffer object (SSBO) wrapper.
//
// core/ is the SOLE owner of raw GL calls (SPEC §3, guardrail
// gpu_api_ownership): higher layers consume GL only through these wrappers.
// This header is GL-call-free; the raw glGenBuffers / glBufferData /
// glBindBufferBase calls live in storage_buffer.cpp.
//
// SSBOs are the GPU-side storage of the v1 order-independent transparency
// pipeline (render/linked_list_oit.cpp): the node buffer holds one record per
// captured fragment and the counter buffer holds the atomic node allocator.

#include <cstddef>
#include <cstdint>

#include "core/vertex_buffer.hpp" // core::BufferUsage
#include "data/result.hpp"

namespace re::core {

/// RAII wrapper for a GL shader storage buffer (SSBO,
/// GL_SHADER_STORAGE_BUFFER).
///
/// The buffer's data store is written by the CPU (upload()) and/or by GPU
/// shaders bound through bindBase() (e.g. atomic operations from fragment
/// shaders). Movable but not copyable; the GL object is deleted on
/// destruction. Requires a current GL context.
class ShaderStorageBuffer {
   public:
    /// Create an SSBO name (glGenBuffers). Returns an error if no GL context is
    /// current (the glad loader has not been initialized).
    static data::Result<ShaderStorageBuffer> create();

    ShaderStorageBuffer(const ShaderStorageBuffer&) = delete;
    ShaderStorageBuffer& operator=(const ShaderStorageBuffer&) = delete;

    ShaderStorageBuffer(ShaderStorageBuffer&& other) noexcept;
    ShaderStorageBuffer& operator=(ShaderStorageBuffer&& other) noexcept;

    ~ShaderStorageBuffer();

    /// Bind to GL_SHADER_STORAGE_BUFFER.
    void bind() const noexcept;

    /// Unbind GL_SHADER_STORAGE_BUFFER (bind 0).
    void unbind() const noexcept;

    /// Bind the whole buffer to shader storage binding point `index`
    /// (glBindBufferBase(GL_SHADER_STORAGE_BUFFER, index, id)), making it
    /// visible to shaders that declare `layout(std430, binding = index)`.
    void bindBase(std::uint32_t index) const noexcept;

    /// Allocate `byteSize` bytes on the GPU and upload `data` (may be null to
    /// only reserve). The buffer must be bound.
    void upload(const void* data, std::size_t byteSize,
                BufferUsage usage) const noexcept;

    /// Read `count` uint32 values starting at `byteOffset` bytes into `out`
    /// (glGetBufferSubData). The buffer must be bound. This is a test-consumed
    /// readback path (guardrail no_production_readback), used by the OIT gate
    /// to observe the node-allocator count. Returns an error if no GL context
    /// is current.
    data::Result<void> readUint32(std::size_t byteOffset, std::size_t count,
                                  std::uint32_t* out) const;

    /// The GL object name. GL reserves 0: a valid generated name is non-zero.
    std::uint32_t id() const noexcept {
        return id_;
    }

    /// True if this object owns a GL buffer name (id != 0).
    bool valid() const noexcept {
        return id_ != 0u;
    }

   private:
    explicit ShaderStorageBuffer(std::uint32_t id) noexcept : id_(id) {}

    std::uint32_t id_{0u};
};

} // namespace re::core
