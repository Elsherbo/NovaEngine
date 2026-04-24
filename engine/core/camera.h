// ============================================================
// FILE:    engine/core/camera.h
// MODULE:  Core > Camera
// PHASE:   1
// STATUS:  DONE
// PURPOSE: First-person camera: WASD + QE movement, mouse look,
//          view/projection matrix generation.
// DEPENDS: core/math
//
// FIX LOG:
//   1. [NEW] Added vertical movement: Space = up, C = down (Q2-style
//      noclip flight). Without this, the camera is locked to the XZ
//      plane and can never look at geometry above or below spawn height.
//   2. [NEW] Added setPosition overload and getForwardFull() for
//      unconstrained forward (used for noclip-style free flight).
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

    void update(const InputState &input, float dt);

    // Physics integration - set by engine
    void setPhysicsWorld(void *world) { m_physics = world; }
    void *getPhysicsWorld() const { return m_physics; }

    Vec3 getPosition() const { return m_position; }
    Quat getRotation() const { return m_rotation; }
    Mat4 getViewMatrix() const;
    Mat4 getProjectionMatrix() const;
    Mat4 getViewProjectionMatrix() const { return getProjectionMatrix() * getViewMatrix(); }

    void setPosition(const Vec3 &pos);
    void setFOV(float fovDegrees);
    void setAspect(float aspect);
    void setNearFar(float nearZ, float farZ);
    void setYaw(float yaw) { m_yaw = yaw; m_rotation = Quat::fromEuler(m_pitch, m_yaw, 0.f); }

    void lookAt(const Vec3 &target);
    Vec3 getForward() const;       // horizontal-plane locked (for WASD movement)
    Vec3 getForwardFull() const;   // full 3D forward along view direction
    Vec3 getRight() const;
    Vec3 getUp() const;

    float getMoveSpeed() const { return m_moveSpeed; }
    void  setMoveSpeed(float s) { m_moveSpeed = s; }

private:
    void applyMouseLook(float dx, float dy);

    Vec3 m_position = {0.f, 0.f, 0.f};
    Quat m_rotation = Quat::identity();

    float m_yaw   = 0.f;
    float m_pitch = 0.f;

    float m_fov    = 90.f;
    float m_aspect = 1280.f / 720.f;
    float m_nearZ  = 0.1f;
    float m_farZ   = 8192.f;

    float m_moveSpeed = 400.f;    // raised from 200: Q2 maps are large
    float m_lookSpeed = 0.08f;

    void *m_physics = nullptr;  // IPhysicsWorld*
    bool m_grounded = false;
};

} // namespace nova
