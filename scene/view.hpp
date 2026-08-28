#pragma once

// scene/view.hpp — View description for scene value library (SPEC §3.1, V3.1).
//
// View{rect, plane, itemIds, camera, gen} — pure value type, GL-free, RE-free.

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "scene/camera.hpp"
#include "scene/layer.hpp"
#include "scene/light.hpp"
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
    uint64_t clearColorGen{0};
    uint64_t depthTestGen{0};
    uint64_t lightsGen{0};
    uint64_t layerGen{0};
    uint64_t generation{0}; // legacy coarse — equals max of per-field.
    /// Clear color of this screen section (consumed by the render-side pass
    /// prologue; see setClearColor).
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 0.0f};
    /// Depth-tested rendering opt-in (see setDepthTest). Default false.
    bool depthTest{false};
    /// Per-View lights: empty vector = unlit (2D) or fixed headlight fallback
    /// (Phong-only non-goal: existing mesh shader headlight preserved when
    /// empty, so empty lights keeps FR-render.* 1/255 gates byte-identical).
    /// Non-empty vector is translated via LightMapper → ReLight before the
    /// drawLayer loop (one upload per view, not per item). T19 stretch.
    std::vector<Light> lights{};
    /// Visual stacking control — per-view bitmask and per-object override map that together replace insertion-order painting with deterministic (layer, techniquePriority) grouping. The mask hides whole layers without removing objects; the map reassigns a single object's effective layer for this view only. Both bump layerGen so the synchronizer re-groups.
    LayerMask layerMask{0xFFu};
    std::unordered_map<uint64_t, Layer> layerOverrides;

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
    /// clears this screen section to) and bump `clearColorGen` plus `generation`.
    /// Default matches the engine's historical default (transparent black), so
    /// views that never call this keep their previous behavior. The dedicated
    /// `clearColorGen` allows the synchronizer's per-field cache (ViewCache)
    /// to skip re-applying the clear color when it hasn't changed — the
    /// per-field generation split keeps color dirt separate from rect/plane
    /// dirt (SPEC §10.4, T9 A5).
    void setClearColor(glm::vec4 c) noexcept {
        if (clearColor != c) {
            clearColor = std::move(c);
            ++clearColorGen;
            ++generation;
        }
    }
    /// Opt this view into depth-tested rendering: the render side recreates
    /// its target with a real depth attachment and enables + clears the depth
    /// test in the pass prologue, so overlapping opaque items resolve by true
    /// occlusion instead of draw order. Bumps `depthTestGen` plus `generation`
    /// (the poll path sees the flip); the default stays false — color-only
    /// painter's order, the deterministic-gate configuration. The dedicated
    /// `depthTestGen` keeps depth dirt separate from other fields (T9 A5).
    void setDepthTest(bool enabled) noexcept {
        if (depthTest != enabled) {
            depthTest = enabled;
            ++depthTestGen;
            ++generation;
        }
    }
    /// Set per-View lights and bump lightsGen + generation when changed.
    /// Part of CompositeKey dirty tracking (SPEC §10): a light tweak dirties
    /// only LightMapper cache via lightsGen, not whole View (per-field split).
    /// Empty vector is the unlit/2D default; non-empty uploads ReLight uniforms
    /// once per view before drawLayer (T19).
    void setLights(std::vector<Light> ls) noexcept {
        if (lights != ls) {
            lights = std::move(ls);
            ++lightsGen;
            ++generation;
        }
    }
    /// Replace the per-view layer visibility mask and bump layerGen. Bits correspond to 1u shifted by layer index; the default covers the eight initial layers. A cleared bit hides every object whose effective layer maps to that bit without touching store generation.
    void setLayerMask(LayerMask m) noexcept;
    /// Assign a per-view override for one object id — its effective layer in this view becomes the supplied layer regardless of its global layer value; the override table is an O(1) unordered_map. Bumps layerGen.
    void setOverride(uint64_t id, Layer l);
    /// Remove a single per-view override entry; bumps layerGen if the entry existed.
    void clearOverride(uint64_t id) noexcept;
    /// Remove all per-view overrides; bumps layerGen if any existed.
    void clearAllOverrides() noexcept;
};

} // namespace re::scene
