// ============================================================
// FILE:    tests/physics/test_collide.cpp
// MODULE:  Physics
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Test collision detection
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
        printf("  FAIL: %s\n", #expr); \
        ++g_failed; \
    } \
} while(0)

#define TEST_FLOAT(expr, expected, tol) do { \
    float diff = std::abs((expr) - (expected)); \
    if (diff < (tol)) { \
        printf("  PASS: %s\n", #expr); \
        ++g_passed; \
    } else { \
        printf("  FAIL: %s (got %f, expected %f)\n", #expr, (expr), (expected)); \
        ++g_failed; \
    } \
} while(0)

void test_ray_aabb()
{
    printf("\n=== Ray-AABB ===\n");

    // Simple ray going through box
    Vec3 rayOrigin = {0, 0, 0};
    Vec3 rayDir = {1, 0, 0};  // along X
    
    Vec3 boxMins = {5, -1, -1};
    Vec3 boxMaxs = {10, 1, 1};
    
    float tMin, tMax;
    bool hit = intersectRayAABB(rayOrigin, rayDir, boxMins, boxMaxs, &tMin, &tMax);
    TEST(hit);
    TEST_FLOAT(tMin, 5.0f, 0.01f);
    TEST_FLOAT(tMax, 10.0f, 0.01f);
}

void test_ray_aabb_miss()
{
    printf("\n=== Ray-AABB Miss ===\n");

    // Ray going past the box
    Vec3 rayOrigin = {0, 5, 0};
    Vec3 rayDir = {1, 0, 0};  // along X, but box is at Y=-1 to Y=1
    
    Vec3 boxMins = {5, -1, -1};
    Vec3 boxMaxs = {10, 1, 1};
    
    float tMin, tMax;
    bool hit = intersectRayAABB(rayOrigin, rayDir, boxMins, boxMaxs, &tMin, &tMax);
    TEST(!hit);
}

void test_segment_aabb()
{
    printf("\n=== Segment-AABB ===\n");

    // Segment going through box
    Vec3 start = {0, 0, 0};
    Vec3 end = {20, 0, 0};
    
    Vec3 boxMins = {5, -1, -1};
    Vec3 boxMaxs = {10, 1, 1};
    
    float frac = intersectRayAABBFromPoints(start, end, boxMins, boxMaxs);
    TEST_FLOAT(frac, 0.25f, 0.01f);  // hit at t=0.25 (5/20)
}

void test_ray_plane()
{
    printf("\n=== Ray-Plane ===\n");

    // Ray hitting plane at origin, normal = (1, 0, 0)
    Vec3 rayOrigin = {-5, 0, 0};
    Vec3 rayDir = {1, 0, 0};
    
    Vec3 planeNormal = {-1, 0, 0};  // facing -X
    float planeDist = 0;
    
    float t;
    bool hit = intersectRayPlane(rayOrigin, rayDir, planeNormal, planeDist, &t);
    TEST(hit);
    TEST_FLOAT(t, 5.0f, 0.01f);
}

void test_segment_plane()
{
    printf("\n=== Segment-Plane ===\n");

    // Segment hitting plane
    Vec3 start = {-10, 0, 0};
    Vec3 end = {10, 0, 0};
    
    Vec3 planeNormal = {-1, 0, 0};
    float planeDist = 0;
    
    float frac = intersectRayPlaneFromPoints(start, end, planeNormal, planeDist);
    TEST_FLOAT(frac, 0.5f, 0.01f);  // hit at midpoint
}

int main()
{
    printf("=== Collision Tests ===\n");

    test_ray_aabb();
    test_ray_aabb_miss();
    test_segment_aabb();
    test_ray_plane();
    test_segment_plane();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);

    return g_failed > 0 ? 1 : 0;
}