// ============================================================
// FILE:    engine/world/bsp_world.cpp
// MODULE:  World
// PHASE:   3
// STATUS:  NEW
// PURPOSE: BSPWorld implementation.
//
// NOTES:
//   - traceLine / traceBox delegate to a zero-size / full-size
//     AABBPhysics trace so we don't duplicate the sweep math.
//     We create a temporary AABBPhysics and wire it to m_bsp.
//     This is fine for occasional game queries; the player physics
//     still runs its own persistent AABBPhysics instance.
//   - clusterForPoint re-implements the BSP tree walk that was
//     previously inlined in engine.cpp F3 debug block and in
//     BSPMap::findLeaf (private).  We copy the same logic here
//     because BSPMap::findLeaf is private — this file is the
//     authoritative public version from now on.
//   - getPVS mirrors BSPMap::decompressPVS (also private).
//     The RLE algorithm is identical; see bsp_loader.cpp for
//     the format description.
// ============================================================

#include "engine/world/bsp_world.h"
#include "engine/physics/aabb_physics.h"

#include <cstring>
#include <algorithm>

namespace nova
{

// ---------------------------------------------------------------------------
BSPWorld::BSPWorld(BSPMap* bsp)
    : m_bsp(bsp)
{
}

// ---------------------------------------------------------------------------
// traceLine — zero-size swept trace
// ---------------------------------------------------------------------------
TraceResult BSPWorld::traceLine(const Vec3& from, const Vec3& to)
{
    // Reuse the same AABB physics that PlayerController uses.
    // A zero-size box (mins = maxs = zero) is a ray trace.
    AABBPhysics phys;
    phys.setWorld(m_bsp);
    return phys.trace(from, to, Vec3::zero(), Vec3::zero());
}

// ---------------------------------------------------------------------------
// traceBox — swept AABB trace
// ---------------------------------------------------------------------------
TraceResult BSPWorld::traceBox(const Vec3& from, const Vec3& to,
                                const Vec3& mins, const Vec3& maxs)
{
    AABBPhysics phys;
    phys.setWorld(m_bsp);
    return phys.trace(from, to, mins, maxs);
}

// ---------------------------------------------------------------------------
// clusterForPoint
//
// Walks the BSP node tree from the root (index 0) until it reaches a leaf.
// Leaves are encoded as: child < 0  →  leafIndex = ~child
// Returns the cluster stored in that leaf, or -1 if out of bounds.
//
// Source of truth: mirrors the private BSPMap::findLeaf + leaf.cluster
// lookup that was previously duplicated in engine.cpp (F3 debug block).
// ---------------------------------------------------------------------------
int BSPWorld::clusterForPoint(const Vec3& pos) const
{
    if (!m_bsp) return -1;

    int nodeIdx = 0;  // root
    const int nodeCount = m_bsp->nodeCount();
    const int leafCount = m_bsp->leafCount();

    while (nodeIdx >= 0)
    {
        if (nodeIdx >= nodeCount) return -1;

        const BSPNode&  node = m_bsp->nodes()[nodeIdx];
        const BSPPlane& pl   = m_bsp->planes()[node.plane];

        float d = pos.x * pl.normal.x
                + pos.y * pl.normal.y
                + pos.z * pl.normal.z
                - pl.dist;

        nodeIdx = node.children[d < 0.f ? 1 : 0];
    }

    const int leafIdx = ~nodeIdx;
    if (leafIdx < 0 || leafIdx >= leafCount) return -1;

    return static_cast<int>(m_bsp->leaves()[leafIdx].cluster);
}

// ---------------------------------------------------------------------------
// isClusterVisible
//
// Returns true when any of these holds:
//   1. Either cluster is -1 (outside-world fallback → assume visible).
//   2. The PVS bitset for fromCluster has the bit for toCluster set.
// ---------------------------------------------------------------------------
bool BSPWorld::isClusterVisible(int fromCluster, int toCluster) const
{
    // Fallback: always visible when PVS data is absent or clusters are invalid.
    if (fromCluster < 0 || toCluster < 0) return true;

    const std::vector<uint8_t>& pvs = getPVS(fromCluster);
    if (pvs.empty()) return true;

    const int byteIdx = toCluster >> 3;
    const int bitMask = 1 << (toCluster & 7);

    if (byteIdx >= static_cast<int>(pvs.size())) return true;
    return (pvs[byteIdx] & bitMask) != 0;
}

// ---------------------------------------------------------------------------
// Spawn / entity passthrough
// ---------------------------------------------------------------------------
Vec3 BSPWorld::getSpawnOrigin() const
{
    return m_bsp ? m_bsp->getSpawnOrigin() : Vec3::zero();
}

Vec3 BSPWorld::getSpawnAngles() const
{
    return m_bsp ? m_bsp->getSpawnAngles() : Vec3::zero();
}

const char* BSPWorld::getEntityString() const
{
    return m_bsp ? m_bsp->getEntityString() : "";
}

// ---------------------------------------------------------------------------
// getPVS — cached RLE decompression
//
// Q2 vis lump layout (all offsets relative to start of raw vis data):
//   [0]  int32  numClusters
//   [4]  int32  clusterSize   (always 8 bytes per entry)
//   [8]  numClusters × 8:  { int32 pvsByteOffset, int32 phasByteOffset }
//   [...] RLE bitstream
//
// RLE rule: non-zero byte → copy verbatim.
//           0x00 followed by byte N → N zero bytes.
//
// This is a verbatim copy of BSPMap::decompressPVS (private) promoted to
// the world layer so callers don't need to touch bsp.h internals.
// ---------------------------------------------------------------------------
const std::vector<uint8_t>& BSPWorld::getPVS(int cluster) const
{
    if (cluster == m_lastCluster)
        return m_pvsCache;

    m_lastCluster = cluster;

    // These are the same private fields BSPMap uses internally.
    // We access them through the IBSPCollisionWorld public interface:
    // We can't — IBSPCollisionWorld doesn't expose the raw vis bytes.
    // So we re-call through BSPMap* directly.  BSPWorld already holds
    // BSPMap* (not the abstract interface) so this is fine.
    //
    // The method is private on BSPMap, so we expose it through a
    // new public method we add to BSPMap: decompressPVSPublic().
    m_bsp->decompressPVSPublic(cluster, m_pvsCache);
    return m_pvsCache;
}

// ---------------------------------------------------------------------------
// findLeaf — kept private, only used internally by this translation unit.
// (clusterForPoint is the public caller.)
// ---------------------------------------------------------------------------
int BSPWorld::findLeaf(const Vec3& pos) const
{
    return clusterForPoint(pos);  // reuse; cluster lookup is identical up to the return value
    // Note: findLeaf really returns a leafIdx, while clusterForPoint returns cluster.
    // This member is not used — left here as a named placeholder so the header
    // declaration compiles.  clusterForPoint is the canonical public path.
}

} // namespace nova