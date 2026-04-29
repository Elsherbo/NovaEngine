// ============================================================
// FILE:    engine/entities/entity_factory.h
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Registry mapping classname strings to spawn functions.
//          Provides helpers for parsing BSP entity key-value pairs.
// DEPENDS: entities/entity.h, entities/entity_list.h
// ============================================================
#pragma once

#include "engine/entities/entity.h"

namespace nova
{

// -----------------------------------------------------------------------
// SpawnFn — called after the entity is created and origin/angles are set.
// The function fills in any class-specific fields (mins/maxs, callbacks…).
// -----------------------------------------------------------------------
using SpawnFn = void (*)(Entity* ent);

// -----------------------------------------------------------------------
// EntityFactory
// Static registry — no heap allocation, fixed-capacity table.
// Call EntityFactory::init() once at startup to register all built-in types.
// -----------------------------------------------------------------------
struct EntityFactory
{
    // ---- Registration --------------------------------------------------

    /// Register a spawn function for a classname (string-literal lifetime).
    static void registerClass(const char* classname, SpawnFn fn);

    // ---- Spawning ------------------------------------------------------

    /// Allocate an entity in g_entityList, set origin/classname, run SpawnFn.
    /// Returns nullptr if the pool is full.
    static Entity* spawn(const char* classname, Vec3 origin);

    // ---- Lifecycle -----------------------------------------------------

    /// Register all built-in entity types. Call before MapLoader::load().
    static void init();

    /// Returns the number of registered classes (useful for diagnostics).
    static int classCount() { return s_count; }

    // ---- Parsing helpers -----------------------------------------------
    // Pure functions — no side-effects, usable anywhere.

    /// Parse "x y z" → Vec3.
    static Vec3  parseOrigin(const char* str);

    /// Parse "yaw" OR "pitch yaw roll" → Vec3 angles.
    static Vec3  parseAngles(const char* str);

    /// Parse integer string.
    static int   parseInt(const char* str);

    /// Parse float string.
    static float parseFloat(const char* str);

private:
    static constexpr int kMaxClasses = 64;

    struct Entry {
        const char* classname;
        SpawnFn     fn;
    };

    static Entry s_table[kMaxClasses];
    static int   s_count;

    static void (*lookup(const char* classname))(Entity*);
};

} // namespace nova
