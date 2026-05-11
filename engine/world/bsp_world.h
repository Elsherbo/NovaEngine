// ============================================================
// FILE:    engine/world/bsp_world.h
// MODULE:  World
// PHASE:   3
// STATUS:  NEW
// PURPOSE: Concrete IWorld implementation backed by a BSPMap.
//
//          BSPWorld owns nothing — it holds a non-owning pointer
//          to the BSPMap that engine.cpp already owns.
//          Engine lifetime: BSPWorld is created after BSPMap::load()
//          succeeds and destroyed before BSPMap is deleted.
//
// USAGE (engine.cpp):
//   m_bspWorld = std::make_unique<BSPWorld>(m_bsp);
//   m_physics->setWorld(m_bspWorld.get());       // IWorld* now
//   game->loadMap(m_bspWorld.get());             // IWorld* now
// ============================================================

#pragma once

#include "engine/world/iworld.h"
#include "engine/renderer/bsp/bsp.h"

#include <vector>
#include <cstdint>

namespace nova
{

class BSPWorld : public IWorld
{
public:
    // bsp must outlive this object.
    explicit BSPWorld(BSPMap* bsp);

    // ---- IWorld -------------------------------------------------------
    TraceResult traceLine(const Vec3& from, const Vec3& to) override;
    TraceResult traceBox(const Vec3& from, const Vec3& to,
                          const Vec3& mins, const Vec3& maxs) override;

    int  clusterForPoint(const Vec3& pos) const override;
    bool isClusterVisible(int fromCluster, int toCluster) const override;

    Vec3        getSpawnOrigin()   const override;
    Vec3        getSpawnAngles()   const override;
    const char* getEntityString()  const override;
    Vec3        getBModelOrigin(int modelIndex) const override;
    void        getBModelBounds(int modelIndex, Vec3& mins, Vec3& maxs) const override;

    // ---- Pass-through to IBSPCollisionWorld (for IPhysicsWorld) -------
    // Physics still needs IBSPCollisionWorld* — expose it directly so
    // engine.cpp can pass it to IPhysicsWorld::setWorld() without casting.
    IBSPCollisionWorld* collisionWorld() const { return m_bsp; }

private:
    // Walk the BSP tree to the leaf that contains pos.
    // Returns the leaf index, or -1 if out of bounds / in solid.
    int findLeaf(const Vec3& pos) const;

    // Decompress the PVS bitset for a cluster into m_pvsCache.
    // Cached: only re-decompresses when the cluster changes.
    const std::vector<uint8_t>& getPVS(int cluster) const;

    BSPMap* m_bsp = nullptr;

    // PVS decompression cache.
    mutable int                  m_lastCluster = -2; // -2 forces first fill
    mutable std::vector<uint8_t> m_pvsCache;
};

} // namespace nova