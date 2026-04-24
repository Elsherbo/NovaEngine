// ============================================================
// FILE:    engine/core/camera.cpp
// MODULE:  Core > Camera
// PHASE:   1
// PURPOSE: First-person camera with yaw/pitch, WASD + vertical
//          movement, and view/projection matrix generation.
//
// FIX LOG:
//   1. [NEW] Space = fly up, C = fly down — uses world Y axis.
//      Q2 maps require vertical movement to explore geometry.
//   2. [NEW] getForwardFull() returns the full 3D gaze direction,
//      as opposed to getForward() which flattens to XZ.
//   3. moveSpeed raised from 200 to 400 — Q2 citadel maps are
//      large and 200 units/s makes the engine feel unresponsive.
//      Shift multiplier added (×3) for fast traversal.
// ============================================================
#include "engine/core/camera.h"
#include "engine/platform/iplatform.h"
#include "engine/core/math/common.h"
#include "engine/physics/iphysics_world.h"
#include <SDL3/SDL_scancode.h>

namespace nova
{

Camera::Camera() = default;

void Camera::setPosition(const Vec3 &pos) { m_position = pos; }
void Camera::setFOV(float fovDegrees)     { m_fov = fovDegrees; }
void Camera::setAspect(float aspect)      { m_aspect = aspect; }
void Camera::setNearFar(float n, float f) { m_nearZ = n; m_farZ = f; }

void Camera::lookAt(const Vec3 &target)
{
    Vec3 forward = (target - m_position).normalized();
    m_yaw   = std::atan2(forward.x, -forward.z);
    m_pitch = std::asin(-forward.y);
    m_rotation = Quat::fromEuler(m_pitch, m_yaw, 0.f);
}

// Full 3D forward along the camera gaze direction
Vec3 Camera::getForwardFull() const
{
    return m_rotation.rotate(Vec3::forward());
}

// Horizontal-plane locked forward (XZ only) — used for WASD
Vec3 Camera::getForward() const
{
    Vec3 f = getForwardFull();
    f.y = 0.f;
    if (f.lengthSq() < 1e-6f)
        return Vec3::forward();
    return f.normalized();
}

Vec3 Camera::getRight() const
{
    Vec3 r = m_rotation.rotate(Vec3::right());
    r.y = 0.f;
    if (r.lengthSq() < 1e-6f)
        return Vec3::right();
    return r.normalized();
}

Vec3 Camera::getUp() const
{
    Vec3 f = getForwardFull();
    Vec3 r = m_rotation.rotate(Vec3::right());
    return r.cross(f).normalized();
}

Mat4 Camera::getViewMatrix() const
{
    Vec3 target = m_position + getForwardFull();
    return Mat4::lookAt(m_position, target, Vec3::up());
}

Mat4 Camera::getProjectionMatrix() const
{
    return Mat4::perspective(toRadians(m_fov), m_aspect, m_nearZ, m_farZ);
}

void Camera::applyMouseLook(float dx, float dy)
{
    m_yaw   -= dx * m_lookSpeed;
    m_pitch -= dy * m_lookSpeed;

    constexpr float maxPitch = kHalfPi - 0.01f;
    m_pitch = clamp(m_pitch, -maxPitch, maxPitch);

    if (m_yaw >  kPi) m_yaw -= k2Pi;
    if (m_yaw < -kPi) m_yaw += k2Pi;

    m_rotation = Quat::fromEuler(m_pitch, m_yaw, 0.f);
}

void Camera::update(const InputState &input, float dt)
{
    // ---- Mouse look ----
    if (input.mouseDeltaX != 0 || input.mouseDeltaY != 0)
    {
        applyMouseLook(static_cast<float>(input.mouseDeltaX),
                       static_cast<float>(input.mouseDeltaY));
    }

    // ---- Physics mode (if physics world is set) ----
    if (m_physics)
    {
        // Cast back to IPhysicsWorld
        IPhysicsWorld *phys = (IPhysicsWorld*)m_physics;
        
        // Get movement input
        Vec3 wishDir = {0, 0, 0};
        float speed = m_moveSpeed;
        
        Vec3 fwd = getForward();
        Vec3 right = getRight();
        
        if (input.keys[SDL_SCANCODE_W]) wishDir = wishDir + fwd;
        if (input.keys[SDL_SCANCODE_S]) wishDir = wishDir - fwd;
        if (input.keys[SDL_SCANCODE_D]) wishDir = wishDir + right;
        if (input.keys[SDL_SCANCODE_A]) wishDir = wishDir - right;
        
        if (input.keys[SDL_SCANCODE_LSHIFT] || input.keys[SDL_SCANCODE_RSHIFT])
            speed *= 3.f;
        
// Fallback ground if no collision detected - use spawn height - player height
        float groundZ = m_position.z - 36.0f;
        
        if ((input.keys[SDL_SCANCODE_SPACE]) && (m_position.z >= groundZ + 1.0f))
        {
            // Jump - reset vertical velocity
            m_position.z += 30.0f;
        }
        
        // Apply gravity - fall to ground level
        if (m_position.z > groundZ)
        {
            m_position.z -= 9.8f * dt;
            if (m_position.z < groundZ)
                m_position.z = groundZ;  // Landed
        }
        
        // Use physics moveSlide - need entity handle
        // For camera-only, trace and apply
        if (wishDir.lengthSq() > 0.001f)
        {
            Vec3 delta = wishDir * speed * dt;
            TraceResult tr = phys->trace(m_position, m_position + delta,
                                          {-16, -16, -36}, {16, 16, 36});
            
            if (tr.fraction < 1.0f)
            {
                // Hit - slide along surface
                m_position = tr.endPos;
            }
            else
            {
                m_position = m_position + delta;
            }
        }
        
        return;
    }

    // ---- Fallback: Noclip mode (original behavior) ----
    float speed = m_moveSpeed * dt;
    if (input.keys[SDL_SCANCODE_LSHIFT] || input.keys[SDL_SCANCODE_RSHIFT])
        speed *= 3.f;

    Vec3 fwd   = getForward();
    Vec3 right = getRight();

    // ---- Horizontal movement (WASD) ----
    if (input.keys[SDL_SCANCODE_W]) m_position = m_position + fwd   * speed;
    if (input.keys[SDL_SCANCODE_S]) m_position = m_position - fwd   * speed;
    if (input.keys[SDL_SCANCODE_D]) m_position = m_position + right * speed;
    if (input.keys[SDL_SCANCODE_A]) m_position = m_position - right * speed;

    // ---- Vertical movement (FIX 1: Space = up, C = down) ----
    if (input.keys[SDL_SCANCODE_SPACE]) m_position.y += speed;
    if (input.keys[SDL_SCANCODE_C])     m_position.y -= speed;
}

} // namespace nova
