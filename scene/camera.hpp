#pragma once

// scene/camera.hpp — manipulable camera for the scene value library (SPEC §3.1, V3.3 T4).
//
// Pure value type, GL-free, RE-free. Only depends on glm + standard library.
// Namespace re::scene is the prefix — no App prefix. Owns pan/rotate/zoom/orbit
// + factories makeOrthoForSlice / makePerspectiveCrosshair (T4 V3.3) and sends
// only viewMatrix() (+projMatrix(), pos) via broker::CameraMapper to
// render::Camera{view,proj,pos}. Per-field viewGen/projGen split — orbit dirties
// only viewGen (SPEC §10.4). 2D (plane present) → orthographic, 3D → perspective
// validated by mapper; the layer disposition forbids render/ types here —
// scene/ is the app-side library and must stay render-free (broker/ is the
// only library that includes both sides).

#include <cstdint>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace re::scene {

/// Projection mode for scene::Camera (SPEC §3.1 V3.3 T4).
/// 2D slice views (View.plane present) use Orthographic, 3D crosshair views use
/// Perspective. Plane present → ortho is validated by broker::CameraMapper.
enum class ProjectionType : uint8_t {
    Perspective = 0,
    Orthographic = 1,
};

/// Manipulable camera: pan/rotate/zoom/orbit → viewMatrix() + projMatrix().
///
/// View matrix is defined as glm::lookAt(eye, center, up) — analytic within
/// 1e-6 (FR-app.2/3). Projection is either perspective (FOV/aspect/near/far) or
/// orthographic (left/right/bottom/top/near/far) with separate projGen, so a
/// pure orbit/rotate dirties only viewGen per SPEC §10.4 per-field split (T4).
/// Scene sends only the matrices + position via broker::CameraMapper; no
/// render:: type leaks into scene/.
///
/// @par Factories (T4)
/// makeOrthoForSlice() builds an orthographic camera aligned to a slice plane;
/// makePerspective() / makePerspectiveCrosshair() build perspective cameras for
/// 3D views. Both set eye/center/up and the matching projection mode.
class Camera {
   public:
    /// Default camera: eye (0,0,5), center (0,0,0), up (0,1,0), 45° FOV, 1:1
    /// aspect, near 0.1 far 100, perspective mode.
    Camera() noexcept = default;

    /// Construct with explicit eye/center/up (perspective defaults).
    Camera(glm::vec3 eye, glm::vec3 center, glm::vec3 up) noexcept;

    /// Eye position (world space).
    const glm::vec3& eye() const noexcept { return eye_; }
    /// Look-at center (world space).
    const glm::vec3& center() const noexcept { return center_; }
    /// Up vector (world space, normalized).
    const glm::vec3& up() const noexcept { return up_; }

    /// Current projection mode (Perspective for 3D, Orthographic for 2D).
    ProjectionType projectionType() const noexcept { return projType_; }
    /// True when projectionType() == Perspective (3D crosshair).
    bool isPerspective() const noexcept { return projType_ == ProjectionType::Perspective; }
    /// True when projectionType() == Orthographic (2D slice).
    bool isOrthographic() const noexcept { return projType_ == ProjectionType::Orthographic; }

    /// View matrix: lookAt(eye, center, up).
    glm::mat4 viewMatrix() const noexcept;
    /// Projection matrix: perspective(FOV, aspect, near, far) or
    /// ortho(left, right, bottom, top, near, far) depending on projectionType().
    glm::mat4 projMatrix() const noexcept;

    /// Per-field generations for cache keying (SPEC §10.4). viewGen bumps on
    /// pan/rotate/zoom/orbit; projGen bumps only when projection params or mode change.
    uint64_t viewGen() const noexcept { return viewGen_; }
    uint64_t projGen() const noexcept { return projGen_; }
    /// Combined generation (max of view+proj) for coarse poll.
    uint64_t generation() const noexcept { return viewGen_ > projGen_ ? viewGen_ : projGen_; }

    /// Pan in camera local plane: move eye and center by right*dx + up*dy.
    /// @param dx Right displacement (world units).
    /// @param dy Up displacement (world units).
    /// Bumps viewGen only (projGen unchanged).
    void pan(float dx, float dy) noexcept;

    /// Yaw/pitch orbit around center (degrees). Yaw about world up, pitch about
    /// camera right. Equivalent to spherical orbit; bumps viewGen only.
    /// @param yawDeg   Yaw in degrees (around world up).
    /// @param pitchDeg Pitch in degrees (around camera right).
    void rotate(float yawDeg, float pitchDeg) noexcept;

    /// Zoom by scaling eye-center distance: eye = center + (eye-center)*factor.
    /// factor < 1 zooms in, >1 zooms out. Bumps viewGen only.
    void zoom(float factor) noexcept;

    /// Orbit eye around center by angleDeg about axis through center.
    /// @param angleDeg Rotation angle in degrees.
    /// @param axis     World-space axis (normalized internally).
    /// Bumps viewGen only (projGen unchanged) — per-field split invariant.
    void orbit(float angleDeg, const glm::vec3& axis) noexcept;

    /// Set perspective params; switches to Perspective mode; bumps projGen if changed.
    void setPerspective(float fovDeg, float aspect, float nearPlane, float farPlane) noexcept;

    /// Set orthographic params; switches to Orthographic mode; bumps projGen if changed.
    /// @param left   Left clip plane (world).
    /// @param right  Right clip plane.
    /// @param bottom Bottom clip plane.
    /// @param top    Top clip plane.
    /// @param nearPlane Near clip distance.
    /// @param farPlane  Far clip distance.
    void setOrtho(float left, float right, float bottom, float top, float nearPlane,
                  float farPlane) noexcept;

    // --- factories -----------------------------------------------------------

    /// Perspective camera for 3D view: eye distance derived from bounds radius.
    /// Produces a Perspective projection (FOV/aspect/near/far).
    static Camera makePerspective(glm::vec3 center, float distance, float fovDeg = 45.0f,
                                  float aspect = 1.0f) noexcept;

    /// Perspective camera for the MPR 3D crosshair view. Behaviorally
    /// identical to `makePerspective` — the distinct name documents INTENT at
    /// call sites (this camera looks at the slice-intersection point) and
    /// gives the 3D view its own factory, so a future change to crosshair
    /// framing cannot silently alter other perspective users.
    static Camera makePerspectiveCrosshair(glm::vec3 center, float distance,
                                           float fovDeg = 45.0f, float aspect = 1.0f) noexcept;

    /// Orthographic camera for slice views: looks along `-planeNormal` at
    /// `center` from `distance`, so the near/far planes bracket the volume and
    /// the projection is parallel (no depth foreshortening on a cut plane).
    /// Fixed ortho bounds (-1,1,-1,1,0.1,100) scaled for aspect independence
    /// keep slice geometry deterministic across viewports.
    static Camera makeOrthoForSlice(glm::vec3 center, glm::vec3 planeNormal, float distance) noexcept;

   private:
    glm::vec3 eye_{0.0f, 0.0f, 5.0f};
    glm::vec3 center_{0.0f, 0.0f, 0.0f};
    glm::vec3 up_{0.0f, 1.0f, 0.0f};
    ProjectionType projType_{ProjectionType::Perspective};
    float fovDeg_{45.0f};
    float aspect_{1.0f};
    float near_{0.1f};
    float far_{100.0f};
    float orthoLeft_{-1.0f};
    float orthoRight_{1.0f};
    float orthoBottom_{-1.0f};
    float orthoTop_{1.0f};
    uint64_t viewGen_{0};
    uint64_t projGen_{0};
};

} // namespace re::scene
