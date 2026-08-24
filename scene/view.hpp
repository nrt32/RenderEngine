#pragma once

// scene/view.hpp — View description for scene value library (SPEC §3.1, V3.1).
//
// View{rect, plane, itemIds, camera, gen} — pure value type, GL-free, RE-free.

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "scene/camera.hpp"
#include "scene/plane_desc.hpp"

namespace re::scene {

/// Integer rect in physical pixels (framebuffer size, origin bottom-left matching core::setViewport).
struct Rect {
    int x{0};
    int y{0};
    int w{0};
    int h{0};

    bool operator==(const Rect& o) const noexcept {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
    bool operator!=(const Rect& o) const noexcept { return !(*this == o); }
};

/// Scene view: a screen section with rect + optional plane (2D vs 3D) + item list + camera.
///
/// 2D when plane has_value(), 3D when nullopt — plane lives on View, not on item (SPEC §11.4).
/// Per-field generations (rectGen/planeGen/cameraGen/itemsGen) per SPEC §10.4.
struct View {
    uint64_t id{0};
    Rect rect{0, 0, 640, 480};
    std::optional<PlaneDesc> plane{std::nullopt};
    std::vector<uint64_t> itemIds{};
    Camera camera{};
    /// Per-field generations — bumped by setters below. generation() is max.
    uint64_t rectGen{0};
    uint64_t planeGen{0};
    uint64_t cameraGen{0};
    uint64_t itemsGen{0};
    uint64_t generation{0}; // legacy coarse — equals max of per-field.
    /// Clear color of this screen section (consumed by the render-side pass
    /// prologue; see setClearColor).
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 0.0f};
    /// Depth-tested rendering opt-in (see setDepthTest). Default false.
    bool depthTest{false};

    /// Set rect and bump rectGen.
    void setRect(Rect r) noexcept {
        if (rect != r) {
            rect = r;
            ++rectGen;
            ++generation;
        }
    }
    /// Set plane (nullopt = 3D) and bump planeGen only if changed.
    void setPlane(std::optional<PlaneDesc> p) noexcept {
        if (plane != p) {
            plane = std::move(p);
            ++planeGen;
            ++generation;
        }
    }
    /// Set item list and bump itemsGen only if changed.
    void setItemIds(std::vector<uint64_t> ids) noexcept {
        if (itemIds != ids) {
            itemIds = std::move(ids);
            ++itemsGen;
            ++generation;
        }
    }
    /// Mutate camera via callback and bump cameraGen if viewGen or projGen changed.
    template <typename Fn>
    void mutateCamera(Fn&& fn) {
        const uint64_t beforeView = camera.viewGen();
        const uint64_t beforeProj = camera.projGen();
        fn(camera);
        if (camera.viewGen() != beforeView || camera.projGen() != beforeProj) {
            ++cameraGen;
            ++generation;
        }
    }
    /// Set the view's clear color (the RGBA the render-side pass prologue
    /// clears this screen section to) and bump `generation`. Default matches
    /// the engine's historical default (transparent black), so views that
    /// never call this keep their previous behavior.
    void setClearColor(glm::vec4 c) noexcept {
        if (clearColor != c) {
            clearColor = std::move(c);
            ++generation;
        }
    }
    /// Opt this view into depth-tested rendering: the render side recreates
    /// its target with a real depth attachment and enables + clears the depth
    /// test in the pass prologue, so overlapping opaque items resolve by true
    /// occlusion instead of draw order. Bumps `generation` (the poll path sees
    /// the flip); the default stays false — color-only painter's order, the
    /// deterministic-gate configuration.
    void setDepthTest(bool enabled) noexcept {
        if (depthTest != enabled) {
            depthTest = enabled;
            ++generation;
        }
    }
};

} // namespace re::scene
