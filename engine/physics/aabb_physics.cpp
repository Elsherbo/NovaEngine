// ============================================================
// FILE:    engine/physics/aabb_physics.cpp
// MODULE:  Physics
// PHASE:   2
// STATUS:  IN_PROGRESS  
// PURPOSE: Quake-style AABB physics.
//         PM_SlideMove for player movement.
// DEPENDS:  physics/iphysics_world.h, renderer/bsp/bsp.h
// ============================================================

#include "engine/physics/aabb_physics.h"

#include <algorithm>

namespace nova
{

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------
static constexpr float kStopEpsilon = 0.1f;
static constexpr float kClipEpsilon = 0.01f;
static constexpr int kMaxClipMoves = 4;

// -----------------------------------------------------------------------
// Constructor
// -----------------------------------------------------------------------
AABBPhysics::AABBPhysics()
{
    m_bsp = nullptr;
    m_gravity = 1.0f;
    m_entOrigin = nullptr;
    m_entVelocity = nullptr;
}

// -----------------------------------------------------------------------
// setWorld
// -----------------------------------------------------------------------
void AABBPhysics::setWorld(BSPMap *bsp)
{
    m_bsp = bsp;
}

// -----------------------------------------------------------------------
// step - advance physics for all entities
// -----------------------------------------------------------------------
void AABBPhysics::step(float dt)
{
    if (!m_bsp || !m_entOrigin || !m_entVelocity)
        return;

    // Step each entity with physics
    for (auto& e : m_entities)
    {
        if (!e.isValid())
            continue;

        Vec3& origin = m_entOrigin[e.index()];
        Vec3& velocity = m_entVelocity[e.index()];

        // Apply gravity
        velocity.z -= m_gravity * 9.8f * dt;

        // Move with slide
        moveSlide(e, velocity, 1.0f, 1.0f);

        // Update origin
        origin = getOrigin(e) + velocity * dt;
    }
}

// -----------------------------------------------------------------------
// setEntityStorage - set entity data pointers
// -----------------------------------------------------------------------
void AABBPhysics::setEntityStorage(Vec3 *origin, Vec3 *velocity)
{
    m_entOrigin = origin;
    m_entVelocity = velocity;
}

// -----------------------------------------------------------------------
// setOrigin
// -----------------------------------------------------------------------
void AABBPhysics::setOrigin(EntityHandle e, const Vec3& origin)
{
    if (m_entOrigin)
        m_entOrigin[e.index()] = origin;
}

// -----------------------------------------------------------------------
// setVelocity  
// -----------------------------------------------------------------------
void AABBPhysics::setVelocity(EntityHandle e, const Vec3& velocity)
{
    if (m_entVelocity)
        m_entVelocity[e.index()] = velocity;
}

// -----------------------------------------------------------------------
// getOrigin
// -----------------------------------------------------------------------
Vec3 AABBPhysics::getOrigin(EntityHandle e) const
{
    if (m_entOrigin)
        return m_entOrigin[e.index()];
    return {};
}

// -----------------------------------------------------------------------
// getVelocity
// -----------------------------------------------------------------------
Vec3 AABBPhysics::getVelocity(EntityHandle e) const
{
    if (m_entVelocity)
        return m_entVelocity[e.index()];
    return {};
}

// -----------------------------------------------------------------------
// trace - raycast against world
// -----------------------------------------------------------------------
TraceResult AABBPhysics::trace(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs)
{
    TraceResult result;
    result.endPos = end;

    if (!m_bsp)
        return result;

    Vec3 dir = end - start;
    float len = dir.length();
    if (len < 0.001f)
    {
        result.fraction = 1.0f;
        return result;
    }
    dir = dir * (1.0f / len);

    return traceWorld(start, dir, len, mins, maxs);
}

// -----------------------------------------------------------------------
// traceEntity - trace avoiding one entity
// -----------------------------------------------------------------------
TraceResult AABBPhysics::traceEntity(EntityHandle skip, const Vec3& start, const Vec3& end, 
                                     const Vec3& mins, const Vec3& maxs)
{
    TraceResult result = trace(start, end, mins, maxs);
    if (result.entity == skip)
    {
        result.fraction = 1.0f;
        result.entity = EntityHandle();
    }
    return result;
}

// -----------------------------------------------------------------------
// moveSlide - Quake-style slide movement
// -----------------------------------------------------------------------
TraceResult AABBPhysics::moveSlide(EntityHandle e, const Vec3& wishDir, float speed, float wishSpeed)
{
    (void)speed;
    TraceResult result;

    if (!m_entOrigin || !m_entVelocity)
        return result;

    Vec3 origin = m_entOrigin[e.index()];
    (void)origin;
    Vec3& velocity = m_entVelocity[e.index()];
    (void)velocity;

    // Calculate velocity
    Vec3 wishvel = wishDir * wishSpeed;
    Vec3 newVelocity = origin + wishvel;

    // Try to move
    bool blocked = false;

    for (int i = 0; i < kMaxClipMoves && !blocked; ++i)
    {
        Vec3 delta = newVelocity - origin;
        float dist = delta.length();
        if (dist < 0.01f)
            break;

        // Trace
        Vec3 dir = delta * (1.0f / dist);
        TraceResult tr = traceWorld(origin, dir, dist, {0, 0, 0}, {0, 0, 0});

        if (tr.fraction < 1.0f - kClipEpsilon)
        {
            // Hit - reflect velocity
            blocked = true;
            result = tr;
        }

        if (tr.fraction >= 1.0f)
            break;

        // Move to hit point
        origin = tr.endPos;
        newVelocity = tr.endPos + dir * 0.1f;
    }

    m_entOrigin[e.index()] = origin;
    return result;
}

// -----------------------------------------------------------------------
// traceWorld - internal BSP trace
// -----------------------------------------------------------------------
TraceResult AABBPhysics::traceWorld(const Vec3& start, const Vec3& dir, float dist, 
                                    const Vec3& mins, const Vec3& maxs)
{
    (void)mins;
    (void)maxs;
    TraceResult result;
    result.endPos = start + dir * dist;
    result.fraction = 1.0f;
    return result;
}

// -----------------------------------------------------------------------
// isOnGround
// -----------------------------------------------------------------------
bool AABBPhysics::isOnGround(EntityHandle e)
{
    if (!m_entOrigin)
        return false;

    Vec3 origin = m_entOrigin[e.index()];
    Vec3 mins = {-16, -16, -36};
    Vec3 maxs = {16, 16, 36};

    // Trace down
    TraceResult tr = trace(origin, origin + Vec3{0, 0, -1}, mins, maxs);
    return tr.fraction < 1.0f;
}

// -----------------------------------------------------------------------
// getGroundEntity
// -----------------------------------------------------------------------
EntityHandle AABBPhysics::getGroundEntity(EntityHandle e)
{
    if (!m_entOrigin)
        return EntityHandle();

    Vec3 origin = m_entOrigin[e.index()];
    Vec3 mins = {-16, -16, -36};
    Vec3 maxs = {16, 16, 36};

    TraceResult tr = trace(origin, origin + Vec3{0, 0, -2}, mins, maxs);
    return tr.entity;
}

// -----------------------------------------------------------------------
// getGroundElevation
// -----------------------------------------------------------------------
float AABBPhysics::getGroundElevation(EntityHandle e)
{
    if (!m_entOrigin)
        return -1e10f;

    Vec3 origin = m_entOrigin[e.index()];
    Vec3 mins = {-16, -16, -36};
    Vec3 maxs = {16, 16, 36};

    TraceResult tr = trace(origin, origin + Vec3{0, 0, -2}, mins, maxs);
    return tr.endPos.z;
}

// -----------------------------------------------------------------------
// setGravity
// -----------------------------------------------------------------------
void AABBPhysics::setGravity(float gravity)
{
    m_gravity = gravity;
}

// -----------------------------------------------------------------------
// getGravity
// -----------------------------------------------------------------------
float AABBPhysics::getGravity() const
{
    return m_gravity;
}

} // namespace nova