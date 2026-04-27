// ============================================================
// FILE:    engine/entities/entity_list.h
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Flat pool of entities with O(1) create/destroy.
//          No heap allocation — static flat arrays only.
// DEPENDS:  entities/entity.h, entities/entity_id.h
// ============================================================

#pragma once

#include "engine/entities/entity.h"

#include <cstdio>
#include <cstdint>

namespace nova
{

// -----------------------------------------------------------------------
// EntityList - manages all game entities
// -----------------------------------------------------------------------
class EntityList
{
public:
    static constexpr size_t kMaxEntities = 1024;
    using IterateFn = void(*)(Entity&);

    EntityList();

    // ---- Lifecycle ----
    EntityHandle create(const char *classname);
    void destroy(EntityHandle handle);

    // ---- Access ----
    Entity *get(EntityHandle handle);
    Entity& getRef(EntityHandle handle);
    Entity& getRef(EntityID id);

    // ---- Iteration ----
    void iterateActive(IterateFn func);
    void iterateByClassname(const char *classname, IterateFn func);

    // ---- Per-frame update ----
    // Calls think() on every active STATE_ALIVE entity that has a non-null
    // think pointer. nextThink timing is deferred until a game clock exists.
    void think(float dt);

    // ---- Queries ----
    size_t count() const;
    EntityHandle findByClassname(const char *classname);
    void findInAABB(const AABB& box, EntityHandle *out, int *outCount, int maxCount);

private:
    Entity   m_entities[kMaxEntities];
    uint8_t  m_active[kMaxEntities];    // 1 = active, 0 = free
    EntityID m_freeList[kMaxEntities];  // stack of free slot IDs
    size_t   m_freeCount  = 0;
    size_t   m_activeCount = 0;
};

} // namespace nova
