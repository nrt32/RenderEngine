#pragma once

// render/view_target.hpp — ViewTarget: the offscreen color target of one view.
//
// One ViewTarget = one Texture2D color attachment + the Framebuffer that
// renders into it, sized exactly to the owning view's rect (rect.w×h), so a
// multi-view window composites by drawing each section into its own target
// and blitting them side-by-side into the shared window framebuffer.
// Ownership split (SRP via composition): View owns only view semantics
// (rect+camera+plane+items) and delegates ALL FBO lifecycle — creation,
// resize, clear, attachment — to this class (SPEC §3.2 V3.4 T5).

#include <cstdint>

#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/result.hpp"

namespace re::render {

/// Per-view FBO target: the color-attachment texture plus the framebuffer that
/// renders into it (texture stays alive for framebuffer lifetime). Sized rect.w×h.
class ViewTarget {
   public:
    /// Create a ViewTarget sized width×height (color-only, no depth). Returns
    /// a typed error if GL objects cannot be created or the framebuffer is
    /// incomplete.
    static data::Result<ViewTarget> create(std::uint32_t width, std::uint32_t height);

    ViewTarget() = default;
    ViewTarget(const ViewTarget&) = delete;
    ViewTarget& operator=(const ViewTarget&) = delete;
    ViewTarget(ViewTarget&&) noexcept = default;
    ViewTarget& operator=(ViewTarget&&) noexcept = default;
    ~ViewTarget() = default;

    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }

    core::Texture2D& color() noexcept { return color_; }
    const core::Texture2D& color() const noexcept { return color_; }
    core::Framebuffer& framebuffer() noexcept { return framebuffer_; }
    const core::Framebuffer& framebuffer() const noexcept { return framebuffer_; }

    bool valid() const noexcept { return color_.valid() && framebuffer_.valid(); }

    /// Recreate storage for newWidth×newHeight (used when View rect resizes).
    /// Returns typed error on GL failure.
    data::Result<void> resize(std::uint32_t newWidth, std::uint32_t newHeight);

   private:
    ViewTarget(core::Texture2D color, core::Framebuffer fb, std::uint32_t w, std::uint32_t h)
        : color_(std::move(color)), framebuffer_(std::move(fb)), width_(w), height_(h) {}

    core::Texture2D color_{};
    core::Framebuffer framebuffer_{};
    std::uint32_t width_{0};
    std::uint32_t height_{0};
};

} // namespace re::render
