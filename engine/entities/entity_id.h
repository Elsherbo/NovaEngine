// ============================================================
// FILE:    engine/entities/entity_id.h
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Generational index for safe entity handles.
//          Prevents use-after-free when entities are destroyed.
// DEPENDS:  none
// ============================================================

#pragma once

#include <cstdint>

namespace nova
{

// -----------------------------------------------------------------------
// EntityID - 32-bit generational index
//   [15 bits index] [1 bit valid] [16 bits generation]
// -----------------------------------------------------------------------
struct EntityID
{
    uint32_t bits = kInvalid;

    static constexpr size_t kIndexBits    = 15;
    static constexpr size_t kGenBits    = 16;
    static constexpr uint32_t kInvalid = 0xFFFFFFFF;

    bool isValid() const   { return bits != kInvalid; }
    bool isInvalid() const { return bits == kInvalid; }

    uint16_t index() const
    {
        return static_cast<uint16_t>(bits & 0x7FFF);
    }

    uint16_t generation() const
    {
        return static_cast<uint16_t>((bits >> 16) & 0xFFFF);
    }

    static EntityID make(uint16_t idx, uint16_t gen)
    {
        EntityID id;
        id.bits = (static_cast<uint32_t>(gen) << 16) | idx;
        return id;
    }

    bool operator==(const EntityID& o) const { return bits == o.bits; }
    bool operator!=(const EntityID& o) const { return bits != o.bits; }
};

// -----------------------------------------------------------------------
// EntityHandle - opaque reference to an entity
//   Always use this in game code, not raw indices.
// -----------------------------------------------------------------------
class EntityList;  // forward declaration

class EntityHandle
{
    friend class EntityList;

    EntityID m_id = EntityID{EntityID::kInvalid};

public:
    EntityHandle() { m_id.bits = EntityID::kInvalid; }
    explicit EntityHandle(EntityID id) : m_id(id) {}

    bool isValid() const { return m_id.isValid(); }
    bool isNull() const { return !m_id.isValid(); }

    uint16_t index() const { return m_id.index(); }
    uint16_t generation() const { return m_id.generation(); }

    bool operator==(const EntityHandle& o) const { return m_id.bits == o.m_id.bits; }
    bool operator!=(const EntityHandle& o) const { return m_id.bits != o.m_id.bits; }
};

// -----------------------------------------------------------------------
// EntityRef - reference to entity data (valid only for current frame)
//   Use this for temporary access, not storage.
// -----------------------------------------------------------------------
struct Entity;  // forward declaration

class EntityRef
{
    Entity* m_entity = nullptr;
    EntityHandle m_handle;

public:
    EntityRef() : m_entity(nullptr), m_handle() {}
    EntityRef(Entity* e, EntityHandle h) : m_entity(e), m_handle(h) {}

    bool isValid() const { return m_entity != nullptr; }
    bool isNull() const { return m_entity == nullptr; }

    Entity* operator->() { return m_entity; }
    Entity& operator*() { return *m_entity; }

    const EntityHandle& handle() const { return m_handle; }
};

} // namespace nova