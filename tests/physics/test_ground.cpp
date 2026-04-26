// ============================================================
// FILE:    tests/physics/test_ground.cpp
// MODULE:  Physics
// PHASE:   2
// STATUS:  UPDATED
// PURPOSE: Test ground detection and gravity integration.
//          Tests isOnGround(), getGroundElevation(), and
//          gravity behavior without a BSP loaded.
// ============================================================

#include <cstdio>
#include <cassert>
#include <cmath>

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
        printf("  FAIL: %s  (line %d)\n", #expr, __LINE__); \
        ++g_failed; \
    } \
} while(0)

#define TEST_FLOAT(val, expected, tol) do { \
    float _v = (val); float _e = (expected); \
    if (std::abs(_v - _e) < (tol)) { \
        printf("  PASS: %s  (%.4f)\n", #val, _v); \
        ++g_passed; \
    } else { \
        printf("  FAIL: %s  got %.4f  expected %.4f  (line %d)\n", #val, _v, _e, __LINE__); \
        ++g_failed; \
    } \
} while(0)

// -----------------------------------------------------------------------
void test_no_bsp_always_airborne()
{
    printf("\n=== isOnGround: no BSP → always false ===\n");

    EntityList list;
    AABBPhysics physics;
    // No BSP loaded → every trace returns fraction=1.0 → not on ground

    auto player = list.create("player");
    TEST(player.isValid());

    Vec3 origin   = {100, 50, 100};
    Vec3 velocity = {0, 0, 0};
    list.getRef(player).origin   = origin;
    list.getRef(player).velocity = velocity;

    physics.setEntityStorage(
        &list.getRef(player).origin,
        &list.getRef(player).velocity,
        1
    );

    bool onGround = physics.isOnGround(player);
    TEST(!onGround);   // no BSP → no ground hit
}

// -----------------------------------------------------------------------
void test_no_bsp_ground_elevation()
{
    printf("\n=== getGroundElevation: no BSP → far below ===\n");

    EntityList list;
    AABBPhysics physics;

    auto player = list.create("player");
    Vec3 origin   = {100, 50, 100};
    Vec3 velocity = {0, 0, 0};
    list.getRef(player).origin   = origin;
    list.getRef(player).velocity = velocity;

    physics.setEntityStorage(
        &list.getRef(player).origin,
        &list.getRef(player).velocity,
        1
    );

    float elev = physics.getGroundElevation(player);
    printf("  Elevation (no BSP): %.1f  (origin.y = %.1f)\n", elev, origin.y);

    // With no BSP, trace returns fraction=1 → elevation = origin.y - 400
    TEST(elev < origin.y);
    TEST(elev <= origin.y - 390.0f);   // should be near 400 below
}

// -----------------------------------------------------------------------
void test_gravity_value()
{
    printf("\n=== gravity default / setGravity ===\n");

    AABBPhysics physics;

    // Default should match Q2: 800
    TEST_FLOAT(physics.getGravity(), 800.0f, 0.1f);

    physics.setGravity(600.0f);
    TEST_FLOAT(physics.getGravity(), 600.0f, 0.1f);

    physics.setGravity(0.0f);
    TEST_FLOAT(physics.getGravity(), 0.0f, 0.1f);
}

// -----------------------------------------------------------------------
void test_player_bounds()
{
    printf("\n=== player bounds (Y-up GL space) ===\n");

    AABBPhysics physics;

    // Default bounds: Y-up GL space, Y is vertical
    // mins.y = -36 (feet below origin), maxs.y = +36 (head above origin)
    physics.setPlayerBounds({-16, -36, -16}, {16, 36, 16});
    // Just verifying it doesn't crash
    TEST(true);
}

// -----------------------------------------------------------------------
void test_trace_no_bsp_returns_full_fraction()
{
    printf("\n=== trace: no BSP → fraction = 1.0 ===\n");

    AABBPhysics physics;
    // No BSP → traceWorld returns fraction=1

    Vec3 start = {0, 100, 0};
    Vec3 end   = {0, 0, 0};
    Vec3 mins  = {-16, -36, -16};
    Vec3 maxs  = { 16,  36,  16};

    TraceResult tr = physics.trace(start, end, mins, maxs);
    TEST_FLOAT(tr.fraction, 1.0f, 0.001f);

    // endPos should be the end point when fraction = 1
    float dx = std::abs(tr.endPos.x - end.x);
    float dy = std::abs(tr.endPos.y - end.y);
    float dz = std::abs(tr.endPos.z - end.z);
    TEST(dx < 0.01f && dy < 0.01f && dz < 0.01f);
}

// -----------------------------------------------------------------------
void test_entity_origin_velocity_setget()
{
    printf("\n=== entity origin/velocity set/get ===\n");

    EntityList list;
    AABBPhysics physics;

    auto e = list.create("test");
    Vec3 origin   = {10, 20, 30};
    Vec3 velocity = {1, 2, 3};
    list.getRef(e).origin   = origin;
    list.getRef(e).velocity = velocity;

    physics.setEntityStorage(
        &list.getRef(e).origin,
        &list.getRef(e).velocity,
        1
    );

    physics.setOrigin(e, {100, 200, 300});
    Vec3 got = physics.getOrigin(e);
    TEST_FLOAT(got.x, 100.0f, 0.01f);
    TEST_FLOAT(got.y, 200.0f, 0.01f);
    TEST_FLOAT(got.z, 300.0f, 0.01f);

    physics.setVelocity(e, {5, -10, 3});
    Vec3 vel = physics.getVelocity(e);
    TEST_FLOAT(vel.x,  5.0f, 0.01f);
    TEST_FLOAT(vel.y, -10.0f, 0.01f);
    TEST_FLOAT(vel.z,  3.0f, 0.01f);
}

// -----------------------------------------------------------------------
int main()
{
    printf("=== Ground Detection Tests ===\n");

    test_no_bsp_always_airborne();
    test_no_bsp_ground_elevation();
    test_gravity_value();
    test_player_bounds();
    test_trace_no_bsp_returns_full_fraction();
    test_entity_origin_velocity_setget();

    printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed > 0 ? 1 : 0;
}
