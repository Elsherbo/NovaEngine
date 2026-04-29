// ============================================================
// FILE:    engine/player/player_controller.cpp
// MODULE:  Engine > Player
// PHASE:   2
// PURPOSE: PlayerController implementation — all movement physics.
//
// Physics model (Q2-style, Y-up engine space):
//   1. Sync position FROM physics storage (ensures frame-0 safety)
//   2. Apply gravity → clamp terminal velocity
//   3. Ground-probe trace → set m_grounded / snap to floor
//   4. Jump impulse (edge-detected via m_spaceHeld)
//   5. Friction (air drag or ground friction)
//   6. PM_Accelerate horizontal acceleration
//   7. Sync velocity TO physics, call moveSlide, read back pos+vel
//   8. Kill vertical velocity on floor/ceiling hit
//   9. Clamp max horizontal speed
// ============================================================

#include "engine/player/player_controller.h"

#include "engine/platform/iplatform.h"
#include "engine/physics/iphysics_world.h"

#include <SDL3/SDL_scancode.h>
#include <cmath>
#include <algorithm>

namespace nova
{

PlayerController::PlayerController() = default;

// ---------------------------------------------------------------------------
// update — main entry point, called once per frame
// ---------------------------------------------------------------------------
void PlayerController::update(const InputState& input, float dt,
                               const Vec3& fwd, const Vec3& right)
{
    // ---- Step 1: Sync origin from physics storage ----
    if (m_physics && m_entity.isValid())
        m_position = m_physics->getOrigin(m_entity);

    // ---- Step 2: Gravity ----
    applyGravity(dt);

    // ---- Step 3: Ground detection ----
    m_grounded = detectGround();

    // ---- Step 4: Jump ----
    if (input.keys[SDL_SCANCODE_SPACE] && !m_spaceHeld && m_grounded)
    {
        m_velocity.y = kPC_JumpSpeed;
        m_grounded   = false;
    }
    m_spaceHeld = input.keys[SDL_SCANCODE_SPACE];

    // ---- Step 5: Build wish direction ----
    Vec3  wishDir = Vec3::zero();
    float speed   = m_moveSpeed;

    if (input.keys[SDL_SCANCODE_LSHIFT] || input.keys[SDL_SCANCODE_RSHIFT])
        speed *= 3.0f;

    if (input.keys[SDL_SCANCODE_W]) wishDir = wishDir + fwd;
    if (input.keys[SDL_SCANCODE_S]) wishDir = wishDir - fwd;
    if (input.keys[SDL_SCANCODE_D]) wishDir = wishDir + right;
    if (input.keys[SDL_SCANCODE_A]) wishDir = wishDir - right;

    float wishLen = wishDir.length();
    if (wishLen > 1.0f)
        wishDir = wishDir * (1.0f / wishLen);

    // ---- Step 6: Friction ----
    applyFriction(dt);

    // ---- Step 7: Horizontal acceleration ----
    if (wishLen > 0.01f)
        applyAcceleration(wishDir, speed, dt);

    if (!m_physics || !m_entity.isValid())
        return;

    // ---- Step 8: Integrate via moveSlide ----
    m_physics->setVelocity(m_entity, m_velocity);
    m_physics->setOrigin(m_entity, m_position);

    Vec3  delta     = m_velocity * dt;
    float deltaDist = delta.length();

    if (deltaDist > 1e-4f)
    {
        TraceResult moveResult = m_physics->moveSlide(m_entity, Vec3::zero(), dt, 0.f);
        (void)moveResult;

        m_position = m_physics->getOrigin(m_entity);
        m_velocity = m_physics->getVelocity(m_entity);

        constexpr float kFloorDot = kPC_GroundNormal;
        constexpr float kCeilDot  = -0.1f;
        if (moveResult.normal.y > kFloorDot && m_velocity.y < 0.0f) m_velocity.y = 0.0f;
        if (moveResult.normal.y < kCeilDot  && m_velocity.y > 0.0f) m_velocity.y = 0.0f;
    }

    // ---- Step 9: Clamp max horizontal speed ----
    float maxSpeed = speed * 2.0f;
    float hspd     = std::sqrt(m_velocity.x * m_velocity.x +
                                m_velocity.z * m_velocity.z);
    if (hspd > maxSpeed)
    {
        float scale   = maxSpeed / hspd;
        m_velocity.x *= scale;
        m_velocity.z *= scale;
    }
}

// ---------------------------------------------------------------------------
// applyGravity
// ---------------------------------------------------------------------------
void PlayerController::applyGravity(float dt)
{
    if (!m_physics) return;
    m_velocity.y -= m_physics->getGravity() * dt;
    if (m_velocity.y < -kPC_TerminalVelocity)
        m_velocity.y = -kPC_TerminalVelocity;
}

// ---------------------------------------------------------------------------
// detectGround
// ---------------------------------------------------------------------------
bool PlayerController::detectGround()
{
    if (!m_physics || !m_entity.isValid())
        return false;

    if (m_velocity.y > 1.0f)
        return false;

    Vec3 probeEnd = { m_position.x,
                      m_position.y - kPC_GroundProbe,
                      m_position.z };

    TraceResult tr = m_physics->trace(m_position, probeEnd,
                                       m_playerMins, m_playerMaxs);

    bool grounded = (tr.fraction < 1.0f && tr.normal.y > kPC_GroundNormal);

    if (grounded && m_velocity.y <= 0.0f)
    {
        m_position   = tr.endPos;
        m_velocity.y = 0.0f;
    }

    return grounded;
}

// ---------------------------------------------------------------------------
// applyFriction
// ---------------------------------------------------------------------------
void PlayerController::applyFriction(float dt)
{
    if (!m_grounded)
    {
        float airDrag = std::pow(1.0f - 2.0f / 60.0f, dt * 60.0f);
        m_velocity.x *= airDrag;
        m_velocity.z *= airDrag;
    }
    else
    {
        float spd = std::sqrt(m_velocity.x * m_velocity.x +
                               m_velocity.z * m_velocity.z);
        if (spd > 1.0f)
        {
            float drop  = spd * kPC_Friction * dt;
            float scale = std::max(0.0f, (spd - drop) / spd);
            m_velocity.x *= scale;
            m_velocity.z *= scale;
        }
        else
        {
            m_velocity.x = 0.0f;
            m_velocity.z = 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// applyAcceleration — Q2 PM_Accelerate style
// ---------------------------------------------------------------------------
void PlayerController::applyAcceleration(const Vec3& wishDir,
                                          float speed, float dt)
{
    float accel  = m_grounded ? kPC_GroundAccel : kPC_AirControl;
    float curSpd = m_velocity.x * wishDir.x + m_velocity.z * wishDir.z;
    float addSpd = speed - curSpd;
    if (addSpd <= 0.0f) return;

    float accelSpd = accel * speed * dt;
    if (accelSpd > addSpd)
        accelSpd = addSpd;

    m_velocity.x += wishDir.x * accelSpd;
    m_velocity.z += wishDir.z * accelSpd;
}

} // namespace nova
