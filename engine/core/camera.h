// ============================================================
// FILE:    engine/core/camera.h
// MODULE:  Core > Camera
// PHASE:   1
// STATUS:  FIXED
// PURPOSE: First-person camera: WASD + jump + gravity,
//          mouse look, BSP collision via IPhysicsWorld,
//          view/projection matrix generation.
//
// FIX LOG:
//   1. m_physics changed from void* to IPhysicsWorld* —
//      removes unsafe casts on every use inside camera.cpp.
//   2. m_spaceHeld added — prevents jump from triggering every
//      frame while SPACE is held (one-shot edge detection).
//   3. m_velocity moved to camera state (was local var) so
//      gravity and momentum persist across frames.
//   4. setPhysicsWorld() now takes IPhysicsWorld* directly.
//   5. m_entity added — entity handle for physics integration
//      via moveSlide().
// ============================================================

#pragma once

#include "engine/core/math/vec.h"
#include "engine/core/math/mat4.h"
#include "engine/core/math/quat.h"
#include "engine/entities/entity.h"

namespace nova
{

struct InputState;
class  IPhysicsWorld;

class Camera
{
public:
    Camera();

    void update(const InputState& input, float dt);

    // ---- Physics integration ----
    void           setPhysicsWorld(IPhysicsWorld* world) { m_physics = world; }
    IPhysicsWorld* getPhysicsWorld() const               { return m_physics; }
    EntityHandle getEntity() const { return m_entity; }
    void         setEntity(EntityHandle e) { m_entity = e; }

    // ---- Transform queries ----
    Vec3 getPosition() const { return m_position; }
    Quat getRotation() const { return m_rotation; }
    Mat4 getViewMatrix() const;
    Mat4 getProjectionMatrix() const;
    Mat4 getViewProjectionMatrix() const { return getProjectionMatrix() * getViewMatrix(); }

    // ---- Setters ----
    void setPosition(const Vec3& pos);
    void setFOV(float fovDegrees);
    void setAspect(float aspect);
    void setNearFar(float nearZ, float farZ);
    void setYaw(float yaw) { m_yaw = yaw; m_rotation = Quat::fromEuler(m_pitch, m_yaw, 0.f); }

    // ---- Direction helpers ----
    void lookAt(const Vec3& target);
    Vec3 getForward() const;       // XZ-plane locked (for movement)
    Vec3 getForwardFull() const;   // full 3D gaze direction
    Vec3 getRight() const;         // XZ-plane locked
    Vec3 getUp() const;            // true up based on orientation

    // ---- Speed ----
    float getMoveSpeed() const { return m_moveSpeed; }
    void  setMoveSpeed(float s) { m_moveSpeed = s; }

private:
    void applyMouseLook(float dx, float dy);

    // ---- Transform ----
    Vec3 m_position = {0.f, 0.f, 0.f};
    Vec3 m_velocity = {0.f, 0.f, 0.f};   // persistent velocity (gravity, momentum)
    Quat m_rotation = Quat::identity();
    EntityHandle m_entity = {};            // entity handle for physics

    float m_yaw   = 0.f;
    float m_pitch = 0.f;

    // ---- Projection ----
    float m_fov    = 90.f;
    float m_aspect = 1280.f / 720.f;
    float m_nearZ  = 0.1f;
    float m_farZ   = 8192.f;

    // ---- Control settings ----
    float m_moveSpeed = 400.f;   // units/s — Q2 maps are large
    // Mouse sensitivity in radians per raw pixel of SDL relative motion.
    // SDL3 delivers xrel/yrel as raw pixel deltas at the hardware polling rate.
    // At 800 DPI moving 1 inch = 800 pixels, so:
    //   0.001 rad/px  →  800 * 0.001 = 0.8 rad (46°) per inch  — slow/precise
    //   0.002 rad/px  →  800 * 0.002 = 1.6 rad (92°) per inch  — moderate default
    //   0.004 rad/px  →  800 * 0.004 = 3.2 rad (183°) per inch — fast
    // Tune to taste. The old value 0.08 was ~40× too fast for typical DPI.
    float m_lookSpeed = 0.002f;

    // ---- Physics ----
    IPhysicsWorld* m_physics  = nullptr;
    bool           m_spaceHeld = false;  // edge-detect for jump
};

} // namespace nova
