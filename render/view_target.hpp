#pragma once

// render/view_target.hpp — ViewTarget: the offscreen render target of one
// view.
//
// One ViewTarget = one Texture2D color attachment + the Framebuffer that
// renders into it, sized exactly to the owning view's rect (rect.w×h), so a
// multi-view window composites by drawing each section into its own target
// and blitting them side-by-side into the shared window framebuffer.
//
// Depth is OPT-IN (architecture review 2026-08-23: v1 targets were color-only
// everywhere, which forced painter's-order workarounds and blocked opaque
// meshes under order-independent transparency). A target constructed with
// DepthMode::Enabled additionally owns a DEPTH_COMPONENT24 texture attached to
// the framebuffer's depth attachment point, and its completeness check then
// covers that depth attachment. The DEFAULT stays ColorOnly — every analytic
// pixel gate of the suite renders through color-only targets, whose output is
// deterministic painter's-order on software GL (llvmpipe) — so enabling depth
// can never silently change an existing gate.
//
// Ownership split (SRP via composition): View owns only view semantics
// (rect+camera+plane+items) and delegates ALL FBO lifecycle — creation,
// resize, clear, attachment — to this class.

#include <cstdint>
#include <optional>

#include "core/framebuffer.hpp"
#include "core/texture2d.hpp"
#include "data/result.hpp"

namespace re::render {

/// Whether a ViewTarget allocates a depth attachment alongside its color
/// attachment. ColorOnly is the default of every factory path: deterministic,
/// painter's-order, software-GL-safe — the configuration all analytic gates
/// assert against. Enabled adds a 24-bit fixed-point depth attachment for
/// views that need true occlusion (nearer fragment wins regardless of draw
/// order); such a target asserts framebuffer completeness WITH the depth
/// attachment at creation time.
enum class DepthMode {
    ColorOnly, ///< Color attachment only (the default; no depth buffer).
    Enabled,   ///< Color + DEPTH_COMPONENT24 depth attachment.
};

/// Per-view FBO target: the color-attachment texture plus the framebuffer that
/// renders into it (textures stay alive for framebuffer lifetime). Sized
/// rect.w×h. Optionally owns a depth-attachment texture (DepthMode::Enabled).
class ViewTarget {
   public:
    /// Create a ViewTarget sized width×height. The default mode ColorOnly has
    /// no depth buffer (deterministic-gate default); DepthMode::Enabled adds
    /// the depth attachment and requires framebuffer completeness including
    /// it. Returns a typed error if GL objects cannot be created or the
    /// framebuffer is incomplete.
    static data::Result<ViewTarget> create(
        std::uint32_t width, std::uint32_t height,
        DepthMode mode = DepthMode::ColorOnly);

    ViewTarget() = default;
    ViewTarget(const ViewTarget&) = delete;
    ViewTarget& operator=(const ViewTarget&) = delete;
    ViewTarget(ViewTarget&&) noexcept = default;
    ViewTarget& operator=(ViewTarget&&) noexcept = default;
    ~ViewTarget() = default;

    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }

    /// The depth mode this target was created with (resize preserves it).
    DepthMode depthMode() const noexcept { return mode_; }

    core::Texture2D& color() noexcept { return color_; }
    const core::Texture2D& color() const noexcept { return color_; }

    /// The depth-attachment texture, or nullptr on a ColorOnly target. Non-
    /// null iff depthMode() == DepthMode::Enabled.
    /// @note lifetime: non-owning view of ViewTarget-owned storage (the
    /// depth_ `optional<>` member) — valid while this ViewTarget is and until
    /// it is moved-from/resized; never delete through it.
    core::Texture2D* /*borrow*/ depth() noexcept {
        return depth_.has_value() ? &*depth_ : nullptr;
    }
    const core::Texture2D* /*borrow*/ depth() const noexcept {
        return depth_.has_value() ? &*depth_ : nullptr;
    }

    /// True when this target has a usable depth attachment (Enabled mode).
    bool hasDepth() const noexcept { return depth_.has_value(); }

    core::Framebuffer& framebuffer() noexcept { return framebuffer_; }
    const core::Framebuffer& framebuffer() const noexcept { return framebuffer_; }

    /// True when this target owns everything its mode promises: the color
    /// attachment + framebuffer always, plus the depth texture when created
    /// with DepthMode::Enabled.
    bool valid() const noexcept {
        return color_.valid() && framebuffer_.valid() &&
               (!depth_.has_value() || depth_->valid());
    }

    /// Recreate storage for newWidth×newHeight (used when View rect resizes),
    /// preserving this target's DepthMode. Returns typed error on GL failure.
    data::Result<void> resize(std::uint32_t newWidth, std::uint32_t newHeight);

   private:
    ViewTarget(core::Texture2D color, core::Framebuffer fb, std::uint32_t w,
               std::uint32_t h, DepthMode mode, std::optional<core::Texture2D> depth)
        : color_(std::move(color)),
          depth_(std::move(depth)),
          framebuffer_(std::move(fb)),
          width_(w),
          height_(h),
          mode_(mode) {}

    core::Texture2D color_{};
    std::optional<core::Texture2D> depth_{std::nullopt};
    core::Framebuffer framebuffer_{};
    std::uint32_t width_{0};
    std::uint32_t height_{0};
    DepthMode mode_{DepthMode::ColorOnly};
};

} // namespace re::render
