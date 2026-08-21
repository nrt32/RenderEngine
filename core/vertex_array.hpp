#pragma once

// core/vertex_array.hpp — RAII vertex array object (VAO) wrapper.
//
// core/ is the SOLE owner of raw GL calls (SPEC S3, guardrail
// gpu_api_ownership); this header is GL-call-free. The raw
// glGenVertexArrays / glVertexAttribPointer calls live in vertex_array.cpp.

#include <cstddef>
#include <cstdint>

#include "data/result.hpp"

namespace re::core {

/// RAII wrapper for a GL vertex array object (VAO).
///
/// Captures vertex-attribute configuration (index, component count, stride,
/// offset). v1 vertex data is float-only, so the element type is fixed to
/// GL_FLOAT and the API exposes the component count. Movable but not
/// copyable; the GL object is deleted on destruction.
class VertexArray {
   public:
    /// Create a VAO (glGenVertexArrays). Returns an error if no GL context is
    /// current.
    static data::Result<VertexArray> create();

    VertexArray(const VertexArray&) = delete;
    VertexArray& operator=(const VertexArray&) = delete;

    VertexArray(VertexArray&& other) noexcept;
    VertexArray& operator=(VertexArray&& other) noexcept;

    ~VertexArray();

    /// Bind the VAO (GL_VERTEX_ARRAY_BINDING). Attribute configuration and
    /// element-buffer association are captured while bound.
    void bind() const noexcept;

    /// Unbind the VAO (bind 0).
    void unbind() const noexcept;

    /// Enable + configure attribute `index` as `componentCount` floats
    /// (GL_FLOAT). `strideBytes`/`offsetBytes` describe the interleaved vertex
    /// layout. A vertex buffer must be bound to GL_ARRAY_BUFFER when calling
    /// this (the binding is captured by the VAO), per the GL spec.
    void setAttribute(std::uint32_t index, std::int32_t componentCount,
                      bool normalized, std::size_t strideBytes,
                      std::size_t offsetBytes) const noexcept;

    /// The GL object name (non-zero for a valid generated name).
    std::uint32_t id() const noexcept {
        return id_;
    }

    /// True if this object owns a GL VAO name (id != 0).
    bool valid() const noexcept {
        return id_ != 0u;
    }

   private:
    explicit VertexArray(std::uint32_t id) noexcept : id_(id) {}

    std::uint32_t id_{0u};
};

} // namespace re::core
