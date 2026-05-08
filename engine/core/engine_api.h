// ============================================================
// FILE:    engine/core/engine_api.h
// MODULE:  Core
// PHASE:   2
// PURPOSE: Public C-API bridge from engine to game DLL.
//          Exposes ModelRenderer and entity ops without
//          leaking Engine internals or C++ ABI across DLL boundaries.
// DEPENDS: entities/entity_id.h, renderer/models/md2.h
// ============================================================
#pragma once

#include "engine/entities/entity_id.h"
#include "engine/core/math/vec.h"

namespace nova
{

// -----------------------------------------------------------------------
// EngineAPI — opaque handle + function table for cross-DLL calls.
// The engine creates one instance and passes it to IGameModule at init.
// -----------------------------------------------------------------------
struct EngineAPI
{
    // Set entity animation by name (e.g., "stand", "run", "attack").
    // Returns true if found and set, false otherwise.
    bool (*setEntityAnim)(int entityIdx, const char* animName);

    // Set entity animation by explicit frame range.
    void (*setEntityAnimRange)(int entityIdx, int first, int last, float fps);

    // Get mutable origin for an MD2 entity instance (GL Y-up coords).
    Vec3* (*getEntityOrigin)(int entityIdx);

    // Get mutable angles for an MD2 entity instance (radians).
    Vec3* (*getEntityAngles)(int entityIdx);

    // Get current entity health.
    float (*getEntityHealth)(EntityHandle handle);

    // Set entity health.
    void (*setEntityHealth)(EntityHandle handle, float health);

    // Check if entity exists and is alive.
    bool (*isEntityAlive)(EntityHandle handle);

    // Register an entity class with the engine's registry (cross-DLL boundary).
    // `entityClass` is a void* to an object with a `classname()` method returning
    // the classname string. The engine will call this to get the classname and
    // register the class with its EntityList dispatch system.
    void (*registerEntityClass)(void* entityClass, const char* classname);

    // Create a model entity at runtime (for entities not in BSP lump).
    // Returns the entity handle. Loads the model (MD2 or OBJ) and registers
    // it with the ModelRenderer.
    EntityHandle (*createModelEntity)(const char* classname, const Vec3& origin,
                                       const char* modelPath);

    // Set entity origin in world space (Q2 Y-up coords).
    void (*setEntityOrigin)(EntityHandle handle, const Vec3& origin);

    // Get entity origin from the Entity pool (Q2 Y-up coords).
    Vec3 (*getEntityOriginFromPool)(EntityHandle handle);

    // Find entity by classname. Returns invalid handle if not found.
    EntityHandle (*findEntityByClassname)(const char* classname);
};

// Global engine API pointer — set by Engine before GameModule::init().
// Game DLL code can use this directly after receiving it.
extern EngineAPI* g_engineAPI;

// Function pointer types for renderer callbacks (wired at runtime).
typedef bool (*PFN_SetEntityAnim)(int, const char*);
typedef void (*PFN_SetEntityAnimRange)(int, int, int, float);
typedef Vec3* (*PFN_GetEntityOrigin)(int);
typedef Vec3* (*PFN_GetEntityAngles)(int);
typedef void (*PFN_RegisterEntityClass)(void*, const char*);
typedef EntityHandle (*PFN_CreateModelEntity)(const char*, const Vec3&, const char*);
typedef void (*PFN_SetEntityOrigin)(EntityHandle, const Vec3&);
typedef Vec3 (*PFN_GetEntityOriginFromPool)(EntityHandle);
typedef EntityHandle (*PFN_FindEntityByClassname)(const char*);

// Called by Engine at startup to wire renderer callbacks and entity class registrar.
// The game DLL never calls this — it's purely an engine-side bootstrap.
void EngineAPI_setModelRenderer(void* renderer,
                                  PFN_SetEntityAnim setAnim,
                                  PFN_SetEntityAnimRange setAnimRange,
                                  PFN_GetEntityOrigin getOrigin,
                                  PFN_GetEntityAngles getAngles,
                                  PFN_RegisterEntityClass registerClass);

// Called by Engine at startup to wire runtime entity creation callbacks.
void EngineAPI_setEntityOps(PFN_CreateModelEntity createModel,
                             PFN_SetEntityOrigin setOrigin,
                             PFN_GetEntityOriginFromPool getOriginFromPool,
                             PFN_FindEntityByClassname findByClassname);

} // namespace nova
