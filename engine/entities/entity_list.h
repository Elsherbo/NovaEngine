// ============================================================
// FILE:    engine/entities/entity_list.h
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Flat pool of entities with O(1) create/destroy.
// DEPENDS:  entities/entity.h, entities/entity_id.h
// ============================================================

#pragma once

#include "engine/entities/entity.h"

#include <vector>
#include <cstdio>

namespace nova
{

// -----------------------------------------------------------------------
// EntityList - manages all game entities
// -----------------------------------------------------------------------
class EntityList
{
public:
    static constexpr size_t kMaxEntities = 32768;
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

    // ---- Queries ----
    size_t count() const;
    EntityHandle findByClassname(const char *classname);
    void findInAABB(const AABB& box, EntityHandle *out, int *outCount, int maxCount);

private:
    std::vector<Entity>    m_entities;
    std::vector<char>      m_active;
    std::vector<EntityID>  m_freeList;
    size_t                 m_freeCount = 0;
    size_t                 m_activeCount = 0;
};

} // namespace nova