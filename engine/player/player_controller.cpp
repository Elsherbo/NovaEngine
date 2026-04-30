// ============================================================
// FILE:    engine/player/player_controller.cpp
// MODULE:  Engine > Player
// PHASE:   2
// PURPOSE: PlayerController implementation — all movement physics.
//
// FIX LOG (this revision):
//   1. [FIX] detectGround() rewrote from scratch.
//      Old code: hardcoded floor at Y=28 — completely ignored BSP geometry.
//      New code: calls m_physics->isOnGround(m_entity) after moveSlide so
//      ground detection uses the swept AABB result from the physics system.
//      Fallback (no physics): trace 4 units down with the player hull.
//
//   2. [FIX] update() now properly calls moveSlide() for collision.
//      Old code: split X/Z trace with fraction<0.1 threshold (wrong),
//      no Y collision, hardcoded ±180 unit bounds clamp, moveSlide ignored.
//      New code:
//        a. Push m_position + m_velocity into physics entity storage.
//        b. Call moveSlide(entity, Vec3::zero(), dt, 0.0f).
//           wishSpeed=0 skips internal PM_Accelerate (we already accelerated).
//        c. Read back resolved position + velocity from physics storage.
//        d. Re-evaluate m_grounded via isOnGround() on the post-move position.
//
//   3. [FIX] Step order corrected:
//      Old: gravity → wishDir → friction → accel → broken-move → detectGround → jump
//      New: wishDir → jump (before gravity) → gravity → friction → accel → moveSlide
//      Jump now fires in the same frame the key is pressed.
//
//   4. [FIX] applyGravity() now checks m_grounded.
//      Old: gravity accumulated every frame even while standing, fighting floor snap.
//      New: skip gravity accumulation when grounded; clamp vel.y >= 0 on ground.
//
//   5. [FIX] Forward/right vectors flattened to horizontal plane.
//      Old: camera forward includes Y when looking up/down → WASD pushed player
//      vertically through the air.
//      New: wishDir.y = 0 then re-normalize keeps movement strictly in XZ.
//
//   6. [FIX] Removed duplicate m_position = newPos assignment.
//
//   7. [FIX] Removed hardcoded ±180 unit bounds clamp.
//      Q2 maps are thousands of units wide; this was trapping the player
//      inside an invisible box near the world origin.
// ============================================================

#include "engine/player/player_controller.h"

#include "engine/platform/iplatform.h"
#include "engine/physics/iphysics_world.h"

#include <SDL3/SDL_scancode.h>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace nova
{

PlayerController::PlayerController() = default;

// ---------------------------------------------------------------------------
// update — main entry point, called once per frame
//
// Step order (Q2-style, Y-up engine space):
//   1. Build horizontal wish direction (camera fwd/right flattened to XZ)
//   2. Jump impulse — edge-detected BEFORE gravity so it fires this frame
//   3. Gravity — skipped when grounded; vel.y clamped >= 0 on ground
//   4. Friction — air drag (in-air) or ground friction (grounded)
//   5. PM_Accelerate — horizontal acceleration toward wish direction
//   6. moveSlide — push state into physics, run swept AABB with step-up
//                  and wall sliding, read back resolved position + velocity
//   7. Re-detect ground from post-moveSlide position via isOnGround()
//   8. Clamp max horizontal speed
// ---------------------------------------------------------------------------
void PlayerController::update(const InputState& input, float dt,
                               const Vec3& fwd, const Vec3& right)
{
    // END TEMP DEBUG
    // ---- Step 1: Build wish direction (horizontal only) ----
    // Flatten fwd/right to XZ plane so looking up/down doesn't push the
    // player vertically through the air.
    Vec3 fwdH  = { fwd.x,   0.0f, fwd.z   };
    Vec3 rightH = { right.x, 0.0f, right.z };
    const float fwdLen   = fwdH.length();
    const float rightLen = rightH.length();
    if (fwdLen   > 1e-4f) fwdH   = fwdH   * (1.0f / fwdLen);
    if (rightLen > 1e-4f) rightH = rightH * (1.0f / rightLen);

    Vec3  wishDir = Vec3::zero();
    float speed   = m_moveSpeed;

    if (input.keys[SDL_SCANCODE_LSHIFT] || input.keys[SDL_SCANCODE_RSHIFT])
        speed *= 2.0f;   // sprint

    if (input.keys[SDL_SCANCODE_W]) wishDir = wishDir + fwdH;
    if (input.keys[SDL_SCANCODE_S]) wishDir = wishDir - fwdH;
    if (input.keys[SDL_SCANCODE_D]) wishDir = wishDir + rightH;
    if (input.keys[SDL_SCANCODE_A]) wishDir = wishDir - rightH;

    // Normalize diagonal movement
    const float wishLen = wishDir.length();
    if (wishLen > 1.0f)
        wishDir = wishDir * (1.0f / wishLen);

    // ---- Step 2: Jump (edge-detected, before gravity) ----
    // Fire immediately in the same frame the key is pressed.
    // m_grounded here is the result from the end of the previous frame.
    if (input.keys[SDL_SCANCODE_SPACE] && !m_spaceHeld && m_grounded)
    {
        m_velocity.y = kPC_JumpSpeed;
        m_grounded   = false;
        // Q2: friction is NOT applied on the jump frame
    }
    m_spaceHeld = input.keys[SDL_SCANCODE_SPACE];

    // ---- Step 3: Gravity ----
    applyGravity(dt);

    // ---- Step 4: Friction ----
    applyFriction(dt);

    // ---- Step 5: Horizontal acceleration (Q2 PM_Accelerate) ----
    if (wishLen > 0.01f)
        applyAcceleration(wishDir, speed, dt);

    // ---- Step 6: Collision + movement via moveSlide ----
    if (m_physics && m_entity.isValid())
    {
        // Push our integrated velocity (and current position) into physics
        // entity storage so moveSlide reads the correct values.
        m_physics->setOrigin(m_entity, m_position);
        m_physics->setVelocity(m_entity, m_velocity);

        // Run Quake-style swept AABB move:
        //   - step-up over kStepHeight ledges
        //   - wall-sliding with crease/edge fix
        //   - startSolid protection (won't teleport into geometry)
        // wishSpeed=0 → skip internal PM_Accelerate (already applied above).
        m_physics->moveSlide(m_entity, Vec3::zero(), dt, 0.0f);

        // Read back collision-resolved position and velocity.
        m_position = m_physics->getOrigin(m_entity);
        m_velocity  = m_physics->getVelocity(m_entity);

        // ---- Step 7: Re-detect ground from post-move position ----
        m_grounded = m_physics->isOnGround(m_entity);
        if (m_grounded && m_velocity.y < 0.0f)
            m_velocity.y = 0.0f;
    }
    else
    {
        // No physics world connected: simple noclip-style integration.
        // Also handles the first frame before the entity handle is assigned.
        m_position = m_position + m_velocity * dt;
        m_grounded = detectGround();
    }

    // ---- Step 8: Clamp max horizontal speed ----
    // const float maxSpeed = speed * 1.5f;
    // const float hspd     = std::sqrt(m_velocity.x * m_velocity.x +
    //                                   m_velocity.z * m_velocity.z);
    // if (hspd > maxSpeed && hspd > 1e-4f)
    // {
    //     const float scale = maxSpeed / hspd;
    //     m_velocity.x *= scale;
    //     m_velocity.z *= scale;
    // }
    if (m_grounded)
    {
        // Clamp to max ground speed
        const float hspd = std::sqrt(m_velocity.x * m_velocity.x +
                                    m_velocity.z * m_velocity.z);
        if (hspd > kPC_MaxSpeed)
        {
            const float scale = kPC_MaxSpeed / hspd;
            m_velocity.x *= scale;
            m_velocity.z *= scale;
        }
    }
}

// ---------------------------------------------------------------------------
// applyGravity
//
// FIX: Skip gravity accumulation while grounded. Old code applied gravity
// every frame regardless of ground state, causing the downward velocity to
// grow without bound while standing and fighting the floor snap each frame.
// ---------------------------------------------------------------------------
void PlayerController::applyGravity(float dt)
{
    if (m_grounded)
    {
        // Prevent gravity from accumulating while standing on a surface.
        // Keep vel.y >= 0 so a just-initiated jump is never clipped here.
        if (m_velocity.y < 0.0f) m_velocity.y = 0.0f;
        return;
    }

    const float gravity = m_physics ? m_physics->getGravity() : 800.0f;
    m_velocity.y -= gravity * dt;
    if (m_velocity.y < -kPC_TerminalVelocity)
        m_velocity.y = -kPC_TerminalVelocity;
}

// ---------------------------------------------------------------------------
// detectGround
//
// FIX: Old code checked m_position.y <= 28.0f (hardcoded invisible floor).
// This completely ignored all BSP brushes — the player would fall through
// every floor in every map unless Y happened to be exactly 28.
//
// New code: used as a fallback when physics is unavailable or the entity
// handle is not yet valid (e.g. the very first frame before the entity is
// created). In the normal code path, isOnGround() is called on the physics
// entity after moveSlide() resolves the position — this function is only
// reached via the else-branch in update().
// ---------------------------------------------------------------------------
bool PlayerController::detectGround()
{
    if (!m_physics)
        return false;

    // Probe 4 units below the current hull position.
    // A hit normal with Y > kPC_GroundNormal (cos 45°) is treated as ground.
    const Vec3 end = { m_position.x, m_position.y - 4.0f, m_position.z };
    TraceResult tr = m_physics->trace(m_position, end, m_playerMins, m_playerMaxs);

    if (tr.fraction < 1.0f && tr.normal.y >= kPC_GroundNormal)
    {
        // Snap gently to the floor surface.
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
        return; // Q2 has NO air friction — velocity is preserved perfectly in air
    
    // Q2 ground friction: speed-dependent with stopspeed threshold
    const float speed = std::sqrt(m_velocity.x * m_velocity.x +
                                   m_velocity.z * m_velocity.z);
    if (speed < 1.0f)
    {
        m_velocity.x = 0.0f;
        m_velocity.z = 0.0f;
        return;
    }
    
    // Q2: friction applies to max(speed, stopspeed)
    const float control    = (speed < kPC_StopSpeed) ? kPC_StopSpeed : speed;
    const float drop       = control * kPC_Friction * dt;
    const float newSpeed   = std::max(0.0f, speed - drop);
    const float scale      = newSpeed / speed;
    m_velocity.x *= scale;
    m_velocity.z *= scale;
}

// ---------------------------------------------------------------------------
// applyAcceleration — Q2 PM_Accelerate style
//
// Projects current velocity onto the wish direction, then adds acceleration
// only up to wishSpeed. This gives the "strafejump potential" feel of Q2:
// you can exceed wishSpeed by strafing but not by holding W alone.
// ---------------------------------------------------------------------------
void PlayerController::applyAcceleration(const Vec3& wishDir, float speed, float dt)
{
    // Q2 PM_Accelerate
    const float accel = m_grounded ? kPC_GroundAccel : kPC_AirAccel;
    
    const float curSpd = m_velocity.x * wishDir.x + m_velocity.z * wishDir.z;
    const float addSpd = speed - curSpd;
    if (addSpd <= 0.0f) return;
    
    float accelSpd = accel * speed * dt;
    if (accelSpd > addSpd) accelSpd = addSpd;
    
    m_velocity.x += wishDir.x * accelSpd;
    m_velocity.z += wishDir.z * accelSpd;
}

} // namespace nova
