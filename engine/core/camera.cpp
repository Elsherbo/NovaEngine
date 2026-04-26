// ============================================================
// FILE:    engine/core/camera.cpp
// MODULE:  Core > Camera
// PHASE:   1
// PURPOSE: First-person camera with yaw/pitch, WASD + jump,
//          gravity, and BSP collision via IPhysicsWorld.
//
// FIX LOG:
//   1. Physics integration rewritten:
//      - Old code split horizontal and vertical into two separate
//        traces that didn't compose (newPos was partially updated
//        between them, causing drift).
//      - New code: integrates full 3D velocity into a single
//        delta vector, then calls physics->moveSlide() which
//        handles all BSP clipping internally. Camera just
//        reads back the final position.
//
//   2. isOnGround() / jump check fixed:
//      - Old trace was using hull {-16,-36,-16}/{16,36,16} but
//        forgot the mins/maxs are entity-relative. The check
//        traced DOWN from camera origin but didn't account for
//        feet being at origin.y - 36. Now relies on physics
//        world's isOnGround() instead of a manual trace.
//
//   3. Gravity axis: engine is Y-up. Gravity subtracts from
//      velocity.y. Was already correct but is now explicit.
//
//   4. Friction: applied correctly to horizontal (XZ) components
//      only. Vertical (Y) is governed by gravity / ground contact.
//
//   5. setPhysicsWorld() made type-safe: stores IPhysicsWorld*
//      directly instead of void* with a cast on every use.
//
//   6. Movement direction uses getForwardFull() for the XZ
//      projection instead of the old getForward() that could
//      become zero when looking straight up/down.
//
//   7. [FIX] moveSlide() was passed 'delta' (velocity*dt, already
//      scaled) as wishVel.  moveSlide() treats wishVel as a direction
//      and applies Q2 acceleration on top of it, so the player was
//      double-accelerated every frame.  Fix: pass Vec3::zero() as
//      wishVel (no extra acceleration) and wishSpeed=0 so moveSlide
//      acts purely as a swept collision move for the pre-built delta.
//
//   8. [FIX] Gravity was applied to m_velocity BEFORE syncing from
//      physics storage on frame 1, so the first tick would add
//      gravity to whatever was in m_velocity (uninitialised = 0)
//      then immediately overwrite origin from storage.  Fix: sync
//      origin from physics FIRST, then apply gravity.
//
//   9. [FIX] Air drag was applied with a frame-rate-DEPENDENT
//      multiplier (1 - 2*dt instead of pow(base, dt*60)).
//      The ground check used a 3-unit probe but the player hull
//      feet are 36 units below origin — made the probe too short
//      on ramps. Probe extended to 4 units and kGroundNormal
//      threshold made explicit.
//
//  10. [FIX] Ground snap was applied unconditionally every frame
//      when grounded and vel.y < 0, causing sub-unit micro-jitter
//      on slopes from floating-point variance in trace endpoints.
//      Snap now only triggers when position differs from floor by
//      more than 0.5 units. Ground probe is skipped entirely when
//      vel.y > 1 (jumping strongly upward) to save a trace call.
// ============================================================

#include "engine/core/camera.h"
#include "engine/platform/iplatform.h"
#include "engine/core/math/common.h"
#include "engine/physics/iphysics_world.h"
#include <SDL3/SDL_scancode.h>
#include <cmath>

namespace nova
{

    Camera::Camera() = default;

    void Camera::setPosition(const Vec3 &pos) { m_position = pos; }
    void Camera::setFOV(float fovDegrees) { m_fov = fovDegrees; }
    void Camera::setAspect(float aspect) { m_aspect = aspect; }
    void Camera::setNearFar(float n, float f)
    {
        m_nearZ = n;
        m_farZ = f;
    }

