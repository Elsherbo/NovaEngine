// ============================================================
// FILE:    game/src/game_entity_classes.h
// MODULE:  Game
// PURPOSE: Game-specific entity classes using EngineAPI.
//          All property access and iteration route through EngineAPI
//          to avoid cross-DLL global duplication (g_propertyStore
//          and g_entityList are separate copies per binary).
// ============================================================
#pragma once

#include "engine/core/engine_api.h"
#include "engine/entities/entity_class.h"
#include "engine/entities/entity.h"

#include <cstdlib>
#include <cstdio>
#include <cstdint>

namespace nova
{

// DLL-local EngineAPI pointer — defined in game_entity_classes.cpp.
// Declared extern here so all TUs see the same variable.
extern EngineAPI* s_gameAPI;

// -----------------------------------------------------------------------
// MonsterClass — basic enemy entity with onSpawn + onThink hooks.
// Uses per-entity state (Entity::aiThinkTimer, aiState) — not shared.
// -----------------------------------------------------------------------
class MonsterClass : public EntityClass
{
public:
    const char* classname() const override { return "monster_soldier"; }

    void onSpawn(Entity* e) override
    {
        if (!e || !s_gameAPI) return;

        s_gameAPI->setEntityHealth(e->handle, 50.0f);

        int idx = e->handle.index();
        s_gameAPI->setEntityAnim(idx, "run");
    }

    void onThink(Entity* e, float dt) override
    {
        if (!e || !s_gameAPI) return;

        e->aiThinkTimer += dt;
        if (e->aiThinkTimer >= 5.0f)
        {
            e->aiThinkTimer = 0.0f;
            e->aiState = (e->aiState == 0) ? 1 : 0;

            int idx = e->handle.index();

            if (e->aiState == 1)
                s_gameAPI->setEntityAnim(idx, "run");
            else
                s_gameAPI->setEntityAnim(idx, "waiting");
        }
        (void)dt;
    }
};

// -----------------------------------------------------------------------
// Moving platform states (stored in Entity::platState)
// -----------------------------------------------------------------------
enum PlatState : uint8_t
{
    PLAT_IDLE_BOTTOM  = 0,
    PLAT_MOVING_UP    = 1,
    PLAT_WAITING_TOP  = 2,
    PLAT_MOVING_DOWN  = 3,
};

// -----------------------------------------------------------------------
// FuncPlat — cyclic vertical moving platform.
// TrenchBroom properties (read via EngineAPI to cross DLL boundary):
//   "height" = travel distance (default 64)
//   "speed"  = units/sec       (default 100)
//   "wait"   = pause at top    (default 2s)
//   "model"  = visual mesh     (default: none, uses a box if unset)
//
// All state stored per-entity in Entity struct (platStartPos, platTargetPos,
// platHeight, platSpeed, platWait, platWaitTimer, platState).
// Uses s_gameAPI->iterateActiveEntities() to avoid g_entityList duplication.
// -----------------------------------------------------------------------
class FuncPlat : public EntityClass
{
public:
    const char* classname() const override { return "func_plat"; }

    void onSpawn(Entity* e) override
    {
        if (!e || !s_gameAPI) return;

        const char* h = s_gameAPI->getEntityProperty(e->handle, "height");
        e->platHeight = h ? atof(h) : 64.0f;

        const char* s = s_gameAPI->getEntityProperty(e->handle, "speed");
        e->platSpeed = s ? atof(s) : 100.0f;

        const char* w = s_gameAPI->getEntityProperty(e->handle, "wait");
        e->platWait = w ? atof(w) : 2.0f;

        e->platStartPos  = e->origin;
        e->platTargetPos = e->platStartPos;
        e->platTargetPos.z += e->platHeight;

        e->platState      = PLAT_IDLE_BOTTOM;
        e->platWaitTimer  = 1.0f;
    }

