// ============================================================
// FILE:    engine/entities/entity_class.h
// MODULE:  Entities
// PHASE:   2
// PURPOSE: Class-based entity system.
//          Allows game code to define entity types with virtual
//          lifecycle hooks (onSpawn, onThink, onDie, etc.)
//          while keeping the flat EntityList array intact.
// DEPENDS:  entities/entity.h
// ============================================================
#pragma once

#include "engine/entities/entity.h"

#include <cstdint>

namespace nova
{

// -----------------------------------------------------------------------
// EntityClass - base class for entity behavior types.
// One instance per class (not per entity). Registered at init time.
// -----------------------------------------------------------------------
class EntityClass
{
public:
    virtual ~EntityClass() = default;

    // Must match the classname string used in TrenchBroom/map files.
    virtual const char* classname() const = 0;

    // Called once when the entity is spawned from the map or created at runtime.
    virtual void onSpawn(Entity* e) { (void)e; }

    // Called every frame (only on STATE_ALIVE entities).
    virtual void onThink(Entity* e, float dt) { (void)e; (void)dt; }

    // Called when the entity takes damage.
    virtual void onPain(Entity* e, Entity* inflictor, Entity* attacker, float damage, int dflags)
    { (void)e; (void)inflictor; (void)attacker; (void)damage; (void)dflags; }

    // Called when the entity dies.
    virtual void onDie(Entity* e, Entity* inflictor, Entity* attacker, float damage)
    { (void)e; (void)inflictor; (void)attacker; (void)damage; }

    // Called when another entity touches this one (collision/trigger).
    virtual void onTouch(Entity* e, Entity* other) { (void)e; (void)other; }

    // Called when a button/door is used by a player.
    virtual void onUse(Entity* e, Entity* other) { (void)e; (void)other; }
};

// -----------------------------------------------------------------------
// EntityClassRegistry - maps classnames to EntityClass instances.
// -----------------------------------------------------------------------
class EntityClassRegistry
{
public:
    static constexpr size_t kMaxClasses = 256;

    void registerClass(EntityClass* cls);
    EntityClass* find(const char* classname) const;

private:
    EntityClass* m_classes[kMaxClasses] = {};
    size_t       m_count = 0;
};

// Global registry
extern EntityClassRegistry g_entityClasses;

} // namespace nova
