#pragma once

// scene/view.hpp — View description for scene value library (SPEC §3.1, V3.1, T8a).
//
// View is View{rect, clearColor, Camera, plane, lights, itemIds} per modules.md:20 — six core
// fields each with a per-field generation for the broker's per-field cache (SPEC §10.4) so a
// Camera orbit dirties only the camera cache and a material tweak does not force a plane re-translate.
// Depth presentation is opt-in via DepthConfig value object (T4) — View owns DepthConfig directly
// with depthConfigGen; ViewState{clearColor,DepthConfig} composition was considered for SRP but
// deferred as View retains exactly six core fields/gens and the extra presentation state travels
// with View without an extra indirection (engine facade defaults DepthConfig{true} for viz while
// low-level View defaults DepthConfig{false} for deterministic llvmpipe gates).

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec4.hpp>

#include "scene/camera.hpp"
#include "scene/depth_config.hpp"
#include "scene/light.hpp"
#include "scene/plane_desc.hpp"

namespace re::scene {

struct Rect {
    int x{0};
    int y{0};
    int w{0};
    int h{0};
    bool operator==(const Rect& o) const noexcept { return x == o.x && y == o.y && w == o.w && h == o.h; }
    bool operator!=(const Rect& o) const noexcept { return !(*this == o); }
};

struct View {
    uint64_t id{0};
    Rect rect{0, 0, 640, 480};
    std::optional<PlaneDesc> plane{std::nullopt};
    std::vector<uint64_t> itemIds{};
    Camera camera{};
    glm::vec4 clearColor{0.0f, 0.0f, 0.0f, 0.0f};
    DepthConfig depthConfig{DepthConfig{false, 1.0f}};
    std::vector<Light> lights{};
    uint64_t rectGen{0};
    uint64_t planeGen{0};
    uint64_t cameraGen{0};
    uint64_t itemsGen{0};
    uint64_t clearColorGen{0};
    uint64_t depthConfigGen{0};
    uint64_t lightsGen{0};
    uint64_t generation{0};
    void setRect(Rect r) noexcept;
    void setPlane(std::optional<PlaneDesc> p) noexcept;
    void setItemIds(std::vector<uint64_t> ids) noexcept;
    void setCamera(const Camera& cam) noexcept;
    void setCamera(Camera&& cam) noexcept;
    void setClearColor(glm::vec4 c) noexcept;
    void setDepthConfig(DepthConfig cfg) noexcept;
    void setLights(std::vector<Light> ls) noexcept;
};

} // namespace re::scene
