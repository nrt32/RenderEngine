// core/transform_feedback.cpp — RAII transform-feedback object implementation.

#include "core/transform_feedback.hpp"

#include <glad/gl.h>

namespace re::core {

data::Result<TransformFeedback> TransformFeedback::create() {
    if (glGenTransformFeedbacks == nullptr) {
        return data::makeError<TransformFeedback>(
            1,
            "TransformFeedback: no GL context (glGenTransformFeedbacks not "
            "loaded)");
    }
    std::uint32_t id = 0u;
    glGenTransformFeedbacks(1, &id);
    return data::makeValue<TransformFeedback>(TransformFeedback(id));
}

TransformFeedback::TransformFeedback(TransformFeedback&& other) noexcept
    : id_(other.id_) {
    other.id_ = 0u;
}

TransformFeedback& TransformFeedback::operator=(
    TransformFeedback&& other) noexcept {
    if (this != &other) {
        if (id_ != 0u) {
            glDeleteTransformFeedbacks(1, &id_);
        }
        id_ = other.id_;
        other.id_ = 0u;
    }
    return *this;
}

TransformFeedback::~TransformFeedback() {
    if (id_ != 0u) {
        glDeleteTransformFeedbacks(1, &id_);
    }
}

void TransformFeedback::bind() const noexcept {
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, id_);
}

void TransformFeedback::unbind() const noexcept {
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK, 0u);
}

void TransformFeedback::bindBufferBase(
    std::uint32_t index, const VertexBuffer& buffer) const noexcept {
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, static_cast<GLuint>(index),
                     buffer.id());
}

void TransformFeedback::begin(std::uint32_t mode) const noexcept {
    glBeginTransformFeedback(static_cast<GLenum>(mode));
}

void TransformFeedback::end() const noexcept {
    glEndTransformFeedback();
}

data::Result<void> TransformFeedback::readFloats(const VertexBuffer& buffer,
                                                 std::size_t floatOffset,
                                                 std::size_t count,
                                                 float* out) const {
    if (glGetBufferSubData == nullptr) {
        return data::makeError<void>(
            1,
            "TransformFeedback: no GL context (glGetBufferSubData not "
            "loaded)");
    }
    // glGetBufferSubData reads from the buffer bound to the given target. The
    // capture buffer is bound to GL_TRANSFORM_FEEDBACK_BUFFER (via
    // bindBufferBase), which is a valid binding target for readback.
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, buffer.id());
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,
                       static_cast<GLintptr>(floatOffset * sizeof(float)),
                       static_cast<GLsizeiptr>(count * sizeof(float)), out);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER, 0u);
    return data::Result<void>(data::value);
}

} // namespace re::core
