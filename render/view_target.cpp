// render/view_target.cpp — ViewTarget implementation (SPEC §3.2 V3.4 T5).

#include "render/view_target.hpp"

#include <vector>

#include "core/texture2d.hpp"

namespace re::render {

data::Result<ViewTarget> ViewTarget::create(std::uint32_t width, std::uint32_t height) {
    if (width == 0u || height == 0u) {
        return data::makeError<ViewTarget>(1, "ViewTarget: invalid size 0");
    }
    auto color = core::Texture2D::create();
    if (color.failed()) {
        return data::makeError<ViewTarget>(color.error().code, color.error().message);
    }
    auto fb = core::Framebuffer::create();
    if (fb.failed()) {
        return data::makeError<ViewTarget>(fb.error().code, fb.error().message);
    }
    std::vector<std::uint8_t> zeros(static_cast<std::size_t>(width) * height * 4u, 0u);
    color->bind(0u);
    color->upload(width, height, zeros.data());
    color->unbind(0u);
    fb->bind();
    fb->attachColor(*color);
    if (!fb->isComplete()) {
        fb->unbind();
        return data::makeError<ViewTarget>(1, "ViewTarget: framebuffer incomplete");
    }
    fb->unbind();
    return data::makeValue<ViewTarget>(ViewTarget(std::move(*color), std::move(*fb), width, height));
}

data::Result<void> ViewTarget::resize(std::uint32_t newWidth, std::uint32_t newHeight) {
    if (newWidth == 0u || newHeight == 0u) {
        return data::makeError<void>(1, "ViewTarget: resize to 0");
    }
    auto recreated = ViewTarget::create(newWidth, newHeight);
    if (recreated.failed()) {
        return data::makeError<void>(recreated.error().code, recreated.error().message);
    }
    *this = std::move(*recreated);
    return data::Result<void>(data::value);
}

} // namespace re::render
