// ============================================================
// FILE:    engine/entities/entity_list.cpp
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Flat pool of entities with O(1) create/destroy.
//          Uses generational indices to detect stale handles.
// DEPENDS:  entities/entity.h, entities/entity_id.h
// ============================================================

#include "engine/entities/entity_list.h"

#include <cstring>
#include <algorithm>

namespace nova
{

// -----------------------------------------------------------------------
// Constants
// -----------------------------------------------------------------------
static constexpr size_t kMaxEntities = 32768;  // 2^15, fits in 15-bit index

// -----------------------------------------------------------------------
// EntityList constructor
// -----------------------------------------------------------------------
EntityList::EntityList()
{
    m_entities.resize(kMaxEntities);
    m_active.resize(kMaxEntities, 0);
    m_freeList.resize(kMaxEntities);

    // Initialize free list
    for (int i = kMaxEntities - 1; i >= 0; --i)
    {
        m_freeList[i] = EntityID::make(static_cast<uint16_t>(i), 0);
    }
    m_freeCount = kMaxEntities;
}

// -----------------------------------------------------------------------
// create - allocate a new entity
// -----------------------------------------------------------------------
EntityHandle EntityList::create(const char *classname)
{
    if (m_freeCount == 0)
    {
        fprintf(stderr, "EntityList: no free entities\n");
        return EntityHandle();
    }

    // Pop from free list
    --m_freeCount;
    EntityID id = m_freeList[m_freeCount];

    // Get entity and initialize
    Entity& e = m_entities[id.index()];
    e = Entity{};

    // Set handle
    e.handle = EntityHandle(id);

    // Set classname
    if (classname)
    {
        std::strncpy(e.classname, classname, sizeof(e.classname) - 1);
    }

    // Mark as active
    m_active[id.index()] = 1;

    return EntityHandle(id);
}

// -----------------------------------------------------------------------
// destroy - free an entity
// -----------------------------------------------------------------------
void EntityList::destroy(EntityHandle handle)
{
    if (!handle.isValid())
        return;

    uint16_t idx = handle.index();

    // Verify valid index and generation match
    if (idx >= kMaxEntities || !m_active[idx])
        return;

    Entity& e = m_entities[handle.index()];
    if (e.handle.generation() != handle.generation())
        return;  // stale handle

    // Free it
    m_active[idx] = 0;

    // Increment generation for safety
    uint16_t newGen = e.handle.generation() + 1;
    if (newGen == 0) newGen = 1;  // avoid overflow to 0

    // Add back to free list
    m_freeList[m_freeCount] = EntityID::make(idx, newGen);
    ++m_freeCount;
}

// -----------------------------------------------------------------------
// get - get entity by handle (returns null if invalid)
// -----------------------------------------------------------------------
Entity *EntityList::get(EntityHandle handle)
{
    if (!handle.isValid())
        return nullptr;

    uint16_t idx = handle.index();
    if (idx >= kMaxEntities || !m_active[idx])
        return nullptr;

    Entity& e = m_entities[handle.index()];
    if (e.handle.generation() != handle.generation())
        return nullptr;  // stale

    return &e;
}

// -----------------------------------------------------------------------
// getRef - get reference to entity
// -----------------------------------------------------------------------
Entity& EntityList::getRef(EntityHandle handle)
{
    return m_entities[handle.index()];
}

// -----------------------------------------------------------------------
// getRef - get const reference
// -----------------------------------------------------------------------
Entity& EntityList::getRef(EntityID id)
{
    return m_entities[id.index()];
}

// -----------------------------------------------------------------------
// iterateActive - call func for each active entity
// -----------------------------------------------------------------------
void EntityList::iterateActive(IterateFn func)
{
    for (size_t i = 0; i < kMaxEntities; ++i)
    {
        if (m_active[i])
        {
            Entity& e = m_entities[i];
            func(e);
        }
    }
}

// -----------------------------------------------------------------------
// iterateByClassname - call func for entities matching classname
// -----------------------------------------------------------------------
void EntityList::iterateByClassname(const char *classname, IterateFn func)
{
    for (size_t i = 0; i < kMaxEntities; ++i)
    {
        if (m_active[i])
        {
            Entity& e = m_entities[i];
            if (std::strncmp(e.classname, classname, 32) == 0)
            {
                func(e);
            }
        }
    }
}

// -----------------------------------------------------------------------
// count - number of active entities
// -----------------------------------------------------------------------
size_t EntityList::count() const
{
    size_t c = 0;
    for (size_t i = 0; i < kMaxEntities; ++i)
        if (m_active[i]) ++c;
    return c;
}

// -----------------------------------------------------------------------
// findByClassname - first entity with matching classname
// -----------------------------------------------------------------------
EntityHandle EntityList::findByClassname(const char *classname)
{
    for (size_t i = 0; i < kMaxEntities; ++i)
    {
        if (m_active[i])
        {
            Entity& e = m_entities[i];
            if (std::strncmp(e.classname, classname, 32) == 0)
            {
                return EntityHandle(e.handle);
            }
        }
    }
    return EntityHandle();
}

// -----------------------------------------------------------------------
// findInAABB - all entities within bounding box
// -----------------------------------------------------------------------
void EntityList::findInAABB(const AABB& box, EntityHandle *out, int *outCount, int maxCount)
{
    int count = 0;
    for (size_t i = 0; i < kMaxEntities && count < maxCount; ++i)
    {
        if (m_active[i])
        {
            Entity& e = m_entities[i];
            AABB entBox = { e.origin + e.mins, e.origin + e.maxs };
            if (box.overlaps(entBox))
            {
                out[count++] = EntityHandle(e.handle);
            }
        }
    }
    *outCount = count;
}

} // namespace nova