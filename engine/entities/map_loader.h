// ============================================================
// FILE:    engine/entities/map_loader.h
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Parse the BSP entity lump and spawn all game entities.
//          Handles entity linking (target → targetname) post-spawn.
// DEPENDS: entities/entity_factory.h, renderer/bsp/bsp.h
// ============================================================
#pragma once

#include "engine/world/iworld.h"

namespace nova
{

// -----------------------------------------------------------------------
// ParsedEntity — temporary storage for one entity's key-value pairs
// during parsing. Stack-allocated, discarded after spawning.
// -----------------------------------------------------------------------
struct ParsedEntity
{
    static constexpr int kMaxPairs = 32;
    static constexpr int kMaxKeyLen  = 64;
    static constexpr int kMaxValLen  = 256;

    struct KV {
        char key[kMaxKeyLen];
        char val[kMaxValLen];
    };

    KV   pairs[kMaxPairs];
    int  count = 0;

    /// Find value for key; returns nullptr if not found.
    const char* get(const char* key) const;

    /// Add a key-value pair (silently drops if table full).
    void add(const char* key, const char* val);
};

// -----------------------------------------------------------------------
// MapLoader
// -----------------------------------------------------------------------
struct MapLoader
{
    /// Parse the entity lump from `world` and spawn all entities via
    /// EntityFactory. EntityFactory::init() must have been called first.
    /// Returns the number of entities spawned.
    static int load(IWorld* world);

private:
    /// Parse one `{ ... }` block and fill `out`. Returns the pointer
    /// just past the closing `}`, or nullptr on error / end-of-string.
    static const char* parseBlock(const char* src, ParsedEntity& out);

    /// Resolve target→targetname links after all entities are spawned.
    static void linkTargets();
};

} // namespace nova