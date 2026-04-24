// ============================================================
// FILE:    engine/physics/iphysics_world.h
// MODULE:  Physics
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Abstract physics world interface.
//         Swappable backends (AABB vs Jolt).
// DEPENDS:  core/math (Vec3, AABB), entities/entity_id.h
// ============================================================

#pragma once

#include "engine/core/math/vec.h"
#include "engine/core/math/shapes.h"
#include "engine/entities/entity_id.h"

namespace nova
{

// -----------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------
class BSPMap;
struct Entity;
class EntityList;

// -----------------------------------------------------------------------
// Trace result
// -----------------------------------------------------------------------
struct TraceResult
{
    float   fraction = 1.0f;      // 0 = hit, 1 = miss
    Vec3    normal = {0, 0, 0};
    Vec3    endPos;
    EntityHandle entity;   // hit entity
    const char *surface = nullptr;  // hit surface name
};

// -----------------------------------------------------------------------
// IPhysicsWorld - pure virtual physics interface
// -----------------------------------------------------------------------
class IPhysicsWorld
{
public:
    virtual ~IPhysicsWorld() = default;

    // ---- World ----
    virtual void setWorld(BSPMap *bsp) = 0;

    // ---- Movement ----
    virtual void step(float dt) = 0;

    // ---- Entity operations ----
    virtual void setOrigin(EntityHandle e, const Vec3& origin) = 0;
    virtual void setVelocity(EntityHandle e, const Vec3& velocity) = 0;
    virtual Vec3 getOrigin(EntityHandle e) const = 0;
    virtual Vec3 getVelocity(EntityHandle e) const = 0;

    // ---- Collision tests ----
    virtual TraceResult trace(const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) = 0;
    virtual TraceResult traceEntity(EntityHandle skip, const Vec3& start, const Vec3& end, const Vec3& mins, const Vec3& maxs) = 0;
    virtual TraceResult moveSlide(EntityHandle e, const Vec3& wishDir, float speed, float wishSpeed) = 0;

    // ---- Ground queries ----
    virtual bool isOnGround(EntityHandle e) = 0;
    virtual EntityHandle getGroundEntity(EntityHandle e) = 0;
    virtual float getGroundElevation(EntityHandle e) = 0;

    // ---- Physics settings ----
    virtual void setGravity(float gravity) = 0;
    virtual float getGravity() const = 0;
};

} // namespace nova