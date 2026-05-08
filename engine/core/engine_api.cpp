// ============================================================
// FILE:    engine/core/engine_api.cpp
// MODULE:  Core
// PHASE:   2
// PURPOSE: Implementation of EngineAPI function table.
//          Renderer-dependent functions are wired at runtime
//          by Engine, so this file has NO renderer dependency.
//          Entity class registration is forwarded to engine's registry.
// DEPENDS: engine_api.h, entities/entity_list.h, entities/entity_class.h
// ============================================================
#include "engine/core/engine_api.h"
#include "engine/entities/entity_list.h"
#include "engine/entities/entity.h"
#include "engine/entities/entity_factory.h"

#include <cstring>

namespace nova
{

// ---- Non-renderer implementations (always available) ----
static float api_getEntityHealth(EntityHandle handle)
{
    const Entity* e = g_entityList.get(handle);
    return e ? e->health : 0.0f;
}

static void api_setEntityHealth(EntityHandle handle, float health)
{
    Entity* e = g_entityList.get(handle);
    if (e) e->health = health;
}

static bool api_isEntityAlive(EntityHandle handle)
{
    const Entity* e = g_entityList.get(handle);
    return e && e->state == STATE_ALIVE;
}

static EntityHandle api_findEntityByClassname(const char* classname)
{
    return g_entityList.findByClassname(classname);
}

static void api_setEntityOrigin(EntityHandle handle, const Vec3& origin)
{
    Entity* e = g_entityList.get(handle);
    if (e) e->origin = origin;
}

static Vec3 api_getEntityOriginFromPool(EntityHandle handle)
{
    const Entity* e = g_entityList.get(handle);
    return e ? e->origin : Vec3::zero();
}

// ---- Renderer-dependent implementations (wired at runtime) ----
static PFN_SetEntityAnim        s_setEntityAnim = nullptr;
static PFN_SetEntityAnimRange   s_setEntityAnimRange = nullptr;
static PFN_GetEntityOrigin      s_getEntityOrigin = nullptr;
static PFN_GetEntityAngles      s_getEntityAngles = nullptr;
static PFN_RegisterEntityClass  s_registerEntityClass = nullptr;
static PFN_CreateModelEntity    s_createModelEntity = nullptr;

static bool api_setEntityAnim(int entityIdx, const char* animName)
{
    return s_setEntityAnim ? s_setEntityAnim(entityIdx, animName) : false;
}

static void api_setEntityAnimRange(int entityIdx, int first, int last, float fps)
{
    if (s_setEntityAnimRange) s_setEntityAnimRange(entityIdx, first, last, fps);
}

static Vec3* api_getEntityOrigin(int entityIdx)
{
    return s_getEntityOrigin ? s_getEntityOrigin(entityIdx) : nullptr;
}

static Vec3* api_getEntityAngles(int entityIdx)
{
    return s_getEntityAngles ? s_getEntityAngles(entityIdx) : nullptr;
}

static EntityHandle api_createModelEntity(const char* classname, const Vec3& origin,
                                            const char* modelPath)
{
    if (s_createModelEntity) return s_createModelEntity(classname, origin, modelPath);
    return EntityHandle{};
}

// ---- Engine-side function to wire renderer callbacks ----
void EngineAPI_setModelRenderer(void* renderer,
                                  PFN_SetEntityAnim setAnim,
                                  PFN_SetEntityAnimRange setAnimRange,
                                  PFN_GetEntityOrigin getOrigin,
                                  PFN_GetEntityAngles getAngles,
                                  PFN_RegisterEntityClass registerClass)
{
    (void)renderer;
    s_setEntityAnim = setAnim;
    s_setEntityAnimRange = setAnimRange;
    s_getEntityOrigin = getOrigin;
    s_getEntityAngles = getAngles;
    s_registerEntityClass = registerClass;
}

// ---- Engine-side function to wire entity creation callbacks ----
void EngineAPI_setEntityOps(PFN_CreateModelEntity createModel,
                              PFN_SetEntityOrigin setOrigin,
                              PFN_GetEntityOriginFromPool getOriginFromPool,
                              PFN_FindEntityByClassname findByClassname)
{
    s_createModelEntity = createModel;

    // Override the EntityAPI function pointers for these ops
    // (they're already in the table as static stubs above)
    (void)setOrigin;       // handled by api_setEntityOrigin in table
    (void)getOriginFromPool; // handled by api_getEntityOriginFromPool in table
    (void)findByClassname;   // handled by api_findEntityByClassname in table
}

// ---- Entity class registration (cross-DLL boundary) ----
static void api_registerEntityClass(void* entityClass, const char* classname)
{
    if (s_registerEntityClass) s_registerEntityClass(entityClass, classname);
}

// ---- Global API table ----
static EngineAPI s_engineAPI = {
    api_setEntityAnim,
    api_setEntityAnimRange,
    api_getEntityOrigin,
    api_getEntityAngles,
    api_getEntityHealth,
    api_setEntityHealth,
    api_isEntityAlive,
    api_registerEntityClass,
    api_createModelEntity,
    api_setEntityOrigin,
    api_getEntityOriginFromPool,
    api_findEntityByClassname,
};

EngineAPI* g_engineAPI = &s_engineAPI;

} // namespace nova
