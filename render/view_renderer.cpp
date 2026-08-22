// render/view_renderer.cpp — ViewRenderer implementation (SPEC §9 V2.4).

#include "render/view_renderer.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/draw.hpp"
#include "render/types.hpp"

namespace re::render {

ViewRenderer::ViewRenderer(std::size_t viewCount, std::uint32_t viewWidth,
                           std::uint32_t viewHeight)
    : viewCount_(viewCount),
      viewWidth_(viewWidth),
      viewHeight_(viewHeight),
      viewTargets_(viewCount) {}

void ViewRenderer::setRenderer(SceneKind kind, IRenderer* renderer) noexcept {
    renderers_[static_cast<std::size_t>(kind)] = renderer;
}

data::Result<void> ViewRenderer::ensureTargets() {
    if (targetsReady_) {
        return data::Result<void>(data::value);
    }
    for (std::size_t i = 0u; i < viewCount_; ++i) {
        auto color = core::Texture2D::create();
        if (color.failed()) {
            return data::makeError<void>(color.error().code,
                                         color.error().message);
        }
        auto framebuffer = core::Framebuffer::create();
        if (framebuffer.failed()) {
            return data::makeError<void>(framebuffer.error().code,
                                         framebuffer.error().message);
        }
        std::vector<std::uint8_t> zeros(
            static_cast<std::size_t>(viewWidth_) * viewHeight_ * 4u, 0u);
        color->bind(0u);
        color->upload(viewWidth_, viewHeight_, zeros.data());
        color->unbind(0u);
        framebuffer->bind();
        framebuffer->attachColor(*color);
        if (!framebuffer->isComplete()) {
            framebuffer->unbind();
            return data::makeError<void>(
                1, "ViewRenderer: per-view framebuffer incomplete");
        }
        framebuffer->unbind();
        viewTargets_[i] =
            ViewTarget(std::move(*color), std::move(*framebuffer));
    }
    targetsReady_ = true;
    return data::Result<void>(data::value);
}

core::Framebuffer* ViewRenderer::viewFramebuffer(
    std::size_t viewIndex) noexcept {
    if (viewIndex >= viewTargets_.size() ||
        !viewTargets_[viewIndex].has_value()) {
        return nullptr;
    }
    return &viewTargets_[viewIndex]->framebuffer;
}

data::Result<void> ViewRenderer::renderViews(const std::vector<View>& views) {
    if (views.size() != viewCount_) {
        return data::makeError<void>(
            1, "ViewRenderer: view count mismatch (views.size()=" +
                   std::to_string(views.size()) +
                   ", constructed count=" + std::to_string(viewCount_) + ")");
    }
    auto targets = ensureTargets();
    if (targets.failed()) {
        return targets;
    }

    for (std::size_t i = 0u; i < views.size(); ++i) {
        const View& view = views[i];
        // Dispatch the scene to the renderer of its technique: the Scene
        // variant's alternative index equals the SceneKind enumerator value
        // (render/view_renderer.hpp static_assert), so the engine always sends
        // a scene to the renderer that owns its technique (SPEC §9 V2.3).
        const std::size_t kindIndex = view.scene.index();
        if (kindIndex >= renderers_.size() ||
            renderers_[kindIndex] == nullptr) {
            return data::makeError<void>(
                2, "ViewRenderer: no renderer registered for view " +
                       std::to_string(i) + "'s scene technique");
        }

        RenderTarget target;
        target.framebuffer = &viewTargets_[i]->framebuffer;
        target.width = viewWidth_;
        target.height = viewHeight_;
        target.clearColor = view.clearColor;

        auto result =
            renderers_[kindIndex]->render(view.scene, view.camera, target);
        if (result.failed()) {
            return result;
        }
    }
    return data::Result<void>(data::value);
}

data::Result<void> ViewRenderer::present(const std::vector<View>& views,
                                         core::Framebuffer* destination) {
    if (views.size() != viewCount_) {
        return data::makeError<void>(
            1, "ViewRenderer: view count mismatch (views.size()=" +
                   std::to_string(views.size()) +
                   ", constructed count=" + std::to_string(viewCount_) + ")");
    }
    // The per-view FBOs must already exist: present() blits what renderViews()
    // rendered into them, so calling it before the first renderViews is a
    // contract violation — rejected with a typed error (code 3), never a blit
    // of unrendered targets.
    if (!targetsReady_) {
        return data::makeError<void>(
            3,
            "ViewRenderer: per-view framebuffers not created (call "
            "renderViews first)");
    }

    // Engine-side blit present: each view's FBO is copied into its pinned
    // window rect via core::blit (the glBlitFramebuffer wrapper under core/,
    // guardrail gpu_api_ownership). The app performs no viewport blending.
    for (std::size_t i = 0u; i < views.size(); ++i) {
        const ViewRect& rect = views[i].rect;
        auto blitResult = core::blit(viewTargets_[i]->framebuffer, 0, 0,
                                     static_cast<int>(viewWidth_),
                                     static_cast<int>(viewHeight_), destination,
                                     rect.x, rect.y, rect.width, rect.height);
        if (blitResult.failed()) {
            return blitResult;
        }
    }
    return data::Result<void>(data::value);
}

data::Result<void> ViewRenderer::render(const std::vector<View>& views,
                                        core::Framebuffer* destination) {
    auto viewsResult = renderViews(views);
    if (viewsResult.failed()) {
        return viewsResult;
    }
    return present(views, destination);
}

} // namespace re::render