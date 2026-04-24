// ============================================================
// FILE:    engine/physics/aabb_physics.cpp
// MODULE:  Physics
// PHASE:   2
// STATUS:  PARTIAL
// PURPOSE: AABB physics with BSP world collision.
//          Swept AABB vs plane using Minkowski expansion.
// DEPENDS:  physics/iphysics_world.h
// ============================================================

#include "engine/physics/aabb_physics.h"

#include <algorithm>
#include <cmath>
#include <cassert>

namespace nova
{

// ---- Constants ----
static constexpr float kClipEpsilon      = 0.03125f;
static constexpr float kPlaneTolerance    = 0.125f;
static constexpr float kPenetrationSlack = 0.1f;
static constexpr float kMinMoveDist      = 1e-6f;
static constexpr float kMinVelocitySq     = 1e-8f;
static constexpr int  kMaxIterations    = 4;
static constexpr int  kStackSize        = 64;

// ---- Helpers ----
static bool boxIntersectsAABB(
    const Vec3& boxMin, const Vec3& boxMax,
    const Vec3& nodeMin, const Vec3& nodeMax)
{
    return boxMax.x > nodeMin.x && boxMin.x < nodeMax.x &&
           boxMax.y > nodeMin.y && boxMin.y < nodeMax.y &&
           boxMax.z > nodeMin.z && boxMin.z < nodeMax.z;
}

static Vec3 computeBoxCenter(const Vec3& mins, const Vec3& maxs)
{
    return {(mins.x + maxs.x) * 0.5f,
            (mins.y + maxs.y) * 0.5f,
            (mins.z + maxs.z) * 0.5f};
}

static Vec3 computeBoxExtents(const Vec3& mins, const Vec3& maxs)
{
    return {(maxs.x - mins.x) * 0.5f,
            (maxs.y - mins.y) * 0.5f,
            (maxs.z - mins.z) * 0.5f};
}

// Signed distance from plane to box using Minkowski expansion.
// Plane equation: n·x = dist
// Offset = |n·extents| accounts for box half-extents.
static float planeBoxOffset(const Vec3& normal, const Vec3& extents)
{
    return std::abs(normal.x * extents.x) +
           std::abs(normal.y * extents.y) +
           std::abs(normal.z * extents.z);
}

AABBPhysics::AABBPhysics()
{
    m_bsp          = nullptr;
    m_gravity      = 1.0f;
    m_entOrigin    = nullptr;
    m_entVelocity = nullptr;
    m_playerMins  = {-16, -16, -36};
    m_playerMaxs  = {16, 16, 36};
}

void AABBPhysics::setWorld(BSPMap *bsp)
{
    m_bsp = bsp;
}

TraceResult AABBPhysics::traceWorld(const Vec3& start, const Vec3& dir, float dist,
                                  const Vec3& mins, const Vec3& maxs)
{
    TraceResult r;
    r.fraction = 1.0f;

    assert(m_bsp != nullptr && "BSP world must be set");

    if (m_bsp->m_nodes.empty() || dist < kMinMoveDist)
        return r;

    // Normalize direction once
    Vec3 dirNorm = dir.normalized();

    // Compute box geometry
    Vec3 boxCenter = computeBoxCenter(start + mins, start + maxs);
    Vec3 extents   = computeBoxExtents(mins, maxs);
    Vec3 boxMin    = boxCenter - extents;
    Vec3 boxMax    = boxCenter + extents;

    // ---- Phase 1: BSP traversal to collect candidate leaves ----
    int stack[kStackSize];
    int stackPos = 0;
    stack[stackPos++] = 0;  // root node index

    std::vector<int> candidateLeaves;
    candidateLeaves.reserve(64);

    while (stackPos > 0)
    {
        int nodeIdx = stack[--stackPos];

        // Handle nodes (nodeIdx >= 0)
        while (nodeIdx >= 0)
        {
            if (nodeIdx >= (int)m_bsp->m_nodes.size())
                break;

            const BSPNode& node = m_bsp->m_nodes[nodeIdx];
            const BSPPlane& plane = m_bsp->m_planes[node.plane];

            Vec3 nodeMin{float(node.mins[0]), float(node.mins[1]), float(node.mins[2])};
            Vec3 nodeMax{float(node.maxs[0]), float(node.maxs[1]), float(node.maxs[2])};

            // Broadphase: skip if no overlap
            if (!boxIntersectsAABB(boxMin, boxMax, nodeMin, nodeMax))
                break;

            // Distance from plane to box corners along plane normal
            float offset = planeBoxOffset(plane.normal, extents);
            float d1 = plane.dist - plane.normal.dot(boxCenter) + offset;
            float d2 = plane.dist - plane.normal.dot(boxCenter) - offset;

            // Determine front/back children
            int front = node.children[0];
            int back  = node.children[1];

            // Box is behind plane — swap
            if (d1 < 0)
            {
                std::swap(front, back);
                std::swap(d1, d2);
            }

            // Case: box is entirely in front of plane
            if (d2 >= -kPlaneTolerance)
            {
                stack[stackPos++] = front;
                nodeIdx = back;
                continue;
            }

            // Case: box straddles the plane — descend back child
            stack[stackPos++] = front;
            nodeIdx = back;
        }

        // Handle leaf (~nodeIdx gives leaf index in Quake convention)
        int leafIdx = ~nodeIdx;
        if (leafIdx >= 0 && leafIdx < (int)m_bsp->m_leaves.size())
            candidateLeaves.push_back(leafIdx);
    }

    // ---- Phase 2: Swept AABB vs plane (Minkowski method) ----
    float bestT = dist;
    Vec3 bestN  = {0, 0, 0};

    for (int leafIdx : candidateLeaves)
    {
        const BSPLeaf& leaf = m_bsp->m_leaves[leafIdx];

        if (leaf.numFaces == 0)
            continue;

        uint32_t endFace = leaf.firstFace + leaf.numFaces;
        for (uint32_t fi = leaf.firstFace; fi < endFace; ++fi)
        {
            uint32_t faceIdx = m_bsp->m_leafFaces[fi];
            if (faceIdx >= m_bsp->m_faces.size())
                continue;

            const BSPFace& face = m_bsp->m_faces[faceIdx];
            if (face.numEdges <= 0)
                continue;

            const BSPPlane& plane = m_bsp->m_planes[face.plane];
            const Vec3& n = plane.normal;

            // Projected velocity onto plane normal
            float denom = dirNorm.dot(n);
            if (std::abs(denom) < kMinMoveDist)
                continue;

            // Swept AABB vs plane using Minkowski expansion:
            // Effective plane dist = plane.dist - offset
            float offset = planeBoxOffset(n, extents);
            float effectiveDist = plane.dist - n.dot(boxCenter) - offset;

            // Time of impact
            float t = -(effectiveDist + offset) / denom;

            if (t < 0 || t > bestT)
                continue;

            // Refine: verify box actually touches plane at t
            Vec3 hitPos = boxCenter + dirNorm * t;
            float dFromPlane = n.dot(hitPos) - plane.dist;

            if (dFromPlane < kPenetrationSlack)
            {
                bestT = t;
                bestN = n;
            }
        }
    }

    // ---- Phase 3: Build result ----
    if (bestT < dist)
    {
        r.fraction = bestT / dist;
        r.endPos   = start + dirNorm * bestT;
        r.normal  = bestN;
    }
    else
    {
        r.fraction = 1.0f;
        r.endPos   = start + dir;
    }

    return r;
}

void AABBPhysics::step(float dt)
{
    (void)dt;
}

void AABBPhysics::setEntityStorage(Vec3 *origin, Vec3 *velocity)
{
    m_entOrigin    = origin;
    m_entVelocity = velocity;
}

void AABBPhysics::setOrigin(EntityHandle e, const Vec3& origin)
{
    if (m_entOrigin)
        m_entOrigin[e.index()] = origin;
}

void AABBPhysics::setVelocity(EntityHandle e, const Vec3& velocity)
{
    if (m_entVelocity)
        m_entVelocity[e.index()] = velocity;
}

Vec3 AABBPhysics::getOrigin(EntityHandle e) const
{
    return m_entOrigin ? m_entOrigin[e.index()] : Vec3{};
}

Vec3 AABBPhysics::getVelocity(EntityHandle e) const
{
    return m_entVelocity ? m_entVelocity[e.index()] : Vec3{};
}

TraceResult AABBPhysics::trace(const Vec3& start, const Vec3& end,
                              const Vec3& mins, const Vec3& maxs)
{
    Vec3 delta = end - start;
    float dist = delta.length();

    if (dist < kMinMoveDist)
    {
        TraceResult r;
        r.endPos   = start;
        r.fraction = 1.0f;
        return r;
    }

    return traceWorld(start, delta, dist, mins, maxs);
}

TraceResult AABBPhysics::traceEntity(EntityHandle skip, const Vec3& start, const Vec3& end,
                                  const Vec3& mins, const Vec3& maxs)
{
    (void)skip;
    return trace(start, end, mins, maxs);
}

TraceResult AABBPhysics::moveSlide(EntityHandle e, const Vec3& wishDir, float, float wishSpeed)
{
    if (!m_entOrigin || !m_entVelocity)
    {
        TraceResult r;
        return r;
    }

    float dt = 1.0f / 60.0f;  // 60 Hz physics
    Vec3 velocity = m_entVelocity[e.index()] + wishDir * wishSpeed;
    Vec3 pos = m_entOrigin[e.index()];
    Vec3 vel = velocity;

    for (int iter = 0; iter < kMaxIterations; ++iter)
    {
        if (vel.lengthSq() < kMinVelocitySq)
            break;

        Vec3 delta = vel * dt;
        float dist = delta.length();

        if (dist < kMinMoveDist)
            break;

        TraceResult tr = traceWorld(pos, delta, dist, m_playerMins, m_playerMaxs);

        pos = tr.endPos;

        if (tr.fraction >= 1.0f)
            break;

        // Quake-style slide: reflect velocity along collision normal
        float vDotN = vel.dot(tr.normal);
        if (vDotN < 0)
        {
            vel = vel - tr.normal * vDotN;
        }

        // Push out to resolve penetration
        pos = pos + tr.normal * kPenetrationSlack;
    }

    m_entOrigin[e.index()] = pos;
    m_entVelocity[e.index()] = vel;

    TraceResult r;
    r.endPos   = pos;
    r.fraction = 1.0f;
    r.normal   = vel.lengthSq() > kMinVelocitySq ? vel.normalized() : Vec3{};
    return r;
}

bool AABBPhysics::isOnGround(EntityHandle e)
{
    if (!m_entOrigin)
        return false;

    Vec3 o = m_entOrigin[e.index()];
    Vec3 down = {0, 0, -2};
    TraceResult tr = trace(o, o + down, m_playerMins, m_playerMaxs);
    return tr.fraction < 1.0f;
}

EntityHandle AABBPhysics::getGroundEntity(EntityHandle e)
{
    if (!m_entOrigin)
        return EntityHandle();
    return isOnGround(e) ? e : EntityHandle();
}

float AABBPhysics::getGroundElevation(EntityHandle e)
{
    if (!m_entOrigin)
        return -1e10f;
    return m_entOrigin[e.index()].z;
}

void AABBPhysics::setGravity(float g)
{
    m_gravity = g;
}

float AABBPhysics::getGravity() const
{
    return m_gravity;
}

} // namespace nova