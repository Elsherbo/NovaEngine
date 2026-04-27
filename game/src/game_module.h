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

private:
    void updatePlayer(float dt);
    void checkPickups();

    IPhysicsWorld* m_physics = nullptr;
    IAudioSystem* m_audio = nullptr;
    INetworkSystem* m_network = nullptr;

    EntityHandle m_playerHandle = {};

    float m_playerHealth = 100.0f;
    float m_playerArmor = 0.0f;
    int m_ammo[8] = {};  // ammo types
    int m_weapons = 0;
};

// -----------------------------------------------------------------------
// GetGameModule - exported factory function
// Game DLL must export this under GetGameModule name
// -----------------------------------------------------------------------
extern "C" NOVA_API IGameModule* GetGameModule();

} // namespace nova