// core/vertex_buffer.cpp — RAII vertex buffer object (VBO) implementation.

#include "core/vertex_buffer.hpp"

#include <glad/gl.h>

namespace re::core {

data::Result<VertexBuffer> VertexBuffer::create() {
    if (glGenBuffers == nullptr) {
        return data::makeError<VertexBuffer>(
            1, "VertexBuffer: no GL context (glGenBuffers not loaded)");
    }
    std::uint32_t id = 0u;
    glGenBuffers(1, &id);
    return data::makeValue<VertexBuffer>(VertexBuffer(id));
}

VertexBuffer::VertexBuffer(VertexBuffer&& other) noexcept : id_(other.id_) {
    other.id_ = 0u;
}

VertexBuffer& VertexBuffer::operator=(VertexBuffer&& other) noexcept {
    if (this != &other) {
        if (id_ != 0u) {
            glDeleteBuffers(1, &id_);
        }
        id_ = other.id_;
        other.id_ = 0u;
    }
    return *this;
}

VertexBuffer::~VertexBuffer() {
    if (id_ != 0u) {
        glDeleteBuffers(1, &id_);
    }
}

void VertexBuffer::bind() const noexcept {
    glBindBuffer(GL_ARRAY_BUFFER, id_);
}

void VertexBuffer::unbind() const noexcept {
    glBindBuffer(GL_ARRAY_BUFFER, 0u);
}

void VertexBuffer::upload(const void* data, std::size_t byteSize,
                          BufferUsage usage) const noexcept {
    GLenum targetUsage = GL_STATIC_DRAW;
    switch (usage) {
        case BufferUsage::StaticDraw:
            targetUsage = GL_STATIC_DRAW;
            break;
        case BufferUsage::DynamicDraw:
            targetUsage = GL_DYNAMIC_DRAW;
            break;
        case BufferUsage::StreamDraw:
            targetUsage = GL_STREAM_DRAW;
            break;
    }
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(byteSize), data,
                 targetUsage);
}

} // namespace re::core
