// ============================================================
// FILE:    engine/physics/collide_util.h
// MODULE:  Physics
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Collision detection utilities.
// DEPENDS:  core/math
// ============================================================

#pragma once

#include "engine/core/math/vec.h"

#include <algorithm>
#include <cmath>

namespace nova
{

// -----------------------------------------------------------------------
// getComponent - get component by axis index
// -----------------------------------------------------------------------
inline float getComponent(const Vec3& v, int axis)
{
    if (axis == 0) return v.x;
    if (axis == 1) return v.y;
    return v.z;
}

// -----------------------------------------------------------------------
// intersectRayAABB - ray vs axis-aligned box
// Returns: true if ray intersects box, tMin/tMax are distances along ray
// -----------------------------------------------------------------------
inline bool intersectRayAABB(const Vec3& rayOrigin, const Vec3& rayDir,
                            const Vec3& boxMins, const Vec3& boxMaxs,
                            float *tMinOut, float *tMaxOut)
{
    float tMin = 0.0f;
    float tMax = 1e10f;

    for (int axis = 0; axis < 3; ++axis)
    {
        float axisOrigin = getComponent(rayOrigin, axis);
        float axisDir = getComponent(rayDir, axis);
        float boxMin = getComponent(boxMins, axis);
        float boxMax = getComponent(boxMaxs, axis);
        
        if (std::abs(axisDir) < 1e-8f)
        {
            if (axisOrigin < boxMin || axisOrigin > boxMax)
                return false;
            continue;
        }
        
        float t1 = (boxMin - axisOrigin) / axisDir;
        float t2 = (boxMax - axisOrigin) / axisDir;
        
        if (t1 > t2)
        {
            float temp = t1;
            t1 = t2;
            t2 = temp;
        }
        
        if (t1 > tMin) tMin = t1;
        if (t2 < tMax) tMax = t2;
        
        if (tMin > tMax)
            return false;
    }
    
    *tMinOut = tMin;
    *tMaxOut = tMax;
    return true;
}

// -----------------------------------------------------------------------
// intersectRayAABBFromPoints - segment version
// Returns: fraction along segment (0-1), 1.0 if no hit
// -----------------------------------------------------------------------
inline float intersectRayAABBFromPoints(const Vec3& start, const Vec3& end,
                                       const Vec3& boxMins, const Vec3& boxMaxs)
{
    Vec3 dir = end - start;
    float len = dir.length();
    
    if (len < 0.001f)
        return 1.0f;
    
    Vec3 rDir = dir * (1.0f / len);
    
    float tMin = 0.0f;
    float tMax = len;
    
    for (int axis = 0; axis < 3; ++axis)
    {
        float axisStart = getComponent(start, axis);
        float axisDir = getComponent(rDir, axis);
        float boxMin = getComponent(boxMins, axis);
        float boxMax = getComponent(boxMaxs, axis);
        
        if (std::abs(axisDir) < 1e-8f)
        {
            if (axisStart < boxMin || axisStart > boxMax)
                return 1.0f;
            continue;
        }
        
        float t1 = (boxMin - axisStart) / axisDir;
        float t2 = (boxMax - axisStart) / axisDir;
        
        if (t1 > t2)
        {
            float temp = t1;
            t1 = t2;
            t2 = temp;
        }
        
        if (t1 > tMin) tMin = t1;
        if (t2 < tMax) tMax = t2;
        
        if (tMin > tMax)
            return 1.0f;
    }
    
    return tMin / len;
}

// -----------------------------------------------------------------------
// intersectRayPlane - ray vs plane
// Returns: true if intersects, t = distance along ray
// -----------------------------------------------------------------------
inline bool intersectRayPlane(const Vec3& rayOrigin, const Vec3& rayDir,
                            const Vec3& planeNormal, float planeDist,
                            float *tOut)
{
    float denom = rayDir.dot(planeNormal);
    
    if (std::abs(denom) < 1e-8f)
        return false;
    
    float t = -(rayOrigin.dot(planeNormal) + planeDist) / denom;
    
    if (t < 0.0f)
        return false;
    
    *tOut = t;
    return true;
}

// -----------------------------------------------------------------------
// intersectRayPlaneFromPoints - segment vs plane version
// Returns: fraction along segment (0-1), 1.0 if no hit
// -----------------------------------------------------------------------
inline float intersectRayPlaneFromPoints(const Vec3& start, const Vec3& end,
                                     const Vec3& planeNormal, float planeDist)
{
    Vec3 dir = end - start;
    
    float denom = dir.dot(planeNormal);
    if (std::abs(denom) < 1e-8f)
        return 1.0f;
    
    float t = -(start.dot(planeNormal) + planeDist) / denom;
    
    if (t < 0.0f || t > 1.0f)
        return 1.0f;
    
    return t;
}

} // namespace nova