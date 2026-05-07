// ============================================================
// FILE:    engine/entities/entity_list.cpp
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Flat pool of entities with O(1) create/destroy.
//          Uses generational indices to detect stale handles.
//          No heap allocation — static flat arrays only.
// DEPENDS:  entities/entity.h, entities/entity_id.h
// ============================================================

#include "engine/entities/entity_list.h"

#include <cstring>
#include <algorithm>
#include <cassert>

namespace nova
{

// Global entity list — used by EntityFactory, MapLoader, engine
EntityList g_entityList;

// -----------------------------------------------------------------------
// EntityList constructor
// -----------------------------------------------------------------------
EntityList::EntityList()
{
    // Zero all active flags
    for (size_t i = 0; i < kMaxEntities; ++i)
        m_active[i] = 0;

    // Pre-populate free list so that create() pops index 0 FIRST.
    //
    // FIX: The old code filled m_freeList[i] = EntityID(i, 0) and then
    // create() did --m_freeCount and popped m_freeList[m_freeCount].
    // Because m_freeCount starts at kMaxEntities, the FIRST pop gave
    // m_freeList[kMaxEntities-1] = EntityID(1023, 0) — index 1023.
    //
    // AABBPhysics::setEntityStorage is called with count=1, so
    // isValidEntityIndex(handle_1023, 1) = (1023 < 1) = false.
    // Every physics call on the player entity silently returned Vec3{0,0,0},
    // teleporting the player to the world origin every frame.
    //
    // Fix: fill in REVERSE order so that m_freeList[kMaxEntities-1] = EntityID(0,0).
    // First pop → m_freeList[kMaxEntities-1] = EntityID(0,0). Index 0. ✓
    for (size_t i = 0; i < kMaxEntities; ++i)
        m_freeList[i] = EntityID::make(static_cast<uint16_t>(kMaxEntities - 1 - i), 0);

    m_freeCount  = kMaxEntities;
    m_activeCount = 0;
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

    // Get entity slot and zero-initialise it
    Entity& e = m_entities[id.index()];
    e = Entity{};

    // Set handle
    e.handle = EntityHandle(id);

    // Set classname with guaranteed null terminator
    if (classname)
    {
        std::strncpy(e.classname, classname, sizeof(e.classname) - 1);
        e.classname[sizeof(e.classname) - 1] = '\0';
    }

    // Mark entity alive (Problem 3d)
    e.state = STATE_ALIVE;

    // Mark slot active
    m_active[id.index()] = 1;
    ++m_activeCount;

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

    // Verify valid index and active
    if (idx >= kMaxEntities || !m_active[idx])
        return;

    Entity& e = m_entities[idx];
    if (e.handle.generation() != handle.generation())
        return;  // stale handle

    // Free it — mark state and deactivate slot (Problem 3c)
    e.state = STATE_FREE;
    m_active[idx] = 0;
    --m_activeCount;

    // Increment generation for safety
    uint16_t newGen = e.handle.generation() + 1;
    if (newGen == 0) newGen = 1;  // avoid overflow to 0

    // Push back to free list
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

    Entity& e = m_entities[idx];
    if (e.handle.generation() != handle.generation())
        return nullptr;  // stale

    return &e;
}

// -----------------------------------------------------------------------
// getRef - get reference to entity (asserts valid)
// -----------------------------------------------------------------------
Entity& EntityList::getRef(EntityHandle handle)
{
    Entity *e = get(handle);
    assert(e && "getRef: invalid handle");
    return *e;
}

// -----------------------------------------------------------------------
// getRef - get reference by ID (asserts valid)
// -----------------------------------------------------------------------
Entity& EntityList::getRef(EntityID id)
{
    assert(id.isValid() && "getRef: invalid EntityID");

    uint16_t idx = id.index();
    assert(idx < kMaxEntities && m_active[idx] && "getRef: invalid index");

    return m_entities[idx];
}

// -----------------------------------------------------------------------
// iterateActive - call func for each active entity
// -----------------------------------------------------------------------
void EntityList::iterateActive(IterateFn func)
{
    for (size_t i = 0; i < kMaxEntities; ++i)
    {
        if (m_active[i])
            func(m_entities[i]);
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
                func(e);
        }
    }
}

// -----------------------------------------------------------------------
// think - per-frame think dispatch (Problem 5)
// -----------------------------------------------------------------------
void EntityList::think(float dt)
{
    for (size_t i = 0; i < kMaxEntities; ++i)
    {
        if (!m_active[i])
            continue;

        Entity& e = m_entities[i];
        if (e.state == STATE_ALIVE && e.think != nullptr)
            e.think(&e, dt);
    }
}

// -----------------------------------------------------------------------
// count - number of active entities
// -----------------------------------------------------------------------
size_t EntityList::count() const
{
    return m_activeCount;
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
                return EntityHandle(e.handle);
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
                out[count++] = EntityHandle(e.handle);
        }
    }
    *outCount = count;
}

// -----------------------------------------------------------------------
// Direct index access (for model spawner)
// -----------------------------------------------------------------------
Entity* EntityList::getByIndex(int index)
{
    if (index < 0 || index >= (int)kMaxEntities || !m_active[index])
        return nullptr;
    return &m_entities[index];
}

const Entity* EntityList::getByIndex(int index) const
{
    if (index < 0 || index >= (int)kMaxEntities || !m_active[index])
        return nullptr;
    return &m_entities[index];
}

bool EntityList::isActiveIndex(int index) const
{
    return index >= 0 && index < (int)kMaxEntities && m_active[index] != 0;
}

} // namespace nova
