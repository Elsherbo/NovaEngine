// ============================================================
// FILE:    engine/core/camera.cpp
// MODULE:  Core > Camera
// PHASE:   1
// STATUS:  REFACTORED
// PURPOSE: Camera — orientation only (mouse look, view/projection).
//
// All physics simulation (gravity, friction, acceleration, jump,
// moveSlide) has been extracted to game/src/player_controller.cpp.
//
// Retained here:
//   - applyMouseLook()          (yaw/pitch from mouse deltas)
//   - getViewMatrix()           (lookAt from position+orientation)
//   - getProjectionMatrix()     (reversed-Z perspective)
//   - Direction helpers         (getForward, getRight, getUp)
//   - lookAt() utility
// ============================================================

#include "engine/core/camera.h"
#include "engine/core/math/common.h"
#include <cmath>

namespace nova
{

Camera::Camera() = default;

// ---------------------------------------------------------------------------
// applyMouseLook — public, called from game loop or PlayerController
// ---------------------------------------------------------------------------
void Camera::applyMouseLook(float dx, float dy)
{
    m_yaw   -= dx * m_lookSpeed;
    m_pitch -= dy * m_lookSpeed;

    constexpr float maxPitch = kHalfPi - 0.01f;
    m_pitch = clamp(m_pitch, -maxPitch, maxPitch);

    if (m_yaw >  kPi)  m_yaw -= k2Pi;
    if (m_yaw < -kPi)  m_yaw += k2Pi;

    m_rotation = Quat::fromEuler(m_pitch, m_yaw, 0.f);
}

// ---------------------------------------------------------------------------
// lookAt
// ---------------------------------------------------------------------------
void Camera::lookAt(const Vec3& target)
{
    Vec3 forward = (target - m_position).normalized();
    m_yaw   = std::atan2(forward.x, -forward.z);
    m_pitch = std::asin(clamp(-forward.y, -1.0f, 1.0f));
    m_rotation = Quat::fromEuler(m_pitch, m_yaw, 0.f);
}

// ---------------------------------------------------------------------------
// Direction helpers
// ---------------------------------------------------------------------------
Vec3 Camera::getForwardFull() const
{
    return m_rotation.rotate(Vec3::forward());
}

Vec3 Camera::getForward() const
{
    Vec3 f = getForwardFull();
    f.y = 0.f;
    float len = f.length();
    if (len < 1e-6f)
        return Vec3{ std::sin(m_yaw), 0.f, -std::cos(m_yaw) };
    return f * (1.0f / len);
}

Vec3 Camera::getRight() const
{
    Vec3 r = m_rotation.rotate(Vec3::right());
    r.y = 0.f;
    float len = r.length();
    if (len < 1e-6f) return Vec3::right();
    return r * (1.0f / len);
}

Vec3 Camera::getUp() const
{
    Vec3 f = getForwardFull();
    Vec3 r = m_rotation.rotate(Vec3::right());
    return r.cross(f).normalized();
}

// ---------------------------------------------------------------------------
// Matrices
// ---------------------------------------------------------------------------
Mat4 Camera::getViewMatrix() const
{
    Vec3 target = m_position + getForwardFull();
    return Mat4::lookAt(m_position, target, Vec3::up());
}

Mat4 Camera::getProjectionMatrix() const
{
    // Reversed-Z projection: far-plane fragments receive higher float precision.
    // Requires glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE),
    //          glDepthFunc(GL_GREATER), clearDepth(0.f)
    // — all set in gl_backend.cpp.
    return Mat4::perspectiveReverseZ(toRadians(m_fov), m_aspect, m_nearZ, m_farZ);
}

} // namespace nova
