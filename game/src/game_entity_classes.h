// ============================================================
// FILE:    game/src/game_entity_classes.h
// MODULE:  Game
// PURPOSE: Game-specific entity classes using EngineAPI.
//          Register in GameModule::init().
// ============================================================
#pragma once

#include "engine/core/engine_api.h"
#include "engine/entities/entity_class.h"
#include "engine/entities/entity_list.h"

#include <cstdlib>
#include <cstdio>

namespace nova
{

// DLL-local EngineAPI pointer — set by registerGameEntityClasses().
// Avoids cross-DLL global duplication (g_engineAPI from nova_core
// is a separate copy in the game DLL).
static EngineAPI* s_gameAPI = nullptr;

// -----------------------------------------------------------------------
// EnemyClass — basic enemy entity with onSpawn + onThink hooks.
// Demonstrates the EngineAPI-based entity class system.
// -----------------------------------------------------------------------
class EnemyClass : public EntityClass
{
public:
    const char* classname() const override { return "info_player_deathmatch"; }

    void onSpawn(Entity* e) override
    {
        if (!e || !s_gameAPI) return;

        s_gameAPI->setEntityHealth(e->handle, 50.0f);

        int idx = e->handle.index();
        s_gameAPI->setEntityAnim(idx, "run");
        fprintf(stdout, "[EnemyClass] onSpawn: entity[%d] requested anim='run'\n", idx);
        fflush(stdout);
    }

    void onThink(Entity* e, float dt) override
    {
        if (!e || !s_gameAPI) return;

        m_thinkTimer += dt;
        if (m_thinkTimer >= 5.0f)
        {
            m_thinkTimer = 0.0f;
            m_chasing = !m_chasing;

            int idx = e->handle.index();

            if (m_chasing)
            {
                s_gameAPI->setEntityAnim(idx, "run");
                fprintf(stdout, "[EnemyClass] onThink: entity[%d] chasing='run'\n", idx);
            }
            else
            {
                s_gameAPI->setEntityAnim(idx, "waiting");
                fprintf(stdout, "[EnemyClass] onThink: entity[%d] idle='waiting'\n", idx);
            }
            fflush(stdout);
        }
        (void)dt;
    }

private:
    float m_thinkTimer = 0.0f;
    bool  m_chasing = false;
};

// -----------------------------------------------------------------------
// Moving platform states
// -----------------------------------------------------------------------
enum class PlatState : uint8_t
{
    IDLE_BOTTOM,
    MOVING_UP,
    WAITING_TOP,
    MOVING_DOWN,
};

// -----------------------------------------------------------------------
// FuncPlat — cyclic vertical moving platform.
// TrenchBroom properties:
//   "height" = travel distance (default 64)
//   "speed"  = units/sec       (default 100)
//   "wait"   = pause at top    (default 2s)
//   "model"  = visual mesh     (default: none, uses a box if unset)
//
// Carries any entity standing on top via AABB overlap check.
// -----------------------------------------------------------------------
class FuncPlat : public EntityClass
{
public:
    const char* classname() const override { return "func_plat"; }

    void onSpawn(Entity* e) override
    {
        if (!e) return;

        // Read TrenchBroom properties
        const char* h = e->getProperty("height");
        m_height = h ? atof(h) : 64.0f;

        const char* s = e->getProperty("speed");
        m_speed = s ? atof(s) : 100.0f;

        const char* w = e->getProperty("wait");
        m_wait = w ? atof(w) : 2.0f;

        // Platform bounds (default: 64x64x16 box)
        e->mins = Vec3{-32, -32, 0};
        e->maxs = Vec3{32, 32, 16};

        m_startPos = e->origin;
        m_targetPos = m_startPos;
        m_targetPos.y += m_height;  // Q2 Y = up

        m_state = PlatState::IDLE_BOTTOM;
        m_waitTimer = 1.0f;  // short initial delay before first move

        fprintf(stdout, "[FuncPlat] entity[%d] at (%.1f,%.1f,%.1f) h=%.0f spd=%.0f wait=%.1f\n",
                e->handle.index(), e->origin.x, e->origin.y, e->origin.z,
                m_height, m_speed, m_wait);
        fflush(stdout);
    }

    void onThink(Entity* e, float dt) override
    {
        if (!e) return;

        Vec3 prevOrigin = e->origin;

        switch (m_state)
        {
        case PlatState::IDLE_BOTTOM:
            m_waitTimer -= dt;
            if (m_waitTimer <= 0.0f)
            {
                m_state = PlatState::MOVING_UP;
            }
            break;

        case PlatState::MOVING_UP:
        {
            float step = m_speed * dt;
            e->origin.y += step;
            if (e->origin.y >= m_targetPos.y)
            {
                e->origin.y = m_targetPos.y;
                m_state = PlatState::WAITING_TOP;
                m_waitTimer = m_wait;
            }
            break;
        }

        case PlatState::WAITING_TOP:
            m_waitTimer -= dt;
            if (m_waitTimer <= 0.0f)
            {
                m_state = PlatState::MOVING_DOWN;
            }
            break;

        case PlatState::MOVING_DOWN:
        {
            float step = m_speed * dt;
            e->origin.y -= step;
            if (e->origin.y <= m_startPos.y)
            {
                e->origin.y = m_startPos.y;
                m_state = PlatState::IDLE_BOTTOM;
                m_waitTimer = m_wait;
            }
            break;
        }
        }

        // Carrier: move entities standing on top
        Vec3 delta = e->origin - prevOrigin;
        if (delta.x != 0.0f || delta.y != 0.0f || delta.z != 0.0f)
            carryEntities(e, delta);
    }

private:
    static void carryHelper(Entity& other)
    {
        if (other.state != STATE_ALIVE) return;
        if (!s_currentPlat) return;

        AABB platBox = { s_currentPlat->origin + s_currentPlat->mins,
                         s_currentPlat->origin + s_currentPlat->maxs };
        AABB otherBox = { other.origin + other.mins,
                          other.origin + other.maxs };

        bool hOverlap = (otherBox.max.x > platBox.min.x && otherBox.min.x < platBox.max.x) &&
                        (otherBox.max.z > platBox.min.z && otherBox.min.z < platBox.max.z);

        float feetDist = otherBox.min.y - platBox.max.y;
        bool onTop = (feetDist >= -4.0f && feetDist <= 16.0f);

        if (hOverlap && onTop)
        {
            other.origin = other.origin + s_platDelta;
        }
    }

    void carryEntities(Entity* plat, Vec3 delta)
    {
        s_currentPlat = plat;
        s_platDelta = delta;
        g_entityList.iterateActive(carryHelper);
        s_currentPlat = nullptr;
    }

    static Entity* s_currentPlat;
    static Vec3    s_platDelta;

    Vec3      m_startPos  = Vec3::zero();
    Vec3      m_targetPos = Vec3::zero();
    float     m_height    = 64.0f;
    float     m_speed     = 100.0f;
    float     m_wait      = 2.0f;
    float     m_waitTimer = 0.0f;
    PlatState m_state     = PlatState::IDLE_BOTTOM;
};

// Static members for callback bridge (avoid heap alloc in iterateActive)
Entity* FuncPlat::s_currentPlat = nullptr;
Vec3    FuncPlat::s_platDelta   = Vec3::zero();

// -----------------------------------------------------------------------
// registerGameEntityClasses — call from GameModule::init()
// -----------------------------------------------------------------------
inline void registerGameEntityClasses(EngineAPI* api)
{
    s_gameAPI = api;

    static EnemyClass s_enemy;
    s_gameAPI->registerEntityClass(&s_enemy, s_enemy.classname());

    static FuncPlat s_plat;
    s_gameAPI->registerEntityClass(&s_plat, s_plat.classname());
}

} // namespace nova
