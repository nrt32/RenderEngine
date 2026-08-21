// core/vertex_array.cpp — RAII vertex array object (VAO) implementation.

#include "core/vertex_array.hpp"

#include <glad/gl.h>

#include <cstddef>

namespace re::core {

data::Result<VertexArray> VertexArray::create() {
    if (glGenVertexArrays == nullptr) {
        return data::makeError<VertexArray>(
            1, "VertexArray: no GL context (glGenVertexArrays not loaded)");
    }
    std::uint32_t id = 0u;
    glGenVertexArrays(1, &id);
    return data::makeValue<VertexArray>(VertexArray(id));
}

VertexArray::VertexArray(VertexArray&& other) noexcept : id_(other.id_) {
    other.id_ = 0u;
}

VertexArray& VertexArray::operator=(VertexArray&& other) noexcept {
    if (this != &other) {
        if (id_ != 0u) {
            glDeleteVertexArrays(1, &id_);
        }
        id_ = other.id_;
        other.id_ = 0u;
    }
    return *this;
}

VertexArray::~VertexArray() {
    if (id_ != 0u) {
        glDeleteVertexArrays(1, &id_);
    }
}

void VertexArray::bind() const noexcept {
    glBindVertexArray(id_);
}

void VertexArray::unbind() const noexcept {
    glBindVertexArray(0u);
}

void VertexArray::setAttribute(std::uint32_t index, std::int32_t componentCount,
                               bool normalized, std::size_t strideBytes,
                               std::size_t offsetBytes) const noexcept {
    glEnableVertexAttribArray(index);
    glVertexAttribPointer(index, componentCount, GL_FLOAT,
                          normalized ? GL_TRUE : GL_FALSE,
                          static_cast<GLsizei>(strideBytes),
                          reinterpret_cast<const void*>(offsetBytes));
}

} // namespace re::core
