// ============================================================
// FILE:    engine/player/player_controller.h
// MODULE:  Engine > Player
// PHASE:   2  (updated: CVar integration)
// PURPOSE: First-person player movement controller.
//
// CHANGE (CVar revision):
//   All static constexpr feel constants have been replaced with
//   CVar* registrations via CVarSystem.  The constants are now
//   tunable at runtime via the in-game console, e.g.:
//
//     pc_jumpspeed 350
//     pc_friction 4
//     listcvars
//
//   The kPC_* names are kept as comments next to each registration
//   so the old values are still visible as defaults.
//
//   Hull geometry constants (HullHalfX/Y/Z, GroundNormal,
//   TerminalVelocity, EyeHeight, GroundProbe) are NOT converted to
//   CVars because they affect collision geometry and are not feel
//   tuning knobs — changing them at runtime would desync physics.
//   They remain static constexpr below.
// ============================================================
#pragma once

#include "engine/core/math/vec.h"
#include "engine/entities/entity_id.h"
#include "engine/core/cvar.h"

namespace nova
{

struct InputState;
class  IPhysicsWorld;

// ---------------------------------------------------------------------------
// Hull / physics constants — NOT CVars (affect collision geometry).
// Changing these at runtime would require re-creating the physics entity.
// ---------------------------------------------------------------------------
static constexpr float kPC_HullHalfX        = 16.0f;
static constexpr float kPC_HullHalfY        = 28.0f;
static constexpr float kPC_HullHalfZ        = 16.0f;
static constexpr float kPC_EyeHeight        = 22.0f;
static constexpr float kPC_GroundNormal     = 0.7f;
static constexpr float kPC_TerminalVelocity = 1800.0f;
static constexpr float kPC_GroundProbe      = 170.0f;

// ---------------------------------------------------------------------------
// Feel CVars — registered once at program start via inline definitions.
// Read as cv_XXX->value in the hot path (plain float read — zero overhead).
//
// Naming convention:  pc_ prefix = PlayerController
//   pc_jumpspeed   — upward velocity on jump
//   pc_movespeed   — base horizontal wish speed (units/s)
//   pc_groundaccel — PM_Accelerate scale on ground
//   pc_airaccel    — PM_Accelerate scale in air (0 = CS-like, 1 = Q2-like)
//   pc_friction    — ground friction coefficient
//   pc_stopspeed   — speed below which friction applies stopspeed threshold
//   pc_maxspeed    — max horizontal speed clamp on ground
//   pc_sprintmult  — multiplier applied to movespeed while shift is held
// ---------------------------------------------------------------------------
inline CVar* cv_pc_jumpspeed   = CVarSystem::instance().reg("pc_jumpspeed",   270.0f, "jump impulse (units/s)");
inline CVar* cv_pc_movespeed   = CVarSystem::instance().reg("pc_movespeed",   250.0f, "base move speed (units/s)");
inline CVar* cv_pc_groundaccel = CVarSystem::instance().reg("pc_groundaccel",  10.0f, "ground PM_Accelerate scale");
inline CVar* cv_pc_airaccel    = CVarSystem::instance().reg("pc_airaccel",      0.0f, "air PM_Accelerate scale (0=CS, 1=Q2)");
inline CVar* cv_pc_friction    = CVarSystem::instance().reg("pc_friction",      6.0f, "ground friction coefficient");
inline CVar* cv_pc_stopspeed   = CVarSystem::instance().reg("pc_stopspeed",   100.0f, "friction stopspeed threshold");
inline CVar* cv_pc_maxspeed    = CVarSystem::instance().reg("pc_maxspeed",    320.0f, "max horizontal speed on ground");
inline CVar* cv_pc_sprintmult  = CVarSystem::instance().reg("pc_sprintmult",   2.0f,  "shift sprint speed multiplier");

class PlayerController
{
public:
    PlayerController();

    // ---- Wiring ----
    void setPhysicsWorld(IPhysicsWorld* world) { m_physics = world; }
    IPhysicsWorld* getPhysicsWorld() const     { return m_physics; }

    void setEntity(EntityHandle e) { m_entity = e; }
    EntityHandle getEntity() const { return m_entity; }

    // ---- Per-frame update ----
    void update(const InputState& input, float dt,
                const Vec3& fwd, const Vec3& right);

    // ---- Queries ----
    Vec3 getEyePosition() const { return { m_position.x, m_position.y + kPC_EyeHeight, m_position.z }; }
    Vec3 getVelocity()    const { return m_velocity; }
    bool isOnGround()     const { return m_grounded; }

    // ---- Setters ----
    // pos = GL-space FEET position (center of hull bottom, NOT hull center).
    // Camera = pos + kPC_EyeHeight, hull center = pos + kPC_HullHalfY.
    void setPosition(const Vec3& pos) { m_position = pos; }
    void setGrounded(bool g) { m_grounded = g; }

private:
    void applyGravity(float dt);
    void applyFriction(float dt);
    void applyAcceleration(const Vec3& wishDir, float speed, float dt);
    bool detectGround();

    IPhysicsWorld* m_physics = nullptr;
    EntityHandle   m_entity  = {};

    Vec3  m_position  = {};
    Vec3  m_velocity  = {};
    bool  m_grounded  = false;
    bool  m_spaceHeld = false;

    Vec3 m_playerMins = { -kPC_HullHalfX, -kPC_HullHalfY, -kPC_HullHalfZ };
    Vec3 m_playerMaxs = {  kPC_HullHalfX,  kPC_HullHalfY,  kPC_HullHalfZ };
};

} // namespace nova