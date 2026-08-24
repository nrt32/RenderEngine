#pragma once

// core/transform_feedback.hpp — RAII transform-feedback object wrapper.
//
// core/ is the SOLE owner of raw GL calls (SPEC §3, guardrail
// gpu_api_ownership): higher layers consume GL only through these wrappers.
// This header is GL-call-free; the raw glGenTransformFeedbacks /
// glBeginTransformFeedback / glEndTransformFeedback / glBindBufferBase /
// glGetBufferSubData calls live in transform_feedback.cpp.
//
// Transform feedback captures the primitives a geometry (or vertex) shader
// emits into a buffer, letting tests observe exactly which vertices were
// emitted. The v1 consumer is the SliceRenderer cross-section gate (T11,
// FR-render.4): the geometry shader clips a mesh against a plane and emits the
// on-plane cross-section vertices, which are captured here and read back to
// assert they lie on the plane. Reading back the captured vertices is a
// test-consumed readback path (guardrail no_production_readback), so it lives
// under core/.

#include <cstddef>
#include <cstdint>

#include "core/vertex_buffer.hpp"
#include "data/result.hpp"

namespace re::core {

/// Primitive type a transform-feedback capture consumes (glBeginTransformFeedback's mode).
///
/// core/-owned named constants: higher layers must never spell raw GL enums
/// (they would need the glad include, which is confined to core/ — guardrail
/// gpu_api_ownership). begin() maps each value to its GL constant internally.
enum class PrimitiveMode {
    Points,        ///< GL_POINTS
    Triangles,     ///< GL_TRIANGLES (MeshGeometry's indexed triangle draw)
    TriangleStrip, ///< GL_TRIANGLE_STRIP (geometry-shader outputs)
};

/// RAII wrapper for a GL transform feedback object
/// (GL_TRANSFORM_FEEDBACK).
///
/// A transform feedback object records the vertices emitted by the current
/// program (whose varyings were declared at link time) into buffers bound via
/// bindBufferBase(). Movable but not copyable; the GL object is deleted on
/// destruction. Requires a current GL context.
class TransformFeedback {
   public:
    /// Create a transform-feedback name (glGenTransformFeedbacks). Returns an
    /// error if no GL context is current (the glad loader has not been
    /// initialized).
    static data::Result<TransformFeedback> create();

    TransformFeedback(const TransformFeedback&) = delete;
    TransformFeedback& operator=(const TransformFeedback&) = delete;

    TransformFeedback(TransformFeedback&& other) noexcept;
    TransformFeedback& operator=(TransformFeedback&& other) noexcept;

    ~TransformFeedback();

    /// Bind this object to GL_TRANSFORM_FEEDBACK (glBindTransformFeedback).
    void bind() const noexcept;

    /// Unbind GL_TRANSFORM_FEEDBACK (bind 0).
    void unbind() const noexcept;

    /// Bind `buffer` to the GL_TRANSFORM_FEEDBACK_BUFFER binding point `index`
    /// of this transform-feedback object (glBindBufferBase). Captured varyings
    /// declared with `layout(xfb_buffer = index, ...)` (or the default buffer
    /// 0) are written into `buffer` during capture.
    void bindBufferBase(std::uint32_t index,
                        const VertexBuffer& buffer) const noexcept;

    /// Begin transform-feedback capture in `mode`, matching the program's
    /// output primitive type (glBeginTransformFeedback; the GL constant is
    /// mapped from the core-owned PrimitiveMode enum internally). Must be
    /// called while the program is in use and this object is bound.
    void begin(PrimitiveMode mode) const noexcept;

    /// End transform-feedback capture (glEndTransformFeedback).
    void end() const noexcept;

    /// Read back `count` float values captured into `buffer`, starting at
    /// `floatOffset` floats into the buffer, into `out` (glGetBufferSubData).
    /// The buffer must have been bound via bindBufferBase(). This is a
    /// test-consumed readback path (guardrail no_production_readback); the
    /// render path never reads back from the GPU. Returns an error if no GL
    /// context is current.
    data::Result<void> readFloats(const VertexBuffer& buffer,
                                  std::size_t floatOffset, std::size_t count,
                                  float* out) const;

    /// The GL object name. GL reserves 0: a valid generated name is non-zero.
    std::uint32_t id() const noexcept {
        return id_;
    }

    /// True if this object owns a GL transform-feedback name (id != 0).
    bool valid() const noexcept {
        return id_ != 0u;
    }

   private:
    explicit TransformFeedback(std::uint32_t id) noexcept : id_(id) {}

    std::uint32_t id_{0u};
};

} // namespace re::core
