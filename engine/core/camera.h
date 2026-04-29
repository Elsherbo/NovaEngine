// ============================================================
// FILE:    engine/core/camera.h
// MODULE:  Core > Camera
// PHASE:   1
// STATUS:  REFACTORED
// PURPOSE: First-person camera — orientation, view/projection
//          matrix generation, and mouse look ONLY.
//
// After PlayerController extraction, Camera no longer owns:
//   - Gravity / ground detection / terminal velocity
//   - Friction / air drag
//   - Jump logic
//   - Q2 PM_Accelerate horizontal acceleration
//   - moveSlide integration
//
// Camera retains:
//   - applyMouseLook()   — yaw/pitch from raw mouse deltas
//   - getViewMatrix()    — look-at from current position+orientation
//   - getProjectionMatrix() — reversed-Z perspective
//   - Position sync      — caller (game loop / PlayerController) sets pos
//   - Direction helpers  — getForward(), getRight() for movement basis
//
// USAGE (game loop):
//   PlayerController ctrl;
//   Camera           cam;
//
//   // Each frame:
//   ctrl.update(input, dt, cam.getForward(), cam.getRight());
//   cam.setPosition(ctrl.getEyePosition());   // sync pos
//   cam.applyMouseLookPublic(dx, dy);         // rotate view
//   renderer.setViewProj(cam.getViewProjectionMatrix());
// ============================================================

#pragma once

#include "engine/core/math/vec.h"
#include "engine/core/math/mat4.h"
#include "engine/core/math/quat.h"

namespace nova
{

struct InputState;

class Camera
{
public:
    Camera();

    // ---- Mouse look (called each frame with raw SDL relative motion) ----
    // Public wrapper around the internal applyMouseLook() helper so that
    // the game loop or PlayerController can drive it directly.
    void applyMouseLook(float dx, float dy);

    // ---- Transform queries ----
    Vec3 getPosition() const { return m_position; }
    Quat getRotation() const { return m_rotation; }
    Mat4 getViewMatrix()           const;
    Mat4 getProjectionMatrix()     const;
    Mat4 getViewProjectionMatrix() const { return getProjectionMatrix() * getViewMatrix(); }

    // ---- Setters ----
    void setPosition(const Vec3& pos) { m_position = pos; }
    void setFOV(float fovDegrees)     { m_fov      = fovDegrees; }
    void setAspect(float aspect)      { m_aspect   = aspect; }
    void setNearFar(float nearZ, float farZ) { m_nearZ = nearZ; m_farZ = farZ; }
    void setYaw(float yaw)
    {
        m_yaw      = yaw;
        m_rotation = Quat::fromEuler(m_pitch, m_yaw, 0.f);
    }
    void setLookSpeed(float s) { m_lookSpeed = s; }

    // ---- Direction helpers (XZ-plane locked — used by PlayerController) ----
    Vec3 getForward()     const;   // horizontal forward (XZ plane)
    Vec3 getForwardFull() const;   // full 3-D gaze direction
    Vec3 getRight()       const;   // horizontal right (XZ plane)
    Vec3 getUp()          const;   // true up from orientation

    // ---- Utility ----
    void lookAt(const Vec3& target);

private:
    // ---- Transform ----
    Vec3 m_position = {0.f, 0.f, 0.f};
    Quat m_rotation = Quat::identity();

    float m_yaw   = 0.f;
    float m_pitch = 0.f;

    // ---- Projection ----
    float m_fov    = 90.f;
    float m_aspect = 1280.f / 720.f;
    float m_nearZ  = 0.1f;
    float m_farZ   = 8192.f;

    // Mouse sensitivity in radians per raw SDL pixel delta.
    // At 800 DPI:  0.002 rad/px → 1.6 rad (92°) per inch moved.
    float m_lookSpeed = 0.002f;
};

} // namespace nova
