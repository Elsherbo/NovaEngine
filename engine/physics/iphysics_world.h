// ============================================================
// FILE:    engine/physics/iphysics_world.h
// MODULE:  Physics
// PHASE:   2
// STATUS:  FIXED
// PURPOSE: Abstract physics world interface.
//          Swappable backends (AABB vs Jolt).
//
// FIX LOG:
//   1. moveSlide() second parameter renamed from 'speed' to 'dt'
//      (delta time). The original parameter was completely unused
//      in all implementations — every backend was hardcoding
//      dt = 1/60.  Now the caller passes actual frame time.
//
//   2. Coordinate system clarification added: engine is Y-up
//      (OpenGL convention) after BSP q2ToGL conversion. All
//      physics methods operate in this space.
//
//   3. TraceResult: added 'startSolid' flag to indicate when
//      the trace starts inside solid geometry (penetrating state).
//      Used by moveSlide to avoid compounding penetration.
//
//   4. setWorld() now takes IBSPCollisionWorld* instead of BSPMap*.
//      Physics only needs collision geometry, not rendering internals.
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
class IBSPCollisionWorld;

// -----------------------------------------------------------------------
// TraceResult
//
// Coordinate space: Y-up (GL/engine space).
// -----------------------------------------------------------------------
struct TraceResult
{
    float        fraction   = 1.0f;     // 0 = hit at start, 1 = no hit
    Vec3         normal     = {0,0,0};  // hit surface normal (world space, Y-up)
    Vec3         endPos;                // final position after trace
    EntityHandle entity;                // hit entity (invalid if world geometry)
    const char*  surface    = nullptr;  // hit surface name (nullptr if none)
    bool         startSolid = false;    // true if trace started inside solid
    bool         allSolid   = false;    // true if trace is entirely inside solid
};

// -----------------------------------------------------------------------
// IPhysicsWorld — pure virtual physics interface
//
// Coordinate system: Y-up (OpenGL), consistent with the renderer.
// The BSP loader converts from Q2 Z-up to Y-up on load.
// -----------------------------------------------------------------------
class IPhysicsWorld
{
public:
    virtual ~IPhysicsWorld() = default;

    // ---- World (collision geometry only) ----
    virtual void setWorld(IBSPCollisionWorld* world) = 0;

    // ---- Tick ----
    virtual void step(float dt) = 0;

    // ---- Entity operations ----
    virtual void setOrigin(EntityHandle e, const Vec3& origin) = 0;
    virtual void setVelocity(EntityHandle e, const Vec3& velocity) = 0;
    virtual Vec3 getOrigin(EntityHandle e) const = 0;
    virtual Vec3 getVelocity(EntityHandle e) const = 0;

    // ---- Collision tests ----
    virtual TraceResult trace(const Vec3& start, const Vec3& end,
                              const Vec3& mins, const Vec3& maxs) = 0;

    virtual TraceResult traceEntity(EntityHandle skip,
                                    const Vec3& start, const Vec3& end,
                                    const Vec3& mins, const Vec3& maxs) = 0;

    virtual TraceResult moveSlide(EntityHandle e, const Vec3& wishVel,
                                  float dt, float wishSpeed) = 0;

    // ---- Ground queries ----
    virtual bool         isOnGround(EntityHandle e)        = 0;
    virtual EntityHandle getGroundEntity(EntityHandle e)   = 0;
    virtual float        getGroundElevation(EntityHandle e) = 0;

    // ---- Settings ----
    virtual void  setGravity(float gravity) = 0;
    virtual float getGravity() const        = 0;
};

} // namespace nova
