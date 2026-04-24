// ============================================================
// FILE:    engine/core/math/shapes.h
// MODULE:  Core > Math
// PHASE:   1
// STATUS:  IN_PROGRESS
// PURPOSE: Plane, AABB, Ray — foundation for BSP traversal
//          and AABB collision. All intersection tests included.
// DEPENDS: core/math/vec.h
// ============================================================
#pragma once

#include "engine/core/math/math.h"
#include "engine/core/math/vec.h"
#include <algorithm>
#include <cfloat>

namespace nova
{

    // ---- Plane -------------------------------------------------
    // Ax + By + Cz + D = 0  (normal points into positive half-space)
    struct Plane
    {
        Vec3 normal;   // must be unit length
        float d = 0.f; // distance from origin

        Plane() = default;
        Plane(const Vec3 &n, float d) : normal(n), d(d) {}
        Plane(const Vec3 &n, const Vec3 &point) : normal(n), d(-n.dot(point)) {}

        // Signed distance from plane to point (positive = front side)
        float distanceTo(const Vec3 &p) const { return normal.dot(p) + d; }

        // Which side is the point on?
        enum class Side
        {
            Front,
            Back,
            On
        };
        Side classify(const Vec3 &p, float eps = 0.001f) const
        {
            float dist = distanceTo(p);
            if (dist > eps)
                return Side::Front;
            if (dist < -eps)
                return Side::Back;
            return Side::On;
        }
    };

    // ---- AABB --------------------------------------------------
    struct AABB
    {
        Vec3 min;
        Vec3 max;

        AABB() : min({FLT_MAX, FLT_MAX, FLT_MAX}),
                 max({-FLT_MAX, -FLT_MAX, -FLT_MAX}) {}
        AABB(const Vec3 &mn, const Vec3 &mx) : min(mn), max(mx) {}

        Vec3 center() const { return (min + max) * 0.5f; }
        Vec3 extents() const { return (max - min) * 0.5f; }

        bool contains(const Vec3 &p) const
        {
            return p.x >= min.x && p.x <= max.x &&
                   p.y >= min.y && p.y <= max.y &&
                   p.z >= min.z && p.z <= max.z;
        }

        bool overlaps(const AABB &b) const
        {
            return min.x <= b.max.x && max.x >= b.min.x &&
                   min.y <= b.max.y && max.y >= b.min.y &&
                   min.z <= b.max.z && max.z >= b.min.z;
        }

        // Expand to include a point
        void expand(const Vec3 &p)
        {
            min.x = std::min(min.x, p.x);
            min.y = std::min(min.y, p.y);
            min.z = std::min(min.z, p.z);
            max.x = std::max(max.x, p.x);
            max.y = std::max(max.y, p.y);
            max.z = std::max(max.z, p.z);
        }

        // Expand to include another AABB
        void expand(const AABB &b)
        {
            expand(b.min);
            expand(b.max);
        }

        // Positive half-space distance from plane to nearest AABB corner
        // Used by BSP frustum culling
        float nearestSignedDistance(const Plane &plane) const
        {
            Vec3 positive = {
                plane.normal.x >= 0.f ? max.x : min.x,
                plane.normal.y >= 0.f ? max.y : min.y,
                plane.normal.z >= 0.f ? max.z : min.z};
            return plane.distanceTo(positive);
        }
    };

    // ---- Ray ---------------------------------------------------
    struct Ray
    {
        Vec3 origin;
        Vec3 direction; // should be normalized

        Ray() = default;
        Ray(const Vec3 &o, const Vec3 &d) : origin(o), direction(d) {}

        Vec3 at(float t) const { return origin + direction * t; }

        // Slab-method AABB intersection. Returns true and sets tMin/tMax.
        bool intersectsAABB(const AABB &box, float &tMin, float &tMax) const
        {
            tMin = 0.f;
            tMax = FLT_MAX;
            const float *o = &origin.x;
            const float *d = &direction.x;
            const float *bmin = &box.min.x;
            const float *bmax = &box.max.x;
            for (int i = 0; i < 3; ++i)
            {
                if (std::abs(d[i]) < 1e-8f)
                {
                    if (o[i] < bmin[i] || o[i] > bmax[i])
                        return false;
                }
                else
                {
                    float invD = 1.0f / d[i];
                    float t1 = (bmin[i] - o[i]) * invD;
                    float t2 = (bmax[i] - o[i]) * invD;
                    if (t1 > t2)
                        std::swap(t1, t2);
                    tMin = std::max(tMin, t1);
                    tMax = std::min(tMax, t2);
                    if (tMin > tMax)
                        return false;
                }
            }
            return true;
        }

        // Ray vs plane intersection. Returns false if parallel or behind.
        bool intersectsPlane(const Plane &plane, float &t) const
        {
            float denom = plane.normal.dot(direction);
            if (std::abs(denom) < 1e-8f)
                return false;
            t = -(plane.normal.dot(origin) + plane.d) / denom;
            return t >= 0.f;
        }
    };

} // namespace nova
