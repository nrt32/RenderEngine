#pragma once

// scene/camera.hpp — manipulable camera for the scene value library (SPEC §3.1, V3.1).
//
// Pure value type, GL-free, RE-free. Only depends on glm + standard library.
// Namespace re::scene is the prefix — no App prefix.
// Provides pan/rotate/zoom/orbit that mutate eye/center/up and bump per-field
// generations. viewMatrix() is glm::lookAt(eye, center, up) — analytic within 1e-6.

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace re::scene {

/// Manipulable camera: pan/rotate/zoom/orbit → viewMatrix().
///
/// View matrix is defined as glm::lookAt(eye, center, up). Projection is a
/// simple perspective (FOV/aspect/near/far) with separate projGen, so a pure
/// orbit/rotate dirties only viewGen per SPEC §10.4 per-field split.
class Camera {
   public:
    /// Default camera: eye (0,0,5), center (0,0,0), up (0,1,0), 45° FOV, 1:1
    /// aspect, near 0.1 far 100.
    Camera() noexcept = default;

    /// Construct with explicit eye/center/up.
    Camera(glm::vec3 eye, glm::vec3 center, glm::vec3 up) noexcept;

    /// Eye position (world space).
    const glm::vec3& eye() const noexcept { return eye_; }
    /// Look-at center (world space).
    const glm::vec3& center() const noexcept { return center_; }
    /// Up vector (world space, normalized).
    const glm::vec3& up() const noexcept { return up_; }

    /// View matrix: lookAt(eye, center, up).
    glm::mat4 viewMatrix() const noexcept;
    /// Projection matrix: perspective(FOV, aspect, near, far).
    glm::mat4 projMatrix() const noexcept;

    /// Per-field generations for cache keying (SPEC §10.4). viewGen bumps on
    /// pan/rotate/zoom/orbit; projGen bumps only when projection params change.
    uint64_t viewGen() const noexcept { return viewGen_; }
    uint64_t projGen() const noexcept { return projGen_; }
    /// Combined generation (max of view+proj) for coarse poll.
    uint64_t generation() const noexcept { return viewGen_ > projGen_ ? viewGen_ : projGen_; }

    /// Pan in camera local plane: move eye and center by right*dx + up*dy.
    /// @param dx Right displacement (world units).
    /// @param dy Up displacement (world units).
    void pan(float dx, float dy) noexcept;

    /// Yaw/pitch orbit around center (degrees). Yaw about world up, pitch about
    /// camera right. Equivalent to spherical orbit; bumps viewGen.
    /// @param yawDeg   Yaw in degrees (around world up).
    /// @param pitchDeg Pitch in degrees (around camera right).
    void rotate(float yawDeg, float pitchDeg) noexcept;

    /// Zoom by scaling eye-center distance: eye = center + (eye-center)*factor.
    /// factor < 1 zooms in, >1 zooms out. Bumps viewGen.
    void zoom(float factor) noexcept;

    /// Orbit eye around center by angleDeg about axis through center.
    /// @param angleDeg Rotation angle in degrees.
    /// @param axis     World-space axis (normalized internally).
    void orbit(float angleDeg, const glm::vec3& axis) noexcept;

    /// Set perspective params; bumps projGen if changed.
    void setPerspective(float fovDeg, float aspect, float nearPlane, float farPlane) noexcept;

    // --- factories -----------------------------------------------------------

    /// Perspective camera for 3D view (crosshair): eye distance derived from
    /// bounds radius.
    static Camera makePerspective(glm::vec3 center, float distance, float fovDeg = 45.0f,
                                  float aspect = 1.0f) noexcept;

    /// Orthographic helper for slice views (plane-normal aligned). Returns a
    /// camera looking along -planeNormal at center.
    static Camera makeOrthoForSlice(glm::vec3 center, glm::vec3 planeNormal, float distance) noexcept;

   private:
    glm::vec3 eye_{0.0f, 0.0f, 5.0f};
    glm::vec3 center_{0.0f, 0.0f, 0.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    float fovDeg_{45.0f};
    float aspect_{1.0f};
    float near_{0.1f};
    float far_{100.0f};
    uint64_t viewGen_{0};
    uint64_t projGen_{0};
};

} // namespace re::scene
