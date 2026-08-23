#pragma once

// scene/layout.hpp — LayoutSpec + Layout resolver (SPEC §10.5, V3.5 T6).
//
// Relative layout constraints LayoutSpec{row,col,rowSpan,colSpan,weight} →
// Layout::resolve(windowSize, contentScale) absolute Rect in physical
// framebuffer pixels (HiDPI aware). View::rect stores physical pixels
// (glfwGetFramebufferSize); LayoutSpec is serialisable relative form.
// Normalisation: weight = flex (remaining space after fixed spans),
// rowSpan==0 = fill remainder, equal weights → equal distribution,
// remainder pixels to last view (deterministic rounding, within 1 px).
// Physical rect hash includes DPR implicitly via framebufferSize.
//
// Pure value type, header-only, GL-free, RE-free (scene/ owns type).

#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <glm/vec2.hpp>

#include "scene/view.hpp"

namespace re::scene {

/// Relative layout constraint for one View (serialisable, HiDPI-agnostic).
struct LayoutSpec {
    /// Which View this spec positions (stable ViewId from ViewStore).
    uint64_t viewId{0};
    int row{0};
    int col{0};
    int rowSpan{1};
    int colSpan{1};
    float weight{1.0f};

    bool operator==(const LayoutSpec& o) const noexcept {
        return viewId == o.viewId && row == o.row && col == o.col &&
               rowSpan == o.rowSpan && colSpan == o.colSpan && weight == o.weight;
    }
    bool operator!=(const LayoutSpec& o) const noexcept { return !(*this == o); }
};

/// Layout — collection of LayoutSpecs for one LayoutId (SPEC §10.2).
///
/// Layout is a vector<AppView> snapshot with a LayoutId. ViewCompositor
/// key is CompositeKey{LayoutId, ViewId}. Same ViewId in layout A vs B
/// maps to two distinct ReViews.
class Layout {
   public:
    uint64_t layoutId{0};
    std::vector<LayoutSpec> specs{};

    Layout() = default;
    explicit Layout(uint64_t id, std::vector<LayoutSpec> s)
        : layoutId(id), specs(std::move(s)) {}

    /// Resolve relative LayoutSpecs to absolute Rects in physical pixels.
    ///
    /// @param windowSize Logical window size (from glfwGetWindowSize).
    /// @param contentScale Per-axis content scale (from glfwGetWindowContentScale).
    ///                     Physical framebuffer size = windowSize * contentScale
    ///                     (rounded). When caller already has physical size
    ///                     (glfwGetFramebufferSize) pass contentScale {1,1}.
    /// @return Vector of Rects in same order as specs (physical pixels,
    ///         bottom-left origin, matching core::setViewport / ReView rect).
    std::vector<Rect> resolve(glm::ivec2 windowSize, glm::vec2 contentScale) const;

    /// Resolve directly from physical framebuffer size (no scale).
    std::vector<Rect> resolvePhysical(glm::ivec2 framebufferSize) const;

    /// Resolve with ids paired: vector of (viewId, Rect).
    std::vector<std::pair<uint64_t, Rect>> resolveWithIds(glm::ivec2 windowSize,
                                                           glm::vec2 contentScale) const;

    /// Lookup rect for a specific viewId (or nullopt if not in layout).
    std::optional<Rect> rectFor(uint64_t viewId, glm::ivec2 windowSize,
                                glm::vec2 contentScale) const;
};

} // namespace re::scene
