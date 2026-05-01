// ============================================================
// FILE:    engine/player/player_controller.cpp
// MODULE:  Engine > Player
// PHASE:   2  (updated: CVar integration)
// PURPOSE: PlayerController implementation.
//
// CHANGE (CVar revision):
//   Every reference to kPC_* feel constants has been replaced with
//   a direct read of the corresponding cv_pc_*->value.  Because
//   CVar::value is a plain float member, this is a single pointer
//   dereference — no overhead versus the old constexpr reads.
//
//   The step order, collision logic, and ground detection are
//   unchanged from the previous revision.
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
// update
// ---------------------------------------------------------------------------
void PlayerController::update(const InputState& input, float dt,
                               const Vec3& fwd, const Vec3& right)
{
    // ---- Step 1: Build wish direction (horizontal only) ----
    Vec3 fwdH   = { fwd.x,   0.0f, fwd.z   };
    Vec3 rightH = { right.x, 0.0f, right.z };
    const float fwdLen   = fwdH.length();
    const float rightLen = rightH.length();
    if (fwdLen   > 1e-4f) fwdH   = fwdH   * (1.0f / fwdLen);
    if (rightLen > 1e-4f) rightH = rightH * (1.0f / rightLen);

    Vec3  wishDir = Vec3::zero();
    float speed   = cv_pc_movespeed->value;

    // Sprint — shift held multiplies wish speed
    if (input.keys[SDL_SCANCODE_LSHIFT] || input.keys[SDL_SCANCODE_RSHIFT])
        speed *= cv_pc_sprintmult->value;

    if (input.keys[SDL_SCANCODE_W]) wishDir = wishDir + fwdH;
    if (input.keys[SDL_SCANCODE_S]) wishDir = wishDir - fwdH;
    if (input.keys[SDL_SCANCODE_D]) wishDir = wishDir + rightH;
    if (input.keys[SDL_SCANCODE_A]) wishDir = wishDir - rightH;

    const float wishLen = wishDir.length();
    if (wishLen > 1.0f)
        wishDir = wishDir * (1.0f / wishLen);

    // ---- Step 2: Jump (edge-detected, before gravity) ----
    if (input.keys[SDL_SCANCODE_SPACE] && !m_spaceHeld && m_grounded)
    {
        m_velocity.y = cv_pc_jumpspeed->value;
        m_grounded   = false;
    }
    m_spaceHeld = input.keys[SDL_SCANCODE_SPACE];

    // ---- Step 3: Gravity ----
    applyGravity(dt);

    // ---- Step 4: Friction ----
    applyFriction(dt);

    // ---- Step 5: Horizontal acceleration ----
    if (wishLen > 0.01f)
        applyAcceleration(wishDir, speed, dt);

    // ---- Step 6: Collision via moveSlide ----
    if (m_physics && m_entity.isValid())
    {
        m_physics->setOrigin(m_entity, m_position);
        m_physics->setVelocity(m_entity, m_velocity);
        m_physics->moveSlide(m_entity, Vec3::zero(), dt, 0.0f);
        m_position = m_physics->getOrigin(m_entity);
        m_velocity  = m_physics->getVelocity(m_entity);

        // ---- Step 7: Re-detect ground ----
        m_grounded = m_physics->isOnGround(m_entity);
        if (m_grounded && m_velocity.y < 0.0f)
            m_velocity.y = 0.0f;
    }
    else
    {
        m_position = m_position + m_velocity * dt;
        m_grounded = detectGround();
    }

    // ---- Step 8: Clamp max horizontal speed on ground ----
    if (m_grounded)
    {
        const float hspd = std::sqrt(m_velocity.x * m_velocity.x +
                                      m_velocity.z * m_velocity.z);
        if (hspd > cv_pc_maxspeed->value && hspd > 1e-4f)
        {
            const float scale = cv_pc_maxspeed->value / hspd;
            m_velocity.x *= scale;
            m_velocity.z *= scale;
        }
    }
}

// ---------------------------------------------------------------------------
// applyGravity
// ---------------------------------------------------------------------------
void PlayerController::applyGravity(float dt)
{
    if (m_grounded)
    {
        if (m_velocity.y < 0.0f) m_velocity.y = 0.0f;
        return;
    }

    const float gravity = m_physics ? m_physics->getGravity() : 800.0f;
    m_velocity.y -= gravity * dt;
    if (m_velocity.y < -kPC_TerminalVelocity)
        m_velocity.y = -kPC_TerminalVelocity;
}

// ---------------------------------------------------------------------------
// detectGround  (fallback: no physics entity)
// ---------------------------------------------------------------------------
bool PlayerController::detectGround()
{
    if (!m_physics)
        return false;

    const Vec3 end = { m_position.x, m_position.y - 4.0f, m_position.z };
    TraceResult tr = m_physics->trace(m_position, end, m_playerMins, m_playerMaxs);

    if (tr.fraction < 1.0f && tr.normal.y >= kPC_GroundNormal)
    {
        m_position.y = tr.endPos.y;
        if (m_velocity.y < 0.0f)
            m_velocity.y = 0.0f;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// applyFriction
// ---------------------------------------------------------------------------
void PlayerController::applyFriction(float dt)
{
    if (!m_grounded)
        return;

    const float speed = std::sqrt(m_velocity.x * m_velocity.x +
                                   m_velocity.z * m_velocity.z);
    if (speed < 1.0f)
    {
        m_velocity.x = 0.0f;
        m_velocity.z = 0.0f;
        return;
    }

    const float stopSpeed = cv_pc_stopspeed->value;
    const float friction  = cv_pc_friction->value;
    const float control   = (speed < stopSpeed) ? stopSpeed : speed;
    const float drop      = control * friction * dt;
    const float newSpeed  = std::max(0.0f, speed - drop);
    const float scale     = newSpeed / speed;
    m_velocity.x *= scale;
    m_velocity.z *= scale;
}

// ---------------------------------------------------------------------------
// applyAcceleration — Q2 PM_Accelerate
// ---------------------------------------------------------------------------
void PlayerController::applyAcceleration(const Vec3& wishDir, float speed, float dt)
{
    const float accel   = m_grounded ? cv_pc_groundaccel->value
                                     : cv_pc_airaccel->value;
    const float curSpd  = m_velocity.x * wishDir.x + m_velocity.z * wishDir.z;
    const float addSpd  = speed - curSpd;
    if (addSpd <= 0.0f) return;

    float accelSpd = accel * speed * dt;
    if (accelSpd > addSpd) accelSpd = addSpd;

    m_velocity.x += wishDir.x * accelSpd;
    m_velocity.z += wishDir.z * accelSpd;
}

} // namespace nova