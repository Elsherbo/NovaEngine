// ============================================================
// FILE:    game/src/game_module.cpp
// MODULE:  Game
// PURPOSE: GameModule implementation - retro boomer shooter base
// ============================================================

#include "game_module.h"

#include "engine/entities/entity_list.h"
#include "engine/entities/entity.h"
#include "engine/entities/entity_factory.h"
#include "engine/entities/map_loader.h"
#include "engine/entities/entity_class.h"
#include "engine/world/iworld.h"
#include "engine/core/log.h"
#include "engine/core/engine_api.h"
#include "engine/core/math/vec.h"

#include "game_entity_classes.h"

#include <cstring>
#include <cstdio>

// Helper macro for game module logging
#define GAME_INFO(msg, ...)  ::nova::Logger::instance().info(msg, ##__VA_ARGS__)
#define GAME_WARN(msg, ...) ::nova::Logger::instance().warn(msg, ##__VA_ARGS__)

namespace nova
{

// -----------------------------------------------------------------------
// GameModule constructor
// -----------------------------------------------------------------------
GameModule::GameModule()
{
}

// -----------------------------------------------------------------------
// GameModule destructor
// -----------------------------------------------------------------------
GameModule::~GameModule()
{
    shutdown();
}

// -----------------------------------------------------------------------
// setEngineAPI - receive engine services bridge
// -----------------------------------------------------------------------
void GameModule::setEngineAPI(EngineAPI* api)
{
    m_engineAPI = api;
}

// -----------------------------------------------------------------------
// init - initialize game state
// -----------------------------------------------------------------------
bool GameModule::init()
{
    GAME_INFO("GameModule: initializing...");

    m_playerHealth = 100.0f;
    m_playerArmor = 0.0f;

    for (int i = 0; i < 8; i++)
        m_ammo[i] = 0;

    m_weapons = 0;

    // Register game-specific entity classes with the engine registry
    registerGameEntityClasses(m_engineAPI);

    GAME_INFO("GameModule: ready");
    return true;
}

// -----------------------------------------------------------------------
// shutdown - cleanup game state
// -----------------------------------------------------------------------
void GameModule::shutdown()
{
    if (m_shutdownCalled) return;
    m_shutdownCalled = true;
    GAME_INFO("GameModule: shutdown");
}

// -----------------------------------------------------------------------
// loadMap - parse BSP entity lump and spawn all game entities
// -----------------------------------------------------------------------
void GameModule::loadMap(IWorld* world)
{
    if (!world) return;
    m_world = world;
    // Do NOT call MapLoader::load() here — engine already did it.
    // Use this hook to register game-specific callbacks on spawned entities,
    // set up game state, wire AI, etc.
    GAME_INFO("GameModule: world loaded — wiring game logic...");

    // Create a moving platform as a demonstration
    if (m_engineAPI)
    {
        // Platform spawns near the player deathmatch spawn, moves up 128 units
        Vec3 platOrigin = {-700.0f, -168.0f, 112.0f};  // Q2 Y-up coords
        m_engineAPI->createModelEntity("func_plat", platOrigin, "models/platform/platform.obj");
        GAME_INFO("GameModule: created func_plat at (%.1f, %.1f, %.1f)",
                  platOrigin.x, platOrigin.y, platOrigin.z);
    }

    // Example: find the worldspawn and read map properties
    // find player spawns, register think callbacks on items, etc.
}

// -----------------------------------------------------------------------
// think - per-frame game update
// -----------------------------------------------------------------------
void GameModule::think(float dt)
{
    updatePlayer(dt);
    checkPickups();
}

// -----------------------------------------------------------------------
// onEntitySpawn - handle new entity spawned
// -----------------------------------------------------------------------
void GameModule::onEntitySpawn(EntityHandle handle)
{
    Entity* ent = g_entityList.get(handle);
    if (!ent) return;

    if (std::strcmp(ent->classname, "player") == 0 ||
        std::strcmp(ent->classname, "info_player_deathmatch") == 0)
    {
        m_playerHandle = handle;
    }
}

// -----------------------------------------------------------------------
// onEntityDestroy - handle entity destroyed
// -----------------------------------------------------------------------
void GameModule::onEntityDestroy(EntityHandle)
{
}

// -----------------------------------------------------------------------
// onEntityTouch - handle collision
// -----------------------------------------------------------------------
void GameModule::onEntityTouch(EntityHandle handle, EntityHandle other)
{
    Entity* self = g_entityList.get(handle);
    Entity* otherEnt = g_entityList.get(other);
    if (!self || !otherEnt) return;

    // Check if player touched an item
    if (handle == m_playerHandle)
    {
        if (std::strncmp(otherEnt->classname, "item_", 5) == 0)
        {
            // Collect item
            if (std::strcmp(otherEnt->classname, "item_health") == 0)
            {
                addHealth(otherEnt->health);
            }
            else if (std::strcmp(otherEnt->classname, "item_health_small") == 0)
            {
                addHealth(25.0f);
            }
            else if (std::strcmp(otherEnt->classname, "item_health_large") == 0 ||
                    std::strcmp(otherEnt->classname, "item_health_mega") == 0)
            {
                addHealth(100.0f);
            }
            else if (std::strncmp(otherEnt->classname, "item_armor", 9) == 0)
            {
                addArmor(100.0f);
            }
            else if (std::strncmp(otherEnt->classname, "ammo_", 4) == 0)
            {
                addAmmo(0, 50);
            }
            else if (std::strncmp(otherEnt->classname, "weapon_", 7) == 0)
            {
                addWeapon(1);
            }

            g_entityList.destroy(other);
        }
    }
}

// -----------------------------------------------------------------------
// setPhysicsWorld - wire physics
// -----------------------------------------------------------------------
void GameModule::setPhysicsWorld(IPhysicsWorld* physics)
{
    m_physics = physics;
}

// -----------------------------------------------------------------------
// getPhysicsWorld
// -----------------------------------------------------------------------
IPhysicsWorld* GameModule::getPhysicsWorld()
{
    return m_physics;
}

// -----------------------------------------------------------------------
// setAudioSystem - wire audio
// -----------------------------------------------------------------------
void GameModule::setAudioSystem(IAudioSystem* audio)
{
    m_audio = audio;
}

// -----------------------------------------------------------------------
// getAudioSystem
// -----------------------------------------------------------------------
IAudioSystem* GameModule::getAudioSystem()
{
    return m_audio;
}

// -----------------------------------------------------------------------
// setNetworkSystem - wire networking
// -----------------------------------------------------------------------
void GameModule::setNetworkSystem(INetworkSystem* net)
{
    m_network = net;
}

// -----------------------------------------------------------------------
// getNetworkSystem
// -----------------------------------------------------------------------
INetworkSystem* GameModule::getNetworkSystem()
{
    return m_network;
}

// -----------------------------------------------------------------------
// getAmmo
// -----------------------------------------------------------------------
int GameModule::getAmmo(int type) const
{
    if (type < 0 || type >= 8) return 0;
    return m_ammo[type];
}

// -----------------------------------------------------------------------
// addHealth
// -----------------------------------------------------------------------
void GameModule::addHealth(float amount)
{
    m_playerHealth += amount;
    if (m_playerHealth > 100.0f) m_playerHealth = 100.0f;
}

// -----------------------------------------------------------------------
// addArmor
// -----------------------------------------------------------------------
void GameModule::addArmor(float amount)
{
    m_playerArmor += amount;
    if (m_playerArmor > 200.0f) m_playerArmor = 200.0f;
}

// -----------------------------------------------------------------------
// addAmmo
// -----------------------------------------------------------------------
void GameModule::addAmmo(int type, int amount)
{
    if (type < 0 || type >= 8) return;
    m_ammo[type] += amount;
}

// -----------------------------------------------------------------------
// addWeapon
// -----------------------------------------------------------------------
void GameModule::addWeapon(int weaponId)
{
    m_weapons |= (1 << weaponId);
}

// -----------------------------------------------------------------------
// updatePlayer
// -----------------------------------------------------------------------
void GameModule::updatePlayer(float)
{
    // Player state updates (input processing, etc.)
}

// -----------------------------------------------------------------------
// checkPickups
// -----------------------------------------------------------------------
void GameModule::checkPickups()
{
    // Check for pickup collisions
}

// -----------------------------------------------------------------------
// GetGameModule - factory function
// -----------------------------------------------------------------------
IGameModule* GetGameModule()
{
    static GameModule g_module;
    return &g_module;
}

} // namespace nova