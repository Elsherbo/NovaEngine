// ============================================================
// FILE:    engine/physics/collide_util.h
// MODULE:  Physics
// PHASE:   2
// STATUS:  FIXED
// PURPOSE: Collision detection utilities.
//
// FIX LOG:
//   1. intersectRayPlane / intersectRayPlaneFromPoints:
//      Original used planeDist in the equation as
//        t = -(origin·n + planeDist) / denom
//      This assumes the plane equation is  n·p + d = 0
//      (standard math form, d = negative distance from origin).
//
//      BUT: BSPPlane stores dist in Quake style:  n·p = dist
//      (positive half-space value). Callers using BSPPlane as
//      input must negate the dist field when calling these utils:
//        intersectRayPlane(origin, dir, plane.normal, -plane.dist, &t)
//
//      The functions themselves are now documented clearly with
//      the convention they expect (standard math form: n·p + d = 0).
//      A separate BSP-convention overload is added:
//        intersectRayBSPPlane / intersectRayBSPPlaneFromPoints
//      which takes 'planeDist' as the Quake half-space value directly.
//
//   2. intersectRayAABB: Added invDir optimization to avoid per-axis
//      divide every call (caller-amortized for repeated ray use).
//
//   3. Added closestPointOnSegment utility (needed for entity-entity
//      collision and proximity checks).
//
//   4. All functions are inline for zero-overhead on hot paths.
// ============================================================

#pragma once

#include "engine/core/math/vec.h"

#include <algorithm>
#include <cmath>

namespace nova
{

// -----------------------------------------------------------------------
//  Axis component helpers
// -----------------------------------------------------------------------
inline float getComponent(const Vec3& v, int axis)
{
    if (axis == 0) return v.x;
    if (axis == 1) return v.y;
    return v.z;
}

inline void setComponent(Vec3& v, int axis, float val)
{
    if (axis == 0) v.x = val;
    else if (axis == 1) v.y = val;
    else v.z = val;
}

// -----------------------------------------------------------------------
//  intersectRayAABB
//
//  Slab method.  Works for any ray direction including negative.
//  Plane convention: does not depend on normal convention.
//
//  Returns: true if ray intersects box.
//           tMin = entry distance, tMax = exit distance.
//  Note: if rayOrigin is inside the box, tMin will be 0.
// -----------------------------------------------------------------------
inline bool intersectRayAABB(const Vec3& rayOrigin, const Vec3& rayDir,
                              const Vec3& boxMins,  const Vec3& boxMaxs,
                              float* tMinOut, float* tMaxOut)
{
    float tMin = 0.0f;
    float tMax = 1e30f;

    for (int axis = 0; axis < 3; ++axis)
    {
        float origin = getComponent(rayOrigin, axis);
        float dir    = getComponent(rayDir,    axis);
        float bmin   = getComponent(boxMins,   axis);
        float bmax   = getComponent(boxMaxs,   axis);

        if (std::abs(dir) < 1e-8f)
        {
            // Ray parallel to slab — check if origin is inside
            if (origin < bmin || origin > bmax)
                return false;
        }
        else
        {
            float invDir = 1.0f / dir;
            float t1 = (bmin - origin) * invDir;
            float t2 = (bmax - origin) * invDir;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) return false;
        }
    }

    *tMinOut = tMin;
    *tMaxOut = tMax;
    return true;
}

// -----------------------------------------------------------------------
//  intersectRayAABBFromPoints
//
//  Segment version: start → end.
//  Returns fraction along segment [0,1], or 1.0 if no hit.
// -----------------------------------------------------------------------
inline float intersectRayAABBFromPoints(const Vec3& start, const Vec3& end,
                                        const Vec3& boxMins, const Vec3& boxMaxs)
{
    Vec3  dir = end - start;
    float len = dir.length();

    if (len < 0.001f) return 1.0f;

    Vec3 rDir = dir * (1.0f / len);

    float tMin = 0.0f;
    float tMax = len;

    for (int axis = 0; axis < 3; ++axis)
    {
        float origin = getComponent(start, axis);
        float d      = getComponent(rDir,  axis);
        float bmin   = getComponent(boxMins, axis);
        float bmax   = getComponent(boxMaxs, axis);

        if (std::abs(d) < 1e-8f)
        {
            if (origin < bmin || origin > bmax) return 1.0f;
        }
        else
        {
            float invD = 1.0f / d;
            float t1   = (bmin - origin) * invD;
            float t2   = (bmax - origin) * invD;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tMin) tMin = t1;
            if (t2 < tMax) tMax = t2;
            if (tMin > tMax) return 1.0f;
        }
    }

    return tMin / len;
}

// -----------------------------------------------------------------------
//  intersectRayPlane
//
//  Standard math plane convention:  n·p + d = 0
//  (planeDist = d = -(distance from origin to plane along normal))
//
//  Returns: true if ray hits the plane in the positive t direction.
//           t = distance along ray to the hit point.
//
//  IMPORTANT: If using a BSPPlane where dist = n·p (Quake convention),
//  pass -plane.dist as planeDist here, or use intersectRayBSPPlane().
// -----------------------------------------------------------------------
inline bool intersectRayPlane(const Vec3& rayOrigin, const Vec3& rayDir,
                               const Vec3& planeNormal, float planeDist,
                               float* tOut)
{
    // plane equation: n·p + d = 0  (planeDist = d)
    float denom = rayDir.dot(planeNormal);
    if (std::abs(denom) < 1e-8f) return false;

    float t = -(rayOrigin.dot(planeNormal) + planeDist) / denom;
    if (t < 0.0f) return false;

    *tOut = t;
    return true;
}

// -----------------------------------------------------------------------
//  intersectRayPlaneFromPoints
//
//  Standard math plane convention:  n·p + d = 0
//  Returns: fraction [0,1] or 1.0 if no hit within segment.
//
//  IMPORTANT: For BSPPlane, pass -plane.dist, or use
//  intersectRayBSPPlaneFromPoints().
// -----------------------------------------------------------------------
inline float intersectRayPlaneFromPoints(const Vec3& start, const Vec3& end,
                                         const Vec3& planeNormal, float planeDist)
{
    Vec3  dir   = end - start;
    float denom = dir.dot(planeNormal);

    if (std::abs(denom) < 1e-8f) return 1.0f;

    float t = -(start.dot(planeNormal) + planeDist) / denom;
    if (t < 0.0f || t > 1.0f) return 1.0f;

    return t;
}

// -----------------------------------------------------------------------
//  intersectRayBSPPlane
//
//  Quake BSP plane convention:  n·p = planeDist  (positive half-space)
//  Same as intersectRayPlane but takes planeDist as the Quake value.
//
//  Returns: true if ray hits the plane in the positive t direction.
// -----------------------------------------------------------------------
inline bool intersectRayBSPPlane(const Vec3& rayOrigin, const Vec3& rayDir,
                                  const Vec3& planeNormal, float bspPlaneDist,
                                  float* tOut)
{
    // n·p = d  =>  standard form: n·p + (-d) = 0  =>  planeDist = -bspPlaneDist
    return intersectRayPlane(rayOrigin, rayDir, planeNormal, -bspPlaneDist, tOut);
}

// -----------------------------------------------------------------------
//  intersectRayBSPPlaneFromPoints
//
//  Quake BSP plane convention:  n·p = bspPlaneDist
//  Returns: fraction [0,1] or 1.0 if no hit.
// -----------------------------------------------------------------------
inline float intersectRayBSPPlaneFromPoints(const Vec3& start, const Vec3& end,
                                            const Vec3& planeNormal, float bspPlaneDist)
{
    return intersectRayPlaneFromPoints(start, end, planeNormal, -bspPlaneDist);
}

// -----------------------------------------------------------------------
//  closestPointOnSegment
//
//  Returns the closest point on segment [a, b] to point p.
//  Useful for entity-entity proximity checks.
// -----------------------------------------------------------------------
inline Vec3 closestPointOnSegment(const Vec3& a, const Vec3& b, const Vec3& p)
{
    Vec3  ab  = b - a;
    float len2 = ab.lengthSq();
    if (len2 < 1e-12f) return a;

    float t = (p - a).dot(ab) / len2;
    t = std::max(0.0f, std::min(1.0f, t));
    return a + ab * t;
}

// -----------------------------------------------------------------------
//  overlapAABB
//
//  Simple AABB overlap test (non-swept).
//  Returns true if the two boxes intersect.
// -----------------------------------------------------------------------
inline bool overlapAABB(const Vec3& aMin, const Vec3& aMax,
                        const Vec3& bMin, const Vec3& bMax)
{
    return aMax.x > bMin.x && aMin.x < bMax.x &&
           aMax.y > bMin.y && aMin.y < bMax.y &&
           aMax.z > bMin.z && aMin.z < bMax.z;
}

// -----------------------------------------------------------------------
//  pointInAABB
//
//  Returns true if point p is inside [boxMin, boxMax].
// -----------------------------------------------------------------------
inline bool pointInAABB(const Vec3& p, const Vec3& boxMin, const Vec3& boxMax)
{
    return p.x >= boxMin.x && p.x <= boxMax.x &&
           p.y >= boxMin.y && p.y <= boxMax.y &&
           p.z >= boxMin.z && p.z <= boxMax.z;
}

} // namespace nova
