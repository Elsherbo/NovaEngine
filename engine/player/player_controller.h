// ============================================================
// FILE:    engine/player/player_controller.h
// MODULE:  Engine > Player
// PHASE:   2
// PURPOSE: First-person player movement controller.
//          Owns all physics-simulation logic for the player:
//            - Gravity & terminal velocity
//            - Ground detection / ground snap
//            - Jump (one-shot edge detection)
//            - Air drag & ground friction
//            - Q2 PM_Accelerate-style horizontal acceleration
//            - moveSlide BSP collision integration
//
//          Camera retains only:
//            - applyMouseLook()
//            - getViewMatrix() / getProjectionMatrix()
//            - position sync from PlayerController
//
//          This class lives in nova_engine (not nova_game) because
//          the engine main loop drives it directly. Game-specific
//          player logic (health, weapons, etc.) belongs in nova_game
//          and is accessed via IGameModule.
//
// USAGE:
//   PlayerController ctrl;
//   ctrl.setPhysicsWorld(phys);
//   ctrl.setEntity(handle);
//
//   // Each frame:
//   ctrl.update(input, dt, cam.getForward(), cam.getRight());
//   camera.setPosition(ctrl.getEyePosition());
// ============================================================
#pragma once

#include "engine/core/math/vec.h"
#include "engine/entities/entity_id.h"

namespace nova
{

struct InputState;
class  IPhysicsWorld;

// ---------------------------------------------------------------------------
// Feel constants — tune here, no hunting through code.
//
//  Jump height formula: h = kJumpSpeed² / (2 * kGravity)
//    kJumpSpeed=350, kGravity=800  →  h ≈ 77 units
//
//  kAirControl: 0 = CS-like (no air steering)
//               1 = Q2-like (partial air steering)
//               3 = Quake 1-like (strong air steering)
// ---------------------------------------------------------------------------
static constexpr float kPC_JumpSpeed   = 350.0f;
static constexpr float kPC_AirControl  = 1.0f;
static constexpr float kPC_Friction    = 6.0f;
static constexpr float kPC_GroundAccel = 10.0f;

// Player AABB half-extents in Y-up engine space.
// These match what the BSP loader expects for Q2-scale maps
// (Q2 player hull: ±16 on X/Z, -36 to +36 on Y after q2ToGL).
static constexpr float kPC_HullHalfX  = 16.0f;
static constexpr float kPC_HullHalfY  = 36.0f;
static constexpr float kPC_HullHalfZ  = 16.0f;

// Distance below origin to probe for the ground.
static constexpr float kPC_GroundProbe = 4.0f;

// Minimum Y-component of a hit normal considered "ground".
// cos(45°) = 0.707: anything steeper is treated as a wall.
static constexpr float kPC_GroundNormal = 0.7f;

// Maximum downward speed (units/s) — prevents tunnelling through thin geometry.
static constexpr float kPC_TerminalVelocity = 1800.0f;

class PlayerController
{
public:
    PlayerController();

    // ---- Wiring ----
    void setPhysicsWorld(IPhysicsWorld* world) { m_physics = world; }
    IPhysicsWorld* getPhysicsWorld() const     { return m_physics; }

    void setEntity(EntityHandle e) { m_entity = e; }
    EntityHandle getEntity() const { return m_entity; }

    // ---- Per-frame update (call once per tick before Camera position sync) ----
    // 'fwd' and 'right' are the camera's horizontal-locked forward and right
    // vectors (getForward() / getRight()). PlayerController does not need the
    // full camera orientation — only the movement plane basis.
    void update(const InputState& input, float dt,
                const Vec3& fwd, const Vec3& right);

    // ---- Queries ----
    Vec3 getEyePosition() const { return m_position; }
    Vec3 getVelocity()    const { return m_velocity; }
    bool isOnGround()     const { return m_grounded; }

    // ---- Setters ----
    void setPosition(const Vec3& pos) { m_position = pos; }
    void setMoveSpeed(float s)        { m_moveSpeed = s; }
    float getMoveSpeed()        const { return m_moveSpeed; }

private:
    void applyGravity(float dt);
    void applyFriction(float dt);
    void applyAcceleration(const Vec3& wishDir, float speed, float dt);
    bool detectGround();

    // ---- Physics ----
    IPhysicsWorld* m_physics = nullptr;
    EntityHandle   m_entity  = {};

    // ---- State ----
    Vec3  m_position  = {};
    Vec3  m_velocity  = {};
    bool  m_grounded  = false;
    bool  m_spaceHeld = false;   // edge-detect: prevent hold-to-fly jump

    // ---- Settings ----
    float m_moveSpeed = 400.0f; // units/s (Q2 maps are large)

    Vec3 m_playerMins = { -kPC_HullHalfX, -kPC_HullHalfY, -kPC_HullHalfZ };
    Vec3 m_playerMaxs = {  kPC_HullHalfX,  kPC_HullHalfY,  kPC_HullHalfZ };
};

} // namespace nova
