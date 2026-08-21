#pragma once

// core/element_buffer.hpp — RAII element buffer object (EBO) wrapper.
//
// core/ is the SOLE owner of raw GL calls (SPEC S3, guardrail
// gpu_api_ownership); this header is GL-call-free. The raw glGenBuffers /
// glBufferData calls live in element_buffer.cpp.

#include <cstddef>
#include <cstdint>

#include "core/vertex_buffer.hpp"
#include "data/result.hpp"

namespace re::core {

/// RAII wrapper for a GL element buffer object (EBO, GL_ELEMENT_ARRAY_BUFFER).
///
/// Holds the index buffer of a mesh. Movable but not copyable; the GL object
/// is deleted on destruction. Requires a current GL context.
class ElementBuffer {
   public:
    /// Create an EBO (glGenBuffers). Returns an error if no GL context is
    /// current.
    static data::Result<ElementBuffer> create();

    ElementBuffer(const ElementBuffer&) = delete;
    ElementBuffer& operator=(const ElementBuffer&) = delete;

    ElementBuffer(ElementBuffer&& other) noexcept;
    ElementBuffer& operator=(ElementBuffer&& other) noexcept;

    ~ElementBuffer();

    /// Bind to GL_ELEMENT_ARRAY_BUFFER.
    void bind() const noexcept;

    /// Unbind GL_ELEMENT_ARRAY_BUFFER (bind 0).
    void unbind() const noexcept;

    /// Upload `count` 32-bit indices. The buffer must be bound.
    void upload(const std::uint32_t* indices, std::size_t count,
                BufferUsage usage) const noexcept;

    /// The GL object name (non-zero for a valid generated name).
    std::uint32_t id() const noexcept {
        return id_;
    }

    /// True if this object owns a GL buffer name (id != 0).
    bool valid() const noexcept {
        return id_ != 0u;
    }

   private:
    explicit ElementBuffer(std::uint32_t id) noexcept : id_(id) {}

    std::uint32_t id_{0u};
};

} // namespace re::core
