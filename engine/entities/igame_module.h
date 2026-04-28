// ============================================================
// FILE:    engine/entities/igame_module.h
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Contract between engine and game DLL.
//          Defines how engine runs game logic without linking to it.
// DEPENDS:  core/math (Vec3), entities/entity_id.h
// ============================================================

#pragma once

#include "engine/core/math/vec.h"
#include "engine/entities/entity_id.h"

namespace nova
{

// -----------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------
class IPhysicsWorld;
class IAudioSystem;
class INetworkSystem;
struct IPlatform;
class BSPMap;   // NEW: forward so IGameModule::loadMap can accept BSPMap*

// -----------------------------------------------------------------------
// IGameModule - pure virtual interface
// Any game DLL implements this to add gameplay to the engine.
// -----------------------------------------------------------------------
class IGameModule
{
public:
    virtual ~IGameModule() = default;

    // ---- Lifecycle ----
    virtual bool init() = 0;
    virtual void shutdown() = 0;

    // ---- Map loading (NEW) ----
    // Called by the engine after BSP::load() succeeds.
    // Implementations call EntityFactory::init() then MapLoader::load(map).
    virtual void loadMap(const BSPMap* map) = 0;

    // ---- Per-frame updates ----
    virtual void think(float dt) = 0;

    // ---- Entity events ----
    virtual void onEntitySpawn(EntityHandle handle) = 0;
    virtual void onEntityDestroy(EntityHandle handle) = 0;
    virtual void onEntityTouch(EntityHandle handle, EntityHandle other) = 0;

    // ---- Physics ----
    virtual void setPhysicsWorld(IPhysicsWorld *physics) = 0;
    virtual IPhysicsWorld *getPhysicsWorld() = 0;

    // ---- Audio ----
    virtual void setAudioSystem(IAudioSystem *audio) = 0;
    virtual IAudioSystem *getAudioSystem() = 0;

    // ---- Networking ----
    virtual void setNetworkSystem(INetworkSystem *net) = 0;
    virtual INetworkSystem *getNetworkSystem() = 0;
};

// -----------------------------------------------------------------------
// IGameModule getter - implemented by game DLL
// -----------------------------------------------------------------------
extern "C" typedef IGameModule *(*PFN_GetGameModule)();

} // namespace nova