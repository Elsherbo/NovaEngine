// ============================================================
// FILE:    engine/physics/aabb_physics.h
// MODULE:  Physics
// PHASE:   2
// STATUS:  PARTIAL
// PURPOSE: AABB physics with BSP world collision.
//          Swept AABB vs plane using Minkowski expansion.
// DEPENDS:  physics/iphysics_world.h
// ============================================================

#pragma once

#include "engine/physics/iphysics_world.h"

namespace nova
{

// -----------------------------------------------------------------------
// AABBPhysics - Quake-style AABB collision with BSP world
// -----------------------------------------------------------------------
class AABBPhysics : public IPhysicsWorld
{
public:
    AABBPhysics();

    void setWorld(BSPMap *bsp) override;
    void step(float dt) override;
    void setEntityStorage(Vec3 *origin, Vec3 *velocity);

    void setOrigin(EntityHandle e, const Vec3& origin) override;
    void setVelocity(EntityHandle e, const Vec3& velocity) override;
    Vec3 getOrigin(EntityHandle e) const override;
    Vec3 getVelocity(EntityHandle e) const override;

    TraceResult trace(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) override;
    TraceResult traceEntity(EntityHandle skip, const Vec3& start, const Vec3& end,
                        const Vec3& mins, const Vec3& maxs) override;
    TraceResult moveSlide(EntityHandle e, const Vec3& wishDir, float speed, float wishSpeed) override;

    bool isOnGround(EntityHandle e) override;
    EntityHandle getGroundEntity(EntityHandle e) override;
    float getGroundElevation(EntityHandle e) override;

    void setGravity(float gravity) override;
    float getGravity() const override;

    void setPlayerBounds(const Vec3& mins, const Vec3& maxs)
    {
        m_playerMins = mins;
        m_playerMaxs = maxs;
    }

private:
    TraceResult traceWorld(const Vec3& start, const Vec3& dir, float dist,
                        const Vec3& mins, const Vec3& maxs);

    BSPMap *m_bsp = nullptr;
    Vec3 *m_entOrigin = nullptr;
    Vec3 *m_entVelocity = nullptr;
    float m_gravity = 1.0f;
    Vec3 m_playerMins = {-16, -16, -36};
    Vec3 m_playerMaxs = {16, 16, 36};
};

} // namespace nova