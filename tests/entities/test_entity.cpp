// ============================================================
// FILE:    tests/entities/test_entity.cpp
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Test entity system
// ============================================================

#include <cstdio>
#include <cassert>
#include <cstring>

#include "engine/entities/entity_id.h"
#include "engine/entities/entity.h"
#include "engine/entities/entity_list.h"

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

void test_entity_id()
{
    printf("\n=== EntityID ===\n");

    // Basic operations
    EntityID id = EntityID::make(5, 1);
    TEST(id.index() == 5);
    TEST(id.generation() == 1);
    TEST(!id.isInvalid());
    TEST(id.isValid());

    // Invalid
    EntityID invalid;
    TEST(invalid.isInvalid());
    TEST(!invalid.isValid());

    // Comparison
    EntityID id2 = EntityID::make(5, 1);
    TEST(id == id2);

    EntityID id3 = EntityID::make(5, 2);
    TEST(id != id3);
}

void test_entity_handle()
{
    printf("\n=== EntityHandle ===\n");

    EntityHandle h;
    TEST(h.isNull());
    TEST(!h.isValid());

    EntityID id = EntityID::make(10, 3);
    EntityHandle h2(id);
    TEST(h2.isValid());
    TEST(!h2.isNull());
    TEST(h2.index() == 10);
    TEST(h2.generation() == 3);
}

void test_entity_list()
{
    printf("\n=== EntityList ===\n");

    EntityList list;

    // Initial count is 0
    TEST(list.count() == 0);

    // Create entities
    auto e1 = list.create("player");
    TEST(e1.isValid());
    TEST(list.count() == 1);

    auto e2 = list.create("weapon");
    TEST(e2.isValid());
    TEST(list.count() == 2);

    // Access
    Entity* ent = list.get(e1);
    TEST(ent != nullptr);
    TEST(ent->handle == e1);

    // Invalid access
    EntityHandle bad;
    TEST(list.get(bad) == nullptr);

    // Classname
    TEST(::strncmp(ent->classname, "player", 32) == 0);

    // Destroy
    list.destroy(e1);
    TEST(list.count() == 1);

    // After destroy, handle should be invalid
    ent = list.get(e1);
    TEST(ent == nullptr);  // stale handle

    // Create again - new generation
    auto e3 = list.create("player");
    TEST(e3.generation() != e1.generation());

    // Find by classname
    auto found = list.findByClassname("weapon");
    TEST(found.isValid());

    found = list.findByClassname("nonexistent");
    TEST(!found.isValid());
}

void test_entity_structure()
{
    printf("\n=== Entity ===\n");

    Entity e;
    TEST(e.state == STATE_FREE);
    TEST(e.health == 0.0f);

    e.origin = {100, 200, 300};
    TEST(e.origin.x == 100);
    TEST(e.origin.y == 200);
    TEST(e.origin.z == 300);

    e.velocity = {1, 2, 3};
    TEST(e.velocity.x == 1);

    e.flags |= FL_FLY;
    TEST(e.flags & FL_FLY);
    TEST(!(e.flags & FL_SWIM));
}

int main()
{
    printf("=== Entity System Tests ===\n");

    test_entity_id();
    test_entity_handle();
    test_entity_list();
    test_entity_structure();

    printf("\n=== Results ===\n");
    printf("Passed: %d\n", g_passed);
    printf("Failed: %d\n", g_failed);

    return g_failed > 0 ? 1 : 0;
}