// ============================================================
// FILE:    engine/physics/aabb_physics.h
// MODULE:  Physics
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Quake-style AABB physics implementation.
// DEPENDS:  physics/iphysics_world.h
// ============================================================

#pragma once

#include "engine/physics/iphysics_world.h"

#include <vector>

namespace nova
{

// -----------------------------------------------------------------------
// AABBPhysics - Quake-style physics
// -----------------------------------------------------------------------
class AABBPhysics : public IPhysicsWorld
{
public:
    AABBPhysics();

    // ---- World ----
    void setWorld(BSPMap *bsp) override;

    // ---- Frame ----
    void step(float dt) override;

    // ---- Entity storage (set by engine) ----
    void setEntityStorage(Vec3 *origin, Vec3 *velocity);

    // ---- Entity operations ----
    void setOrigin(EntityHandle e, const Vec3& origin) override;
    void setVelocity(EntityHandle e, const Vec3& velocity) override;
    Vec3 getOrigin(EntityHandle e) const override;
    Vec3 getVelocity(EntityHandle e) const override;

    // ---- Collision tests ----
    TraceResult trace(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) override;
    TraceResult traceEntity(EntityHandle skip, const Vec3& start, const Vec3& end, 
                        const Vec3& mins, const Vec3& maxs) override;
    TraceResult moveSlide(EntityHandle e, const Vec3& wishDir, float speed, float wishSpeed) override;

    // ---- Ground queries ----
    bool isOnGround(EntityHandle e) override;
    EntityHandle getGroundEntity(EntityHandle e) override;
    float getGroundElevation(EntityHandle e) override;

    // ---- Physics settings ----
    void setGravity(float gravity) override;
    float getGravity() const override;

private:
    TraceResult traceWorld(const Vec3& start, const Vec3& dir, float dist,
                        const Vec3& mins, const Vec3& maxs);

    BSPMap *m_bsp = nullptr;
    Vec3 *m_entOrigin = nullptr;
    Vec3 *m_entVelocity = nullptr;
    std::vector<EntityHandle> m_entities;
    float m_gravity = 1.0f;
};

} // namespace nova