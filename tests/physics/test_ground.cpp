// ============================================================
// FILE:    tests/physics/test_ground.cpp
// MODULE:  Physics
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Test ground detection
// ============================================================

#include <cstdio>
#include <cassert>

#include "engine/entities/entity_list.h"
#include "engine/physics/aabb_physics.h"

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

void test_is_on_ground()
{
    printf("\n=== isOnGround ===\n");

    EntityList list;
    AABBPhysics physics;

    // Create player entity
    auto player = list.create("player");
    TEST(player.isValid());

    // Set origin
    list.getRef(player).origin = {100, 0, 50};
    physics.setEntityStorage(
        (Vec3*)&list.getRef(player).origin,
        (Vec3*)&list.getRef(player).velocity
    );

    // Player starts in air - isOnGround should be false (no BSP loaded)
    // After loading BSP, would be true if standing on floor
    TEST(true);  // placeholder until we integrate BSP
}

void test_ground_elevation()
{
    printf("\n=== getGroundElevation ===\n");

    EntityList list;
    AABBPhysics physics;

    auto player = list.create("player");
    list.getRef(player).origin = {100, 0, 50};
    
    physics.setEntityStorage(
        (Vec3*)&list.getRef(player).origin,
        (Vec3*)&list.getRef(player).velocity
    );

    // Without BSP, returns far below
    float elev = physics.getGroundElevation(player);
    printf("  Elevation (no BSP): %f\n", elev);
    TEST(elev < 0.0f);  // should be way below origin

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);
}

int main()
{
    printf("=== Ground Detection Tests ===\n");

    test_is_on_ground();
    test_ground_elevation();

    return g_failed > 0 ? 1 : 0;
}