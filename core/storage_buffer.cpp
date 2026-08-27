// core/storage_buffer.cpp — RAII shader storage buffer (SSBO) implementation.

#include "core/storage_buffer.hpp"

#include <glad/gl.h>

#include <cstdint>

#include "core/re_context.hpp"

namespace re::core {

data::Result<ShaderStorageBuffer> ShaderStorageBuffer::create() {
    if (glGenBuffers == nullptr) {
        return data::makeError<ShaderStorageBuffer>(
            1, "ShaderStorageBuffer: no GL context (glGenBuffers not loaded)");
    }
    std::uint32_t id = 0u;
    glGenBuffers(1, &id);
    return data::makeValue<ShaderStorageBuffer>(ShaderStorageBuffer(id));
}

ShaderStorageBuffer::ShaderStorageBuffer(ShaderStorageBuffer&& other) noexcept
    : id_(other.id_) {
    other.id_ = 0u;
}

ShaderStorageBuffer& ShaderStorageBuffer::operator=(
    ShaderStorageBuffer&& other) noexcept {
    if (this != &other) {
        if (id_ != 0u) {
            glDeleteBuffers(1, &id_);
        }
        id_ = other.id_;
        other.id_ = 0u;
    }
    return *this;
}

ShaderStorageBuffer::~ShaderStorageBuffer() {
    if (id_ != 0u) {
        glDeleteBuffers(1, &id_);
    }
}

void ShaderStorageBuffer::bind() const noexcept {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, id_);
}

void ShaderStorageBuffer::unbind() const noexcept {
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0u);
}

void ShaderStorageBuffer::bindBase(std::uint32_t index) const noexcept {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(index), id_);
}

void ShaderStorageBuffer::upload(const void* data, std::size_t byteSize,
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
    glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(byteSize),
                 data, targetUsage);
}

data::Result<void> ShaderStorageBuffer::readUint32(std::size_t byteOffset,
                                                   std::size_t count,
                                                   std::uint32_t* out) const {
    // Test-consumed readback via REContext sole anchor (T18) — raw call lives
    // only in core/re_context.cpp, this façade delegates via REContext so no
    // second raw site exists in core/storage_buffer.cpp.
    return REContext::current().readBufferSubData(
        REContext::BufferTarget::ShaderStorage, id_, byteOffset,
        count * sizeof(std::uint32_t), out);
}

} // namespace re::core