    void onThink(Entity* e, float dt) override
    {
        if (!e) return;

        Vec3 prevOrigin = e->origin;

        switch (e->platState)
        {
        case PLAT_IDLE_BOTTOM:
            e->platWaitTimer -= dt;
            if (e->platWaitTimer <= 0.0f)
                e->platState = PLAT_MOVING_UP;
            break;

        case PLAT_MOVING_UP:
        {
            float step = e->platSpeed * dt;
            e->origin.z += step;
            if (e->origin.z >= e->platTargetPos.z)
            {
                e->origin.z = e->platTargetPos.z;
                e->platState = PLAT_WAITING_TOP;
                e->platWaitTimer = e->platWait;
            }
            break;
        }

        case PLAT_WAITING_TOP:
            e->platWaitTimer -= dt;
            if (e->platWaitTimer <= 0.0f)
                e->platState = PLAT_MOVING_DOWN;
            break;

        case PLAT_MOVING_DOWN:
        {
            float step = e->platSpeed * dt;
            e->origin.z -= step;
            if (e->origin.z <= e->platStartPos.z)
            {
                e->origin.z = e->platStartPos.z;
                e->platState = PLAT_IDLE_BOTTOM;
                e->platWaitTimer = e->platWait;
            }
            break;
        }
        }

        Vec3 delta = e->origin - prevOrigin;
        if (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f)
            carryEntities(e, delta);
    }

private:
    static void carryHelper(Entity& other)
    {
        if (other.state != STATE_ALIVE) return;
        // Use handle index for skip-guard (safe against pool reuse)
        if (other.handle.index() == s_currentPlat->handle.index()) return;
        if (other.carriedThisFrame) return;

        // Q2 Z-up convention: X,Y = horizontal, Z = vertical (feet→head)
        AABB platBox = { s_currentPlat->origin + s_currentPlat->mins,
                         s_currentPlat->origin + s_currentPlat->maxs };
        AABB otherBox = { other.origin + other.mins,
                          other.origin + other.maxs };

        // Horizontal overlap: entity center must be over the platform.
        // Using AABB overlap would let the player walk 16 units off the edge
        // (half hull width) while still being "carried", preventing them from
        // dropping down.  Center-point check stops carry as soon as the player's
        // midpoint passes the platform edge — gravity takes over naturally.
        bool hOverlap = (other.origin.x > platBox.min.x && other.origin.x < platBox.max.x) &&
                        (other.origin.y > platBox.min.y && other.origin.y < platBox.max.y);

        // Vertical: feet must be at or near platform top surface
        // [-4, +2]: -4 = slight ground-snap interpenetration, +2 = one physics frame of travel
        float feetDist = otherBox.min.z - platBox.max.z;
        bool onTop = (feetDist >= -4.0f && feetDist <= 2.0f);

        if (hOverlap && onTop)
        {
            // Inherit platform vertical velocity (~60fps approximation)
            other.velocity.z = s_platDelta.z * 60.0f;
            other.carriedThisFrame = 1;
        }
    }

    void carryEntities(Entity* plat, Vec3 delta)
    {
        s_currentPlat = plat;
        s_platDelta = delta;
        s_gameAPI->iterateActiveEntities(carryHelper);
        s_currentPlat = nullptr;
    }

    static Entity* s_currentPlat;
    static Vec3    s_platDelta;
};

// -----------------------------------------------------------------------
// PlayerEntityClass — minimal no-op class so the player entity has
// a valid entityClass pointer for correct EntityList dispatch.
// Player logic is driven by PlayerController, not onThink.
// -----------------------------------------------------------------------
class PlayerEntityClass : public EntityClass
{
public:
    const char* classname() const override { return "player"; }
    void onSpawn(Entity* e) override { (void)e; }
    void onThink(Entity* e, float dt) override { (void)e; (void)dt; }
};

// -----------------------------------------------------------------------
// registerGameEntityClasses — call from GameModule::init()
// -----------------------------------------------------------------------
void registerGameEntityClasses(EngineAPI* api);

} // namespace nova
