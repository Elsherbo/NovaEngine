// ============================================================
// FILE:    engine/physics/aabb_physics.h
// MODULE:  Physics
// PHASE:   2
// STATUS:  FIXED
// PURPOSE: AABB physics with BSP world collision.
//          Swept AABB vs plane using Minkowski expansion.
//          Quake 2-style slideMove with step-up.
//
// FIX LOG:
//   1. moveSlide() signature: 'speed' parameter replaced with 'dt'.
//   2. setPlayerBounds() moved inline.
//   3. playerMins/playerMaxs default to Y-up GL space.
//   4. testSolid() added — checks whether an AABB origin is fully
//      inside solid geometry (needed for safe spawn placement).
//   5. moveSlide() crease fix: two-plane clip produces edge direction
//      instead of zeroing velocity (stops edge-sticking).
// ============================================================

#pragma once

#include "engine/physics/iphysics_world.h"
#include "engine/renderer/bsp/ibsp_collision.h"
#include <cstddef>

namespace nova
{

    // Dynamic AABB collider for moving brush entities (func_plat, func_door).
    // All fields are in the physics coordinate space (GL: Z-up).
    struct DynamicCollider
    {
        Vec3 origin;   // world position
        Vec3 mins;     // local half-extents (negative)
        Vec3 maxs;     // local half-extents (positive)
    };

    class AABBPhysics : public IPhysicsWorld
    {
    public:
        AABBPhysics();

        void setWorld(IBSPCollisionWorld *world) override;
        void step(float dt) override;

        // External storage (owned by EntityList / Engine)
        void setEntityStorage(Vec3 *origin, Vec3 *velocity, size_t count);

        void setOrigin(EntityHandle e, const Vec3 &origin) override;
        void setVelocity(EntityHandle e, const Vec3 &velocity) override;
        Vec3 getOrigin(EntityHandle e) const override;
        Vec3 getVelocity(EntityHandle e) const override;

        TraceResult trace(const Vec3 &start, const Vec3 &end,
                          const Vec3 &mins, const Vec3 &maxs) override;

        TraceResult traceEntity(EntityHandle skip, const Vec3 &start, const Vec3 &end,
                                const Vec3 &mins, const Vec3 &maxs) override;

        TraceResult moveSlide(EntityHandle e, const Vec3 &wishVel,
                              float dt, float wishSpeed) override;

        bool isOnGround(EntityHandle e) override;
        EntityHandle getGroundEntity(EntityHandle e) override;
        float getGroundElevation(EntityHandle e) override;

        void setGravity(float gravity) override;
        float getGravity() const override;

        void setPlayerBounds(const Vec3 &mins, const Vec3 &maxs)
        {
            m_playerMins = mins;
            m_playerMaxs = maxs;
        }

        // Returns true if the AABB placed at 'origin' overlaps solid geometry.
        // Used for spawn placement to detect whether a candidate point is inside
        // a brush before sweeping downward.
        bool testSolid(const Vec3 &origin, const Vec3 &mins, const Vec3 &maxs) const;

        // Dynamic colliders — moving brush entities whose AABBs change each frame.
        void setDynamicColliders(const DynamicCollider *colliders, size_t count);

    private:
        TraceResult traceWorld(const Vec3 &start, const Vec3 &dir, float dist,
                               const Vec3 &mins, const Vec3 &maxs);

        // Swept AABB vs a single static AABB. Returns fraction, normal, endPos.
        TraceResult traceAABB(const Vec3 &start, const Vec3 &dir, float dist,
                              const Vec3 &mins, const Vec3 &maxs,
                              const Vec3 &boxOrigin, const Vec3 &boxMins, const Vec3 &boxMaxs);

        IBSPCollisionWorld *m_bsp = nullptr;
        Vec3 *m_entOrigin = nullptr;
        Vec3 *m_entVelocity = nullptr;
        size_t m_entCount = 0;
        float m_gravity = 800.0f;

        Vec3 m_playerMins = {-16.f, -36.f, -16.f};
        Vec3 m_playerMaxs = {16.f, 36.f, 16.f};

        const DynamicCollider *m_dynamicColliders = nullptr;
        size_t m_dynamicColliderCount = 0;
    };

} // namespace nova