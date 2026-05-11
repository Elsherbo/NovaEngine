// ============================================================
// FILE:    engine/entities/entity.h
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Base entity structure.
//          All game objects (players, items, doors) derive from this.
// DEPENDS:  core/math (Vec3, AABB), entities/entity_id.h,
//          entities/property_store.h
// ============================================================

#pragma once

#include "engine/core/math/vec.h"
#include "engine/core/math/shapes.h"
#include "engine/entities/entity_id.h"
#include "engine/entities/property_store.h"

namespace nova
{

// Forward declaration (defined in entity_class.h)
class EntityClass;

// -----------------------------------------------------------------------
// Entity flags (from Quake 2)
// -----------------------------------------------------------------------
enum EntityFlag : uint32_t
{
    FL_FLY      = 0x00000001,  // gravity doesn't affect fly mode
    FL_SWIM     = 0x00000002,  // unused
    FL_CLIENT   = 0x00000004,  // is a client
    FL_INWATER  = 0x00000008,  // entity is in water
    FL_NOTARGET= 0x00000010,  // other entities ignore this
    FL_GODMODE = 0x00000020,  // takes no damage
    FL_TEAMSLAVE= 0x00000040,  // is a team slave
    FL_KILLME  = 0x00000080,  // remove next frame
    FL_MOVECLIP = 0x00000100,  // has moveclip (physics)
    FL_TEAMMASTER=0x00000200,  // is a team master
};

// -----------------------------------------------------------------------
// Entity state
// -----------------------------------------------------------------------
enum EntityState : uint8_t
{
    STATE_FREE,       // available for reuse
    STATE_ALIVE,     // normal
    STATE_DEAD,      // dead, waiting for respawn
};

// -----------------------------------------------------------------------
// Entity link (for chaining entities together)
// -----------------------------------------------------------------------
struct EntityLink
{
    EntityHandle prev;
    EntityHandle next;
};

// -----------------------------------------------------------------------
// Base Entity struct
//   All game entities use this as a base.
// -----------------------------------------------------------------------
struct Entity
{
    // ---- Identification ----
    EntityHandle handle;       // my handle (for reference)
    char          classname[32];  // e.g., "player", "weaponrocket"
    char          model[32];     // e.g., "models/player.md2"

    // ---- Transform ----
    Vec3 origin;          // position in world
    Vec3 oldOrigin;       // last frame position (for interpolation)
    Vec3 velocity;       // current velocity
    Vec3 angles;       // pitch, yaw, roll
    Vec3 angularVel;   // for rotating objects

    // ---- Bounding box ----
    Vec3 mins;         // AABB min corner
    Vec3 maxs;         // AABB max corner

    // ---- Rendering ----
    int    modelIndex = 0;    // model index (0 = none)
    int    skin = 0;          // skin number
    char   animRequest[32];   // pending anim name set by game before model loaded

    // ---- Entity Links ----
    char   target[32];        // entity to trigger when activated
    char   targetname[32];    // name this entity is targeted by

    // ---- AI / Per-entity state (not shared across entities) ----
    float  aiThinkTimer = 0.0f;
    uint8_t aiState = 0;      // 0=idle, 1=chasing, etc. (game-defined)
    uint8_t carriedThisFrame = 0; // reset each think pass, prevents double-carry

    // ---- FuncPlat state (per-entity, avoids singleton bug) ----
    Vec3   platStartPos   = Vec3::zero();
    Vec3   platTargetPos  = Vec3::zero();
    float  platHeight     = 64.0f;
    float  platSpeed      = 100.0f;
    float  platWait       = 2.0f;
    float  platWaitTimer  = 1.0f;
    uint8_t platState     = 0;  // 0=IDLE_BOTTOM, 1=MOVING_UP, 2=WAITING_TOP, 3=MOVING_DOWN

    // ---- Physics ----
    float gravity = 1.0f;    // gravity multiplier
    float mass = 1.0f;       // for pushable objects

    // ---- Flags ----
    uint32_t flags = 0;

    // ---- State ----
    EntityState state = STATE_FREE;

    // ---- Team ----
    EntityHandle teamMaster;   // for doors/buttons
    EntityLink link;        // chain of linked entities

    // ---- Entity Class (virtual dispatch hooks) ----
    class EntityClass* entityClass = nullptr;

    // ---- Callbacks ----
    // These are function pointers set by game code
    using ThinkFn    = void(*)(Entity*, float dt);
    using TouchFn   = void(*)(Entity*, Entity*);
    using UseFn     = void(*)(Entity*, Entity*);
    using BlockFn  = void(*)(Entity*, Entity*);
    using PainFn   = void(*)(Entity*, Entity*, float, int);
    using DieFn    = void(*)(Entity*, Entity*, float);

    ThinkFn  think = nullptr;
    TouchFn  touch = nullptr;
    UseFn    use = nullptr;
    BlockFn  block = nullptr;
    PainFn   pain = nullptr;
    DieFn    die = nullptr;

    // ---- Timing ----
    float nextThink = 0.0f;   // game time to call think()

    // ---- Damage ----
    float health = 0.0f;
    float maxHealth = 0.0f;
    int   deadflag = 0;

    // ---- Links ----
    EntityLink area;             // for spatial partitioning
    EntityHandle groundEntity; // entity standing on

    // ---- Client ----
    // These fields are only for player entities
    EntityHandle client = EntityHandle();  // client index for players
    int    ping = 0;
    int    playerNum = 0;

    // ---- Property access (custom keys from TrenchBroom) ----
    const char* getProperty(const std::string& key) const
    {
        return g_propertyStore.get(handle.index(), key);
    }

    void setProperty(const std::string& key, const std::string& val)
    {
        g_propertyStore.set(handle.index(), key, val);
    }

    bool hasProperty(const std::string& key) const
    {
        return g_propertyStore.has(handle.index(), key);
    }
};

// -----------------------------------------------------------------------
// Helper functions
// -----------------------------------------------------------------------
inline bool isAlive(const Entity& e) { return e.state == STATE_ALIVE; }
inline bool isDead(const Entity& e) { return e.state == STATE_DEAD; }
inline bool isFlyable(const Entity& e) { return e.flags & FL_FLY; }

inline Vec3 getCenter(const Entity& e)
{
    return { (e.mins.x + e.maxs.x) * 0.5f + e.origin.x,
             (e.mins.y + e.maxs.y) * 0.5f + e.origin.y,
             (e.mins.z + e.maxs.z) * 0.5f + e.origin.z };
}

inline AABB getAABB(const Entity& e)
{
    return { e.origin + e.mins, e.origin + e.maxs };
}

// ABI safety: engine and game DLL must agree on Entity layout.
// If this fires, both binaries need recompilation from the same header.
// Expected size on MSVC x64 with alignas(16) Vec3 = 480 bytes.
static_assert(sizeof(Entity) == 480, "Entity struct size changed — verify ABI compatibility between engine and game DLL");

} // namespace nova