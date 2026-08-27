// core/texture2d.cpp — RAII 2D texture implementation.

#include "core/texture2d.hpp"

#include <glad/gl.h>

#include <cassert>
#include <cstdint>

namespace re::core {

data::Result<Texture2D> Texture2D::create() {
    if (glGenTextures == nullptr) {
        return data::makeError<Texture2D>(
            1, "Texture2D: no GL context (glGenTextures not loaded)");
    }
    std::uint32_t id = 0u;
    glGenTextures(1, &id);
    return data::makeValue<Texture2D>(Texture2D(id));
}

Texture2D::Texture2D(Texture2D&& other) noexcept : id_(other.id_) {
    other.id_ = 0u;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        if (id_ != 0u) {
            glDeleteTextures(1, &id_);
        }
        id_ = other.id_;
        other.id_ = 0u;
    }
    return *this;
}

Texture2D::~Texture2D() {
    if (id_ != 0u) {
        glDeleteTextures(1, &id_);
    }
}

void Texture2D::bind(std::uint32_t unit) const noexcept {
    assert(unit < 16u && "Texture2D::bind unit out of range 0..15");
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, id_);
}

void Texture2D::unbind(std::uint32_t unit) const noexcept {
    assert(unit < 16u && "Texture2D::unbind unit out of range 0..15");
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_2D, 0u);
}

void Texture2D::upload(std::uint32_t width, std::uint32_t height,
                       const std::uint8_t* rgba8Data) const noexcept {
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_RGBA8),
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba8Data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Texture2D::uploadR32UI(std::uint32_t width, std::uint32_t height,
                            const std::uint32_t* data) const noexcept {
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_R32UI),
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                 GL_RED_INTEGER, GL_UNSIGNED_INT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void Texture2D::clearToU32(std::uint32_t value) const noexcept {
    glClearTexImage(id_, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &value);
}

void Texture2D::uploadDepth(std::uint32_t width, std::uint32_t height) const noexcept {
    // DEPTH_COMPONENT24 fixed-point depth (GL_UNSIGNED_INT type over the
    // DEPTH_COMPONENT format) with a null data pointer: storage is allocated
    // for the rasterizer to write, nothing is uploaded. GL_NEAREST + clamp
    // keep the texture framebuffer-complete without implying shader sampling.
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(GL_DEPTH_COMPONENT24),
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height), 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

} // namespace re::core
