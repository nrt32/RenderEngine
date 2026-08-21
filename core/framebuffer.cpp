// core/framebuffer.cpp — RAII framebuffer object (FBO) implementation.

#include "core/framebuffer.hpp"

#include <glad/gl.h>

#include <cstdint>

namespace re::core {

data::Result<Framebuffer> Framebuffer::create() {
    if (glGenFramebuffers == nullptr) {
        return data::makeError<Framebuffer>(
            1, "Framebuffer: no GL context (glGenFramebuffers not loaded)");
    }
    std::uint32_t id = 0u;
    glGenFramebuffers(1, &id);
    return data::makeValue<Framebuffer>(Framebuffer(id));
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept : id_(other.id_) {
    other.id_ = 0u;
}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    if (this != &other) {
        if (id_ != 0u) {
            glDeleteFramebuffers(1, &id_);
        }
        id_ = other.id_;
        other.id_ = 0u;
    }
    return *this;
}

Framebuffer::~Framebuffer() {
    if (id_ != 0u) {
        glDeleteFramebuffers(1, &id_);
    }
}

void Framebuffer::bind() const noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, id_);
}

void Framebuffer::unbind() const noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, 0u);
}

void Framebuffer::attachColor(const Texture2D& texture) const noexcept {
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture.id(), 0);
}

bool Framebuffer::isComplete() const noexcept {
    return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
}

} // namespace re::core