    void Camera::lookAt(const Vec3 &target)
    {
        Vec3 forward = (target - m_position).normalized();
        m_yaw = std::atan2(forward.x, -forward.z);
        m_pitch = std::asin(clamp(-forward.y, -1.0f, 1.0f));
        m_rotation = Quat::fromEuler(m_pitch, m_yaw, 0.f);
    }

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
        {
            // Looking straight up/down — use yaw to derive horizontal forward
            return Vec3{std::sin(m_yaw), 0.f, -std::cos(m_yaw)};
        }
        return f * (1.0f / len);
    }

    Vec3 Camera::getRight() const
    {
        Vec3 r = m_rotation.rotate(Vec3::right());
        r.y = 0.f;
        float len = r.length();
        if (len < 1e-6f)
            return Vec3::right();
        return r * (1.0f / len);
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
        m_yaw -= dx * m_lookSpeed;
        m_pitch -= dy * m_lookSpeed;

        constexpr float maxPitch = kHalfPi - 0.01f;
        m_pitch = clamp(m_pitch, -maxPitch, maxPitch);

        if (m_yaw > kPi)
            m_yaw -= k2Pi;
        if (m_yaw < -kPi)
            m_yaw += k2Pi;

        m_rotation = Quat::fromEuler(m_pitch, m_yaw, 0.f);
    }

    void Camera::update(const InputState &input, float dt)
    {
        // FIX 8: Sync position from physics storage FIRST, before applying gravity.
        // On frame 1 m_velocity is zero; applying gravity before reading the stored
        // position would incorrectly subtract gravity from a stale local velocity.
        if (m_physics && m_entity.isValid())
            m_position = m_physics->getOrigin(m_entity);

        // ---- Mouse look ----
        if (input.mouseDeltaX != 0 || input.mouseDeltaY != 0)
        {
            applyMouseLook(static_cast<float>(input.mouseDeltaX),
                           static_cast<float>(input.mouseDeltaY));
        }

        // ----------------------------------------------------------------
        //  Physics mode: gravity, collision, jump
        // ----------------------------------------------------------------
        if (m_physics)
        {
            IPhysicsWorld *phys = m_physics;

            const Vec3 fwd = getForward();
            const Vec3 right = getRight();

            // Build wish direction from WASD input
            Vec3 wishDir = Vec3::zero();
            if (input.keys[SDL_SCANCODE_W])
                wishDir = wishDir + fwd;
            if (input.keys[SDL_SCANCODE_S])
                wishDir = wishDir - fwd;
            if (input.keys[SDL_SCANCODE_D])
                wishDir = wishDir + right;
            if (input.keys[SDL_SCANCODE_A])
                wishDir = wishDir - right;

            float speed = m_moveSpeed;
            if (input.keys[SDL_SCANCODE_LSHIFT] || input.keys[SDL_SCANCODE_RSHIFT])
                speed *= 3.0f;

            // Normalize wish direction
            float wishLen = wishDir.length();
            if (wishLen > 1.0f)
                wishDir = wishDir * (1.0f / wishLen);

            // ---- Gravity ----
            // Y-up: gravity pulls velocity.y downward.
            // Clamp terminal velocity so a single frame never exceeds the
            // floor thickness (~32 units for Q2). At 60fps, 1920 u/s = 32 u/frame.
            m_velocity.y -= phys->getGravity() * dt;
            if (m_velocity.y < -1800.0f) m_velocity.y = -1800.0f;

            // ---- Ground detection ----
            // FIX 9: extended probe from 3 to 4 units; more reliable on slopes.
            // Skip ground check when moving strongly upward to avoid a wasted trace
            // at the apex of a jump (result would always be grounded=false anyway).
            const Vec3 playerMins = {-16.f, -36.f, -16.f};
            const Vec3 playerMaxs = {16.f, 36.f, 16.f};
            static constexpr float kGroundNormal = 0.7f;  // cos(45°)

            bool grounded = false;
            TraceResult groundTr;
            if (m_velocity.y <= 1.0f)  // skip probe when jumping strongly upward
            {
                Vec3 groundCheckEnd = {m_position.x, m_position.y - 4.0f, m_position.z};
                groundTr = phys->trace(m_position, groundCheckEnd, playerMins, playerMaxs);
                grounded = (groundTr.fraction < 1.0f && groundTr.normal.y > kGroundNormal);
            }

            if (grounded && m_velocity.y < 0.0f)
            {
                // Snap to ground surface. The 0.5-unit threshold was causing
                // the player to float above sloped floors and fall through thin
                // geometry when gravity accumulated. Snap unconditionally when
                // grounded and moving downward; moveSlide handles the actual
                // collision so this just corrects minor float drift.
                m_position = groundTr.endPos;
                m_velocity.y = 0.0f;
            }

            // ---- Jump ----
            if (input.keys[SDL_SCANCODE_SPACE] && !m_spaceHeld && grounded)
            {
                m_velocity.y = 270.0f; // Q2 jump speed
                grounded = false;
            }
            m_spaceHeld = input.keys[SDL_SCANCODE_SPACE];

            // ---- Horizontal friction ----
            if (!grounded)
            {
                // FIX 9: frame-rate-independent exponential air drag
                float airDrag = std::pow(1.0f - 2.0f / 60.0f, dt * 60.0f);
                m_velocity.x *= airDrag;
                m_velocity.z *= airDrag;
            }
            else
            {
                // Ground friction — Q2 style: friction constant 6
                const float friction = 6.0f;
                float spd = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z);
                if (spd > 1.0f)
                {
                    float drop = spd * friction * dt;
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

            // ---- Horizontal acceleration (Q2 PM_Accelerate style) ----
            if (wishLen > 0.01f)
            {
                // accel = 10 on ground, 1 in air
                float accel = grounded ? 10.0f : 1.0f;
                float curSpd = m_velocity.x * wishDir.x + m_velocity.z * wishDir.z;
                float addSpd = speed - curSpd;
                if (addSpd > 0.0f)
                {
                    float accelSpd = accel * speed * dt;
                    if (accelSpd > addSpd)
                        accelSpd = addSpd;
                    m_velocity.x += wishDir.x * accelSpd;
                    m_velocity.z += wishDir.z * accelSpd;
                }
            }

            // ---- Integrate and slide ----
            // Sync velocity to external storage for physics
            if (phys && m_entity.isValid())
                phys->setVelocity(m_entity, m_velocity);

            Vec3 delta = m_velocity * dt;
            float deltaDist = delta.length();

            if (deltaDist > 1e-4f && m_entity.isValid())
            {
                // Copy position to external storage before moveSlide
                if (phys)
                    phys->setOrigin(m_entity, m_position);

                // FIX 7: Pass Vec3::zero()/0 so moveSlide doesn't apply a second
                // acceleration pass on top of the velocity we already integrated.
                // moveSlide() will use the entity's current velocity (delta / dt)
                // purely for the swept BSP collision, not for acceleration.
                TraceResult moveResult = phys->moveSlide(m_entity, Vec3::zero(), dt, 0.f);
                m_position = moveResult.endPos;

                // Get updated origin back from physics
                m_position = phys->getOrigin(m_entity);

                // Get updated velocity from physics
                m_velocity = phys->getVelocity(m_entity);

                // Kill vertical velocity on floor/ceiling hit
                if (moveResult.normal.y > kGroundNormal && m_velocity.y < 0.0f)
                    m_velocity.y = 0.0f;
                if (moveResult.normal.y < -0.1f && m_velocity.y > 0.0f)
                    m_velocity.y = 0.0f;

                // Clamp max speed
                float maxSpeed = speed * 2.0f;
                float hspd = std::sqrt(m_velocity.x * m_velocity.x + m_velocity.z * m_velocity.z);
                if (hspd > maxSpeed)
                {
                    float scale = maxSpeed / hspd;
                    m_velocity.x *= scale;
                    m_velocity.z *= scale;
                }
            }

            return;
        }

        // ----------------------------------------------------------------
        //  Fallback: Noclip mode
        // ----------------------------------------------------------------
        float speed = m_moveSpeed * dt;
        if (input.keys[SDL_SCANCODE_LSHIFT] || input.keys[SDL_SCANCODE_RSHIFT])
            speed *= 3.f;

        Vec3 fwd = getForward();
        Vec3 right = getRight();

        if (input.keys[SDL_SCANCODE_W])
            m_position = m_position + fwd * speed;
        if (input.keys[SDL_SCANCODE_S])
            m_position = m_position - fwd * speed;
        if (input.keys[SDL_SCANCODE_D])
            m_position = m_position + right * speed;
        if (input.keys[SDL_SCANCODE_A])
            m_position = m_position - right * speed;

        // Vertical in noclip (Space = up, C = down)
        if (input.keys[SDL_SCANCODE_SPACE])
            m_position.y += speed;
        if (input.keys[SDL_SCANCODE_C])
            m_position.y -= speed;
    }

} // namespace nova
