// ============================================================
// FILE:    engine/world/iworld.h
// MODULE:  World
// PHASE:   3
// STATUS:  NEW
// PURPOSE: High-level world query interface.
//          Hides BSP internals from the game DLL and from any
//          engine system that only needs spatial queries.
//
//          The game DLL and IPhysicsWorld both talk to IWorld.
//          BSPWorld is the concrete BSP implementation.
//          Later you could add a VoxelWorld, a flat-plane test
//          world, or a networked ghost world without touching
//          any caller.
//
// DESIGN RULES:
//   - No BSP types leak through this interface.
//   - TraceResult / Vec3 / EntityHandle are engine primitives
//     already visible to the game DLL — they may appear here.
//   - getEntityString() exists so MapLoader can stay decoupled
//     from bsp.h.  MapLoader receives an IWorld* instead.
// ============================================================

#pragma once

#include "engine/core/math/vec.h"
#include "engine/physics/iphysics_world.h"   // TraceResult, EntityHandle

namespace nova
{

// -----------------------------------------------------------------------
// IWorld — pure virtual
// -----------------------------------------------------------------------
class IWorld
{
public:
    virtual ~IWorld() = default;

    // ---- Spatial queries -----------------------------------------------

    // Trace a ray (zero-size box) through world geometry.
    // Returns fraction=1 and startSolid=false if nothing is hit.
    virtual TraceResult traceLine(const Vec3& from, const Vec3& to) = 0;

    // Trace an AABB swept from 'from' to 'to'.
    // mins/maxs are relative to the sweep origin (centred hull).
    virtual TraceResult traceBox(const Vec3& from, const Vec3& to,
                                  const Vec3& mins, const Vec3& maxs) = 0;

    // ---- Visibility (PVS) ---------------------------------------------

    // Find the PVS cluster index for a point in world space.
    // Returns -1 if the point is outside the world (solid / no leaf).
    virtual int  clusterForPoint(const Vec3& pos) const = 0;

    // Returns true if 'toCluster' is visible from 'fromCluster'.
    // Both cluster indices come from clusterForPoint().
    // Returns true when either index is -1 (always-visible fallback).
    virtual bool isClusterVisible(int fromCluster, int toCluster) const = 0;

    // ---- Spawn info ---------------------------------------------------

    // World-space spawn origin read from the BSP entity lump.
    virtual Vec3 getSpawnOrigin() const = 0;

    // Yaw angle at the spawn point (degrees, Y-up convention).
    virtual Vec3 getSpawnAngles() const = 0;

    // ---- Entity lump --------------------------------------------------

    // Raw entity-lump string from the BSP.
    // MapLoader uses this; no other caller should need it.
    virtual const char* getEntityString() const = 0;
};

} // namespace nova