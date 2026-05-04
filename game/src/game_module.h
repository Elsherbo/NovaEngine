// ============================================================
// FILE:    game/src/game_module.h
// MODULE:  Game
// PURPOSE: GameModule implementation - retro boomer shooter base
// ============================================================
#pragma once

#ifdef _WIN32
    #define NOVA_API __declspec(dllexport)
#else
    #define NOVA_API __attribute__((visibility("default")))
#endif

#include "engine/entities/igame_module.h"

namespace nova
{

// -----------------------------------------------------------------------
// GameModule - implements IGameModule
// Provides base game logic: player, items, weapons
// This is the BASE - subclasses can override behavior
// -----------------------------------------------------------------------
class GameModule : public IGameModule
{
public:
    GameModule();
    ~GameModule();

    // ---- IGameModule ----
    bool init() override;
    void shutdown() override;
    void think(float dt) override;
    void loadMap(IWorld* world) override;

    void onEntitySpawn(EntityHandle handle) override;
    void onEntityDestroy(EntityHandle handle) override;
    void onEntityTouch(EntityHandle handle, EntityHandle other) override;

    void setPhysicsWorld(IPhysicsWorld* physics) override;
    IPhysicsWorld* getPhysicsWorld() override;

    void setAudioSystem(IAudioSystem* audio) override;
    IAudioSystem* getAudioSystem() override;

    void setNetworkSystem(INetworkSystem* net) override;
    INetworkSystem* getNetworkSystem() override;

    // ---- Player state ----
    float getPlayerHealth() const { return m_playerHealth; }
    float getPlayerArmor() const { return m_playerArmor; }
    int getAmmo(int type) const;

    void addHealth(float amount);
    void addArmor(float amount);
    void addAmmo(int type, int amount);
    void addWeapon(int weaponId);

    // ---- Player properties getters (game-specific) ----
    float getPlayerSpeed() const { return m_playerSpeed; }
    float getPlayerJump() const { return m_playerJump; }
    float getPlayerGravity() const { return m_playerGravity; }
    int getCurrentWeapon() const { return m_currentWeapon; }

    // ---- Set player properties (for savegames, etc.) ----
    void setPlayerSpeed(float speed) { m_playerSpeed = speed; }
    void setPlayerJump(float jump) { m_playerJump = jump; }
    void setPlayerGravity(float gravity) { m_playerGravity = gravity; }

private:
    void updatePlayer(float dt);
    void checkPickups();

    IPhysicsWorld* m_physics = nullptr;
    IAudioSystem* m_audio = nullptr;
    INetworkSystem* m_network = nullptr;

    EntityHandle m_playerHandle = {};

    // Player properties - all game-specific, defaults for retro boomer shooter
    float m_playerHealth = 100.0f;
    float m_playerArmor = 0.0f;
    float m_playerSpeed = 200.0f;       // units/second
    float m_playerJump = 180.0f;        // jump velocity
    float m_playerGravity = 800.0f;    // gravity
    int m_ammo[8] = {};               // ammo types
    int m_weapons = 0;
    int m_currentWeapon = 0;
    IWorld* m_world = nullptr;
    bool m_shutdownCalled = false;
};

// -----------------------------------------------------------------------
// GetGameModule - exported factory function
// Game DLL must export this under GetGameModule name
// -----------------------------------------------------------------------
extern "C" NOVA_API IGameModule* GetGameModule();

} // namespace nova