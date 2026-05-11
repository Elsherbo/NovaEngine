// ============================================================
// FILE:    game/src/game_entity_classes.cpp
// MODULE:  Game
// PURPOSE: Single-definition storage for game entity class statics.
//          Avoids ODR violations from header-only definitions.
// ============================================================

#include "game_entity_classes.h"

namespace nova
{

// DLL-local EngineAPI pointer — single definition (no ODR violation)
EngineAPI* s_gameAPI = nullptr;

// FuncPlat static bridge members — single definition
Entity* FuncPlat::s_currentPlat = nullptr;
Vec3    FuncPlat::s_platDelta   = Vec3::zero();

// -----------------------------------------------------------------------
// registerGameEntityClasses — called from GameModule::init()
// -----------------------------------------------------------------------
void registerGameEntityClasses(EngineAPI* api)
{
    s_gameAPI = api;

    // Functional null-checks that trigger during development if the
    // engine fails to wire callbacks before game init.
    if (!s_gameAPI) { fprintf(stderr, "FATAL: s_gameAPI is null in registerGameEntityClasses\n"); return; }
    if (!s_gameAPI->registerEntityClass) { fprintf(stderr, "FATAL: registerEntityClass callback is null\n"); return; }
    if (!s_gameAPI->iterateActiveEntities) { fprintf(stderr, "FATAL: iterateActiveEntities callback is null\n"); return; }
    if (!s_gameAPI->getEntityProperty) { fprintf(stderr, "FATAL: getEntityProperty callback is null\n"); return; }
    if (!s_gameAPI->setEntityProperty) { fprintf(stderr, "FATAL: setEntityProperty callback is null\n"); return; }

    static PlayerEntityClass s_playerClass;
    s_gameAPI->registerEntityClass(&s_playerClass, s_playerClass.classname());

    static MonsterClass s_monster;
    s_gameAPI->registerEntityClass(&s_monster, s_monster.classname());

    static FuncPlat s_plat;
    s_gameAPI->registerEntityClass(&s_plat, s_plat.classname());
}

} // namespace nova
