// ============================================================
// FILE:    engine/physics/aabb_physics.cpp
// MODULE:  Physics
// PHASE:   2
// STATUS:  STUB  
// PURPOSE: Physics placeholder - returns no collision.
//          Full BSP collision needs more work.
// DEPENDS:  physics/iphysics_world.h
// ============================================================

#include "engine/physics/aabb_physics.h"

#include <algorithm>
#include <cmath>

namespace nova
{

static constexpr float kClipEpsilon = 0.03125f;

AABBPhysics::AABBPhysics()
{
    m_bsp = nullptr;
    m_gravity = 1.0f;
    m_entOrigin = nullptr;
    m_entVelocity = nullptr;
}

void AABBPhysics::setWorld(BSPMap *bsp) { m_bsp = bsp; }

void AABBPhysics::step(float dt) { (void)dt; }

void AABBPhysics::setEntityStorage(Vec3 *origin, Vec3 *velocity)
{
    m_entOrigin = origin;
    m_entVelocity = velocity;
}

void AABBPhysics::setOrigin(EntityHandle e, const Vec3& origin)
{
    if (m_entOrigin) m_entOrigin[e.index()] = origin;
}

void AABBPhysics::setVelocity(EntityHandle e, const Vec3& velocity)
{
    if (m_entVelocity) m_entVelocity[e.index()] = velocity;
}

Vec3 AABBPhysics::getOrigin(EntityHandle e) const
{
    return m_entOrigin ? m_entOrigin[e.index()] : Vec3{};
}

Vec3 AABBPhysics::getVelocity(EntityHandle e) const
{
    return m_entVelocity ? m_entVelocity[e.index()] : Vec3{};
}

TraceResult AABBPhysics::trace(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs)
{
    (void)start;
    (void)mins; (void)maxs;
    TraceResult r;
    r.endPos = end;
    r.fraction = 1.0f;
    return r;
}

TraceResult AABBPhysics::traceEntity(EntityHandle skip, const Vec3& start, const Vec3& end, 
                                  const Vec3& mins, const Vec3& maxs)
{
    (void)skip;
    return trace(start, end, mins, maxs);
}

TraceResult AABBPhysics::moveSlide(EntityHandle e, const Vec3& wishDir, float speed, float wishSpeed)
{
    (void)e; (void)speed; (void)wishSpeed;
    TraceResult r;
    if (m_entOrigin)
    {
        m_entOrigin[e.index()] = m_entOrigin[e.index()] + wishDir * wishSpeed * 0.016f;
    }
    return r;
}

TraceResult AABBPhysics::traceWorld(const Vec3& start, const Vec3& dir, float dist, const Vec3& mins, const Vec3& maxs)
{
    (void)start; (void)dir; (void)dist; (void)mins; (void)maxs;
    TraceResult r;
    r.fraction = 1.0f;
    return r;
}

bool AABBPhysics::isOnGround(EntityHandle e)
{
    if (!m_entOrigin) return false;
    Vec3 o = m_entOrigin[e.index()];
    TraceResult tr = trace(o, o + Vec3{0, 0, -2}, {-16, -16, -36}, {16, 16, 36});
    return tr.fraction < 1.0f;
}

EntityHandle AABBPhysics::getGroundEntity(EntityHandle e)
{
    if (!m_entOrigin) return EntityHandle();
    return isOnGround(e) ? e : EntityHandle();
}

float AABBPhysics::getGroundElevation(EntityHandle e)
{
    if (!m_entOrigin) return -1e10f;
    return m_entOrigin[e.index()].z;
}

void AABBPhysics::setGravity(float g) { m_gravity = g; }
float AABBPhysics::getGravity() const { return m_gravity; }

} // namespace nova