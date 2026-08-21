// core/element_buffer.cpp — RAII element buffer object (EBO) implementation.

#include "core/element_buffer.hpp"

#include <glad/gl.h>

namespace re::core {

data::Result<ElementBuffer> ElementBuffer::create() {
    if (glGenBuffers == nullptr) {
        return data::makeError<ElementBuffer>(
            1, "ElementBuffer: no GL context (glGenBuffers not loaded)");
    }
    std::uint32_t id = 0u;
    glGenBuffers(1, &id);
    return data::makeValue<ElementBuffer>(ElementBuffer(id));
}

ElementBuffer::ElementBuffer(ElementBuffer&& other) noexcept : id_(other.id_) {
    other.id_ = 0u;
}

ElementBuffer& ElementBuffer::operator=(ElementBuffer&& other) noexcept {
    if (this != &other) {
        if (id_ != 0u) {
            glDeleteBuffers(1, &id_);
        }
        id_ = other.id_;
        other.id_ = 0u;
    }
    return *this;
}

ElementBuffer::~ElementBuffer() {
    if (id_ != 0u) {
        glDeleteBuffers(1, &id_);
    }
}

void ElementBuffer::bind() const noexcept {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, id_);
}

void ElementBuffer::unbind() const noexcept {
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0u);
}

void ElementBuffer::upload(const std::uint32_t* indices, std::size_t count,
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
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(count * sizeof(std::uint32_t)),
                 indices, targetUsage);
}

} // namespace re::core
