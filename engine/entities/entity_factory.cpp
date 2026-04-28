// ============================================================
// FILE:    engine/entities/entity_factory.cpp
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Entity spawn registry + built-in spawn functions.
// DEPENDS: entities/entity_factory.h, entities/entity_list.h
// ============================================================

#include "engine/entities/entity_factory.h"
#include "engine/entities/entity_list.h"   // provides g_entityList

#include <cstring>   // strcmp, strncpy
#include <cstdio>    // fprintf, sscanf
#include <cstdlib>   // strtol, strtof

namespace nova
{

// ============================================================
// Static storage
// ============================================================

EntityFactory::Entry EntityFactory::s_table[kMaxClasses];
int                  EntityFactory::s_count = 0;

// ============================================================
// Registration
// ============================================================

void EntityFactory::registerClass(const char* classname, SpawnFn fn)
{
    // Overwrite existing entry (idempotent re-init).
    for (int i = 0; i < s_count; ++i)
    {
        if (std::strcmp(s_table[i].classname, classname) == 0)
        {
            s_table[i].fn = fn;
            return;
        }
    }
    if (s_count >= kMaxClasses)
    {
        fprintf(stderr, "EntityFactory: table full — increase kMaxClasses\n");
        return;
    }
    s_table[s_count++] = { classname, fn };
}

// ============================================================
// Lookup
// ============================================================

void (*EntityFactory::lookup(const char* classname))(Entity*)
{
    for (int i = 0; i < s_count; ++i)
        if (std::strcmp(s_table[i].classname, classname) == 0)
            return s_table[i].fn;
    return nullptr;
}

// ============================================================
// Spawn
// ============================================================

Entity* EntityFactory::spawn(const char* classname, Vec3 origin)
{
    EntityHandle handle = g_entityList.create(classname);
    if (!handle.isValid()) return nullptr;

    Entity* ent = g_entityList.get(handle);
    if (!ent) return nullptr;

    ent->origin = origin;

    SpawnFn fn = lookup(classname);
    if (fn) fn(ent);

    return ent;
}

// ============================================================
// Parsing helpers
// ============================================================

Vec3 EntityFactory::parseOrigin(const char* str)
{
    Vec3 v{0.f, 0.f, 0.f};
    if (!str || !*str) return v;
    sscanf(str, "%f %f %f", &v.x, &v.y, &v.z);
    return v;
}

Vec3 EntityFactory::parseAngles(const char* str)
{
    // Q2 uses either a single yaw value (key "angle") or
    // "pitch yaw roll" (key "angles"). Detect by counting spaces.
    Vec3 v{0.f, 0.f, 0.f};
    if (!str || !*str) return v;

    int spaces = 0;
    for (const char* p = str; *p; ++p)
        if (*p == ' ') ++spaces;

    if (spaces >= 2)
        sscanf(str, "%f %f %f", &v.x, &v.y, &v.z);  // pitch yaw roll
    else
        sscanf(str, "%f", &v.y);                       // yaw only

    return v;
}

int EntityFactory::parseInt(const char* str)
{
    if (!str || !*str) return 0;
    return (int)std::strtol(str, nullptr, 10);
}

float EntityFactory::parseFloat(const char* str)
{
    if (!str || !*str) return 0.f;
    return std::strtof(str, nullptr);
}

// ============================================================
// Built-in spawn functions
// ============================================================

static void spawn_worldspawn(Entity* ent)
{
    ent->flags |= FL_NOTARGET;
}

static void spawn_info_player_start(Entity* ent)
{
    ent->flags |= FL_NOTARGET;
    ent->mins = Vec3{0.f, 0.f, 0.f};
    ent->maxs = Vec3{0.f, 0.f, 0.f};
}

static void spawn_info_player_deathmatch(Entity* ent)
{
    ent->flags |= FL_NOTARGET;
    ent->mins = Vec3{0.f, 0.f, 0.f};
    ent->maxs = Vec3{0.f, 0.f, 0.f};
}

static void spawn_func_door(Entity* ent)
{
    ent->mins = Vec3{-16.f, -4.f, -16.f};
    ent->maxs = Vec3{ 16.f,  4.f,  16.f};
    ent->health = 0.f;
}

static void spawn_func_button(Entity* ent)
{
    ent->mins = Vec3{-8.f, -8.f, -8.f};
    ent->maxs = Vec3{ 8.f,  8.f,  8.f};
}

static void spawn_trigger_multiple(Entity* ent)
{
    ent->flags |= FL_NOTARGET;
    ent->mins = Vec3{-16.f, -16.f, -16.f};
    ent->maxs = Vec3{ 16.f,  16.f,  16.f};
}

static void spawn_trigger_push(Entity* ent)
{
    ent->flags |= FL_NOTARGET;
    ent->mins = Vec3{-16.f, -16.f, -16.f};
    ent->maxs = Vec3{ 16.f,  16.f,  16.f};
}

static void spawn_item_health(Entity* ent)
{
    ent->health    = 25.f;
    ent->maxHealth = 25.f;
    ent->mins      = Vec3{-16.f, -16.f, -16.f};
    ent->maxs      = Vec3{ 16.f,  16.f,  16.f};
}

static void spawn_item_shells(Entity* ent)
{
    ent->mins = Vec3{-16.f, -16.f, -16.f};
    ent->maxs = Vec3{ 16.f,  16.f,  16.f};
}

static void spawn_weapon_blaster(Entity* ent)
{
    ent->mins = Vec3{-16.f, -16.f, -16.f};
    ent->maxs = Vec3{ 16.f,  16.f,  16.f};
}

// ============================================================
// init — register all built-in types
// ============================================================

void EntityFactory::init()
{
    s_count = 0; // safe re-init

    registerClass("worldspawn",              spawn_worldspawn);
    registerClass("info_player_start",       spawn_info_player_start);
    registerClass("info_player_deathmatch",  spawn_info_player_deathmatch);
    registerClass("func_door",               spawn_func_door);
    registerClass("func_button",             spawn_func_button);
    registerClass("trigger_multiple",        spawn_trigger_multiple);
    registerClass("trigger_push",            spawn_trigger_push);
    registerClass("item_health",             spawn_item_health);
    registerClass("item_shells",             spawn_item_shells);
    registerClass("weapon_blaster",          spawn_weapon_blaster);
}

} // namespace nova
