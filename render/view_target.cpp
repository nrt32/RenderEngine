// render/view_target.cpp — ViewTarget implementation: create/resize the
// per-view color texture + framebuffer pair, plus the OPT-IN depth attachment
// (DepthMode::Enabled): a DEPTH_COMPONENT24 texture attached to the FBO's
// depth point whose completeness is asserted together with the color
// attachment. ColorOnly stays the default of every factory path because every
// analytic pixel gate in the suite renders through these targets and must stay
// deterministic painter's-order on software GL; resizing reallocates storage
// in place (preserving the depth mode) so a window resize does not invalidate
// the owning View's handle to this target.

#include "render/view_target.hpp"

#include "core/re_context.hpp"
#include "core/texture2d.hpp"

namespace re::render {

data::Result<ViewTarget> ViewTarget::create(std::uint32_t width,
                                            std::uint32_t height, DepthMode mode) {
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
    std::optional<core::Texture2D> depth;
    if (mode == DepthMode::Enabled) {
        auto depthTexture = core::Texture2D::create();
        if (depthTexture.failed()) {
            return data::makeError<ViewTarget>(depthTexture.error().code,
                                               depthTexture.error().message);
        }
        depth = std::move(*depthTexture);
        depth->bind(0u);
        depth->uploadDepth(width, height);
        depth->unbind(0u);
    }
    // Allocate texture storage without host-side zero fill; the first use
    // clears via the shared REContext beginPass, and a resize clears via
    // glClear rather than uploading a temporary zero buffer.
    color->bind(0u);
    color->upload(width, height, nullptr);
    color->unbind(0u);
    fb->bind();
    fb->attachColor(*color);
    if (depth.has_value()) {
        // An enabled-depth target asserts completeness WITH its depth
        // attachment: isComplete() below now requires BOTH attachments to be
        // valid, so a driver that silently dropped the depth attachment would
        // fail loudly here instead of rendering without occlusion.
        fb->attachDepth(*depth);
    }
    if (!fb->isComplete()) {
        fb->unbind();
        return data::makeError<ViewTarget>(1, "ViewTarget: framebuffer incomplete");
    }
    // Clear the newly allocated color attachment to transparent black via the
    // shared REContext cache rather than a host zero upload. The clear is
    // issued through the REContext so the cache stays coherent with later
    // beginPass calls.
    {
        auto& ctx = core::REContext::current();
        ctx.setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        ctx.clearColor();
    }
    fb->unbind();
    return data::makeValue<ViewTarget>(
        ViewTarget(std::move(*color), std::move(*fb), width, height, mode,
                   std::move(depth)));
}

data::Result<void> ViewTarget::resize(std::uint32_t newWidth, std::uint32_t newHeight) {
    if (newWidth == 0u || newHeight == 0u) {
        return data::makeError<void>(1, "ViewTarget: resize to 0");
    }
    // Recreate at the new size with THIS target's depth mode preserved: a
    // window resize must never silently turn an occlusion-capable view into a
    // painter's-order one (or vice versa).
    auto recreated = ViewTarget::create(newWidth, newHeight, mode_);
    if (recreated.failed()) {
        return data::makeError<void>(recreated.error().code, recreated.error().message);
    }
    *this = std::move(*recreated);
    return data::Result<void>(data::value);
}

} // namespace re::render
