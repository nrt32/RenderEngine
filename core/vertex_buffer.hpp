#pragma once

// core/vertex_buffer.hpp — RAII vertex buffer object (VBO) wrapper.
//
// core/ is the SOLE owner of raw GL calls (SPEC S3, guardrail
// gpu_api_ownership): higher layers consume GL only through these wrappers.
// This header is GL-call-free; the raw glGenBuffers/glBufferData calls live in
// vertex_buffer.cpp.

#include <cstddef>
#include <cstdint>

#include "data/result.hpp"

namespace re::core {

/// Hint for how the buffer's data will be used (maps to GL_STATIC_DRAW etc.).
enum class BufferUsage {
    StaticDraw,  ///< Data set once, used many times (GL_STATIC_DRAW).
    DynamicDraw, ///< Data changed frequently (GL_DYNAMIC_DRAW).
    StreamDraw,  ///< Data changed every use (GL_STREAM_DRAW).
};

/// RAII wrapper for a GL vertex buffer object (VBO, GL_ARRAY_BUFFER).
///
/// Movable but not copyable; the GL object is deleted on destruction. Requires
/// a current GL context (provided by core::OffscreenContext in tests).
class VertexBuffer {
   public:
    /// Create a VBO (glGenBuffers). Returns an error if no GL context is
    /// current (the glad loader has not been initialized).
    static data::Result<VertexBuffer> create();

    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;

    VertexBuffer(VertexBuffer&& other) noexcept;
    VertexBuffer& operator=(VertexBuffer&& other) noexcept;

    ~VertexBuffer();

    /// Bind to GL_ARRAY_BUFFER.
    void bind() const noexcept;

    /// Unbind GL_ARRAY_BUFFER (bind 0).
    void unbind() const noexcept;

    /// Allocate `byteSize` bytes on the GPU and upload `data` (may be null to
    /// only reserve). The buffer must be bound.
    void upload(const void* data, std::size_t byteSize,
                BufferUsage usage) const noexcept;

    /// The GL object name. GL reserves 0: a valid generated name is non-zero.
    std::uint32_t id() const noexcept {
        return id_;
    }

    /// True if this object owns a GL buffer name (id != 0).
    bool valid() const noexcept {
        return id_ != 0u;
    }

   private:
    explicit VertexBuffer(std::uint32_t id) noexcept : id_(id) {}

    std::uint32_t id_{0u};
};

} // namespace re::core
