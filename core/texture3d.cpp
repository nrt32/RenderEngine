// core/texture3d.cpp — RAII 3D texture implementation (GL_TEXTURE_3D).

#include "core/texture3d.hpp"

#include <glad/gl.h>

#include <cassert>
#include <cstdint>

namespace re::core {

data::Result<Texture3D> Texture3D::create() {
    if (glGenTextures == nullptr) {
        return data::makeError<Texture3D>(
            1, "Texture3D: no GL context (glGenTextures not loaded)");
    }
    std::uint32_t id = 0u;
    glGenTextures(1, &id);
    return data::makeValue<Texture3D>(Texture3D(id));
}

Texture3D::Texture3D(Texture3D&& other) noexcept : id_(other.id_) {
    other.id_ = 0u;
}

Texture3D& Texture3D::operator=(Texture3D&& other) noexcept {
    if (this != &other) {
        if (id_ != 0u) {
            glDeleteTextures(1, &id_);
        }
        id_ = other.id_;
        other.id_ = 0u;
    }
    return *this;
}

Texture3D::~Texture3D() {
    if (id_ != 0u) {
        glDeleteTextures(1, &id_);
    }
}

void Texture3D::bind(std::uint32_t unit) const noexcept {
    assert(unit < 16u && "Texture3D::bind unit out of range 0..15");
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_3D, id_);
}

void Texture3D::unbind(std::uint32_t unit) const noexcept {
    assert(unit < 16u && "Texture3D::unbind unit out of range 0..15");
    glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
    glBindTexture(GL_TEXTURE_3D, 0u);
}

void Texture3D::upload(std::uint32_t width, std::uint32_t height,
                       std::uint32_t depth,
                       const float* floatData) const noexcept {
    glTexImage3D(GL_TEXTURE_3D, 0, static_cast<GLint>(GL_R32F),
                 static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                 static_cast<GLsizei>(depth), 0, GL_RED, GL_FLOAT, floatData);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

} // namespace re::core
