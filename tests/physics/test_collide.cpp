// ============================================================
// FILE:    tests/physics/test_collide.cpp
// MODULE:  Physics
// PHASE:   2
// STATUS:  UPDATED
// PURPOSE: Test collision detection utilities.
//          Tests ray/segment vs AABB and plane intersections,
//          including both standard-math and BSP plane conventions.
// ============================================================

#include <cstdio>
#include <cassert>
#include <cmath>

#include "engine/physics/collide_util.h"

using namespace nova;

static int g_passed = 0;
static int g_failed = 0;

#define TEST(expr) do { \
    if (expr) { \
        printf("  PASS: %s\n", #expr); \
        ++g_passed; \
    } else { \
        printf("  FAIL: %s  (line %d)\n", #expr, __LINE__); \
        ++g_failed; \
    } \
} while(0)

#define TEST_FLOAT(val, expected, tol) do { \
    float _v = (val); float _e = (expected); float _t = (tol); \
    if (std::abs(_v - _e) < _t) { \
        printf("  PASS: %s  (%.4f)\n", #val, _v); \
        ++g_passed; \
    } else { \
        printf("  FAIL: %s  got %.4f  expected %.4f  tol %.4f  (line %d)\n", \
               #val, _v, _e, _t, __LINE__); \
        ++g_failed; \
    } \
} while(0)

// -----------------------------------------------------------------------
void test_ray_aabb_hit()
{
    printf("\n=== Ray-AABB hit ===\n");

    Vec3 origin = {0, 0, 0};
    Vec3 dir    = {1, 0, 0};
    Vec3 bmin   = {5, -1, -1};
    Vec3 bmax   = {10, 1, 1};

    float tMin, tMax;
    bool hit = intersectRayAABB(origin, dir, bmin, bmax, &tMin, &tMax);
    TEST(hit);
    TEST_FLOAT(tMin, 5.0f, 0.01f);
    TEST_FLOAT(tMax, 10.0f, 0.01f);
}

void test_ray_aabb_miss()
{
    printf("\n=== Ray-AABB miss ===\n");

    // Ray along X at Y=5, box straddles Y=[-1,1]
    Vec3 origin = {0, 5, 0};
    Vec3 dir    = {1, 0, 0};
    Vec3 bmin   = {5, -1, -1};
    Vec3 bmax   = {10, 1, 1};

    float tMin, tMax;
    bool hit = intersectRayAABB(origin, dir, bmin, bmax, &tMin, &tMax);
    TEST(!hit);
}

void test_ray_aabb_origin_inside()
{
    printf("\n=== Ray-AABB origin inside box ===\n");

    // Origin is inside the box — should hit with tMin = 0
    Vec3 origin = {7, 0, 0};
    Vec3 dir    = {1, 0, 0};
    Vec3 bmin   = {5, -1, -1};
    Vec3 bmax   = {10, 1, 1};

    float tMin, tMax;
    bool hit = intersectRayAABB(origin, dir, bmin, bmax, &tMin, &tMax);
    TEST(hit);
    TEST_FLOAT(tMin, 0.0f, 0.01f);
    TEST_FLOAT(tMax, 3.0f, 0.01f);   // exits at x=10, dist from 7 = 3
}

void test_segment_aabb()
{
    printf("\n=== Segment-AABB fraction ===\n");

    // Segment 0→20 along X; box at [5,10]
    // Entry at t=5/20 = 0.25
    Vec3 start = {0, 0, 0};
    Vec3 end   = {20, 0, 0};
    Vec3 bmin  = {5, -1, -1};
    Vec3 bmax  = {10, 1, 1};

    float frac = intersectRayAABBFromPoints(start, end, bmin, bmax);
    TEST_FLOAT(frac, 0.25f, 0.01f);
}

void test_segment_aabb_miss()
{
    printf("\n=== Segment-AABB miss ===\n");

    // Segment along X at Y=5
    Vec3 start = {0, 5, 0};
    Vec3 end   = {20, 5, 0};
    Vec3 bmin  = {5, -1, -1};
    Vec3 bmax  = {10, 1, 1};

    float frac = intersectRayAABBFromPoints(start, end, bmin, bmax);
    TEST_FLOAT(frac, 1.0f, 0.001f);
}

// -----------------------------------------------------------------------
void test_ray_plane_standard()
{
    printf("\n=== Ray-Plane (standard math: n·p + d = 0) ===\n");

    // Plane x = 0  →  normal=(1,0,0), d=0  →  n·p + 0 = 0
    Vec3 origin = {-5, 0, 0};
    Vec3 dir    = {1, 0, 0};
    Vec3 normal = {1, 0, 0};
    float planeDist = 0;  // standard-math d

    float t;
    bool hit = intersectRayPlane(origin, dir, normal, planeDist, &t);
    TEST(hit);
    TEST_FLOAT(t, 5.0f, 0.01f);
}

void test_ray_bsp_plane()
{
    printf("\n=== Ray-BSP-Plane (Quake: n·p = dist) ===\n");

    // Quake plane: normal=(1,0,0), dist=10  →  wall at x=10
    Vec3 origin = {0, 0, 0};
    Vec3 dir    = {1, 0, 0};
    Vec3 normal = {1, 0, 0};
    float bspDist = 10.0f;

    float t;
    bool hit = intersectRayBSPPlane(origin, dir, normal, bspDist, &t);
    TEST(hit);
    TEST_FLOAT(t, 10.0f, 0.01f);
}

void test_segment_plane_standard()
{
    printf("\n=== Segment-Plane (standard math) ===\n");

    // Segment -10→+10 along X; plane at x=0 (n=(1,0,0), d=0)
    Vec3 start  = {-10, 0, 0};
    Vec3 end    = { 10, 0, 0};
    Vec3 normal = {1, 0, 0};
    float d = 0;

    float frac = intersectRayPlaneFromPoints(start, end, normal, d);
    TEST_FLOAT(frac, 0.5f, 0.01f);
}

void test_segment_bsp_plane()
{
    printf("\n=== Segment-BSP-Plane ===\n");

    // Segment 0→20 along X; BSP plane at x=10
    Vec3 start  = {0, 0, 0};
    Vec3 end    = {20, 0, 0};
    Vec3 normal = {1, 0, 0};
    float bspDist = 10.0f;

    float frac = intersectRayBSPPlaneFromPoints(start, end, normal, bspDist);
    TEST_FLOAT(frac, 0.5f, 0.01f);  // hits at x=10, which is 10/20 = 0.5
}

// -----------------------------------------------------------------------
void test_point_in_aabb()
{
    printf("\n=== pointInAABB ===\n");

    Vec3 bmin = {-5, -5, -5};
    Vec3 bmax = {5, 5, 5};

    TEST( pointInAABB({0, 0, 0},   bmin, bmax));
    TEST( pointInAABB({4, 4, 4},   bmin, bmax));
    TEST(!pointInAABB({6, 0, 0},   bmin, bmax));
    TEST(!pointInAABB({0, 0, -6},  bmin, bmax));
}

void test_overlap_aabb()
{
    printf("\n=== overlapAABB ===\n");

    Vec3 aMin = {0, 0, 0};
    Vec3 aMax = {10, 10, 10};

    TEST( overlapAABB(aMin, aMax, {5, 5, 5}, {15, 15, 15}));  // overlap
    TEST( overlapAABB(aMin, aMax, {-5, -5, -5}, {5, 5, 5})); // corner overlap
    TEST(!overlapAABB(aMin, aMax, {11, 0, 0}, {20, 10, 10})); // gap on X
    TEST(!overlapAABB(aMin, aMax, {0, 11, 0}, {10, 20, 10})); // gap on Y
}

void test_closest_point_on_segment()
{
    printf("\n=== closestPointOnSegment ===\n");

    Vec3 a = {0, 0, 0};
    Vec3 b = {10, 0, 0};

    Vec3 c1 = closestPointOnSegment(a, b, {5, 3, 0});  // closest to midpoint
    TEST_FLOAT(c1.x, 5.0f, 0.01f);
    TEST_FLOAT(c1.y, 0.0f, 0.01f);

    Vec3 c2 = closestPointOnSegment(a, b, {-5, 0, 0}); // before A → clamp to A
    TEST_FLOAT(c2.x, 0.0f, 0.01f);

    Vec3 c3 = closestPointOnSegment(a, b, {15, 0, 0}); // after B → clamp to B
    TEST_FLOAT(c3.x, 10.0f, 0.01f);
}

// -----------------------------------------------------------------------
int main()
{
    printf("=== Collision Utility Tests ===\n");

    test_ray_aabb_hit();
    test_ray_aabb_miss();
    test_ray_aabb_origin_inside();
    test_segment_aabb();
    test_segment_aabb_miss();
    test_ray_plane_standard();
    test_ray_bsp_plane();
    test_segment_plane_standard();
    test_segment_bsp_plane();
    test_point_in_aabb();
    test_overlap_aabb();
    test_closest_point_on_segment();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
