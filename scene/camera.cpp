#include "scene/camera.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>

namespace re::scene {

Camera::Camera(glm::vec3 eye, glm::vec3 center, glm::vec3 up) noexcept
    : eye_(eye), center_(center), up_(glm::normalize(up)) {}

glm::mat4 Camera::viewMatrix() const noexcept {
    return glm::lookAt(eye_, center_, up_);
}

glm::mat4 Camera::projMatrix() const noexcept {
    if (projType_ == ProjectionType::Perspective) {
        return glm::perspective(glm::radians(fovDeg_), aspect_, near_, far_);
    }
    return glm::ortho(orthoLeft_, orthoRight_, orthoBottom_, orthoTop_, near_, far_);
}

void Camera::pan(float dx, float dy) noexcept {
    const glm::vec3 dir = glm::normalize(center_ - eye_);
    // Right is cross(dir, up) — world-right of the camera.
    glm::vec3 right = glm::normalize(glm::cross(dir, up_));
    // Guard against degenerate dir||up (looking straight up).
    if (glm::length(right) < 1e-6f) {
        right = glm::vec3{1.0f, 0.0f, 0.0f};
    }
    const glm::vec3 delta = right * dx + up_ * dy;
    eye_ += delta;
    center_ += delta;
    ++viewGen_;
}

void Camera::rotate(float yawDeg, float pitchDeg) noexcept {
    // Orbit eye around center: yaw about world up (0,1,0), pitch about camera right.
    const float yaw = glm::radians(yawDeg);
    const float pitch = glm::radians(pitchDeg);

    glm::vec3 offset = eye_ - center_;
    // Yaw about world up.
    {
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        const glm::vec3 rotated{offset.x * c + offset.z * s, offset.y,
                                -offset.x * s + offset.z * c};
        offset = rotated;
        // Rotate up similarly (keep up roughly world-up for yaw).
        const glm::vec3 upRot{up_.x * c + up_.z * s, up_.y, -up_.x * s + up_.z * c};
        up_ = glm::normalize(upRot);
    }
    // Pitch about camera right.
    if (pitch != 0.0f) {
        const glm::vec3 dir = glm::normalize(-offset);
        glm::vec3 right = glm::normalize(glm::cross(dir, up_));
        if (glm::length(right) < 1e-6f) {
            right = glm::vec3{1.0f, 0.0f, 0.0f};
        }
        const float c = std::cos(pitch);
        const float s = std::sin(pitch);
        // Rodrigues around right axis.
        const glm::vec3 k = right;
        const float kdot = glm::dot(k, offset);
        const glm::vec3 kCross = glm::cross(k, offset);
        offset = offset * c + kCross * s + k * kdot * (1.0f - c);
        // Also rotate up.
        const float kdotUp = glm::dot(k, up_);
        const glm::vec3 kCrossUp = glm::cross(k, up_);
        up_ = glm::normalize(up_ * c + kCrossUp * s + k * kdotUp * (1.0f - c));
    }
    eye_ = center_ + offset;
    ++viewGen_;
}

void Camera::zoom(float factor) noexcept {
    glm::vec3 offset = eye_ - center_;
    offset *= factor;
    // Clamp to avoid collapsing to center.
    if (glm::length(offset) < 1e-4f) {
        offset = glm::normalize(offset) * 1e-4f;
        if (glm::length(offset) < 1e-6f) {
            offset = glm::vec3{0.0f, 0.0f, 1e-4f};
        }
    }
    eye_ = center_ + offset;
    ++viewGen_;
}

void Camera::orbit(float angleDeg, const glm::vec3& axis) noexcept {
    const float angle = glm::radians(angleDeg);
    const glm::vec3 k = glm::normalize(axis);
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    glm::vec3 offset = eye_ - center_;
    const float kdot = glm::dot(k, offset);
    const glm::vec3 kCross = glm::cross(k, offset);
    offset = offset * c + kCross * s + k * kdot * (1.0f - c);
    eye_ = center_ + offset;
    // Rotate up as well.
    const float kdotUp = glm::dot(k, up_);
    const glm::vec3 kCrossUp = glm::cross(k, up_);
    up_ = glm::normalize(up_ * c + kCrossUp * s + k * kdotUp * (1.0f - c));
    ++viewGen_;
}

void Camera::setPerspective(float fovDeg, float aspect, float nearPlane, float farPlane) noexcept {
    bool changed = (projType_ != ProjectionType::Perspective) || (fovDeg_ != fovDeg) ||
                   (aspect_ != aspect) || (near_ != nearPlane) || (far_ != farPlane);
    fovDeg_ = fovDeg;
    aspect_ = aspect;
    near_ = nearPlane;
    far_ = farPlane;
    projType_ = ProjectionType::Perspective;
    if (changed) {
        ++projGen_;
    }
}

void Camera::setOrtho(float left, float right, float bottom, float top, float nearPlane,
                      float farPlane) noexcept {
    bool changed = (projType_ != ProjectionType::Orthographic) || (orthoLeft_ != left) ||
                   (orthoRight_ != right) || (orthoBottom_ != bottom) || (orthoTop_ != top) ||
                   (near_ != nearPlane) || (far_ != farPlane);
    orthoLeft_ = left;
    orthoRight_ = right;
    orthoBottom_ = bottom;
    orthoTop_ = top;
    near_ = nearPlane;
    far_ = farPlane;
    projType_ = ProjectionType::Orthographic;
    if (changed) {
        ++projGen_;
    }
}

Camera Camera::makePerspective(glm::vec3 center, float distance, float fovDeg, float aspect) noexcept {
    Camera cam;
    cam.center_ = center;
    cam.eye_ = center + glm::vec3{0.0f, 0.0f, distance};
    cam.up_ = glm::vec3{0.0f, 1.0f, 0.0f};
    cam.fovDeg_ = fovDeg;
    cam.aspect_ = aspect;
    cam.near_ = 0.1f;
    cam.far_ = 100.0f;
    cam.projType_ = ProjectionType::Perspective;
    return cam;
}

Camera Camera::makePerspectiveCrosshair(glm::vec3 center, float distance, float fovDeg,
                                        float aspect) noexcept {
    // Alias for MPR 3D crosshair view — same deterministic perspective as makePerspective.
    return makePerspective(center, distance, fovDeg, aspect);
}

Camera Camera::makeOrthoForSlice(glm::vec3 center, glm::vec3 planeNormal, float distance) noexcept {
    Camera cam;
    cam.center_ = center;
    const glm::vec3 n = glm::normalize(planeNormal);
    cam.eye_ = center - n * distance;
    // Choose up orthogonal to n.
    glm::vec3 up = glm::vec3{0.0f, 1.0f, 0.0f};
    if (std::abs(glm::dot(up, n)) > 0.99f) {
        up = glm::vec3{1.0f, 0.0f, 0.0f};
    }
    cam.up_ = glm::normalize(up - n * glm::dot(up, n));
    // Deterministic orthographic bounds for the slice gate (-1,1) square.
    cam.projType_ = ProjectionType::Orthographic;
    cam.orthoLeft_ = -1.0f;
    cam.orthoRight_ = 1.0f;
    cam.orthoBottom_ = -1.0f;
    cam.orthoTop_ = 1.0f;
    cam.near_ = 0.1f;
    cam.far_ = 100.0f;
    return cam;
}

} // namespace re::scene
