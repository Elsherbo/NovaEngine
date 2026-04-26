// ============================================================
// FILE:    engine/physics/aabb_physics.cpp
// MODULE:  Physics
// PHASE:   2
// STATUS:  FIXED
//
// FIX LOG (this revision):
//   1. testSolid() added: checks every SOLID/PLAYERCLIP brush
//      to see whether the AABB at 'origin' is fully inside it.
//
//   2. kPenetrationSlack raised from 0.03125 to 0.125.
//
//   3. moveSlide() crease fix: two-plane clip produces edge
//      direction instead of zeroing velocity.
//
//   4. moveSlide() zero-normal guard added.
//
//   5. step() uses m_gravity instead of hardcoded constant.
//
//   6. traceWorld() leaf-brush bounds check fixed.
//
//   7. traceWorld() dist==0 returns start, not start+dir.
//
//   8. moveSlide() step-up: only zero vel.y when negative.
//
//   9. moveSlide() step-up: slide loop always runs after step.
//
//  *** NEW FIXES (wall/floor pass-through) ***
//
//  10. [FIX] CONTENTS_WINDOW (0x2) added to solid mask.
//      Q2 glass/window brushes use this flag. Without it, any
//      wall or pillar built from window-content brushes is
//      completely transparent to the player hull. This was the
//      primary cause of "walk through certain walls/pillars".
//
//  11. [FIX] traceWorld() startSolid now returns fraction=0 and
//      endPos=start instead of fraction=1 and endPos=start+dir.
//      The old code set r.startSolid=true but then left
//      r.fraction at 1.0, so moveSlide() moved the player the
//      full unclipped distance — directly through the solid.
//      Fix: after the brush loop, if startSolid and no valid
//      forward hit was found, clamp fraction=0 / endPos=start.
//      This is the primary cause of "fall through floor on some
//      maps": any frame where the player's hull touched a floor
//      brush within the Minkowski expansion threshold triggered
//      startSolid on that brush, giving fraction=1.0, and the
//      player was teleported through the floor by the delta.
//
//  12. [FIX] moveSlide() step-up now traces UPWARD before the
//      lateral step trace. The old code directly raised stepPos
//      by kStepHeight without checking for a ceiling. If stepPos
//      landed inside a ceiling brush, traceWorld() returned
//      startSolid → fraction=1.0 (bug 11) → stepTr.fraction was
//      1.0 (greater than groundTr.fraction) → step was accepted
//      → player teleported into ceiling solid → every subsequent
//      trace started inside solid → player fell forever through
//      the map. This was the primary cause of "fall through
//      ground on other maps" (maps with low ceilings near stairs).
//      Fix: trace from pos to stepPos first; use the actual
//      clear height as the step origin, and abort the step-up if
//      the vertical trace is blocked (ceiling is in the way).
//
//  13. [FIX] moveSlide() slide loop now breaks immediately when
//      traceWorld() returns startSolid=true (fraction=0, zero
//      normal). The old code continued: remainFrac *= (1-0) =
//      unchanged, then the zero-normal guard fired and broke —
//      but only after consuming an entire loop iteration that
//      could have a side-effect of applying a spurious push.
//      Now the break is explicit and immediate.
//
//  14. [FIX] testSolid() and traceWorld() both updated to use
//      kSolidMask (SOLID|WINDOW|PLAYERCLIP|MONSTERCLIP) instead
//      of a partial two-flag check.
// ============================================================

#include "engine/physics/aabb_physics.h"
#include "engine/renderer/bsp/bsp.h"

#include <algorithm>
#include <cmath>
#include <cassert>

namespace nova
{

// -----------------------------------------------------------------------
//  Constants
// -----------------------------------------------------------------------
static constexpr float kClipEpsilon     = 0.03125f;   // Q2 DIST_EPSILON
static constexpr float kPenetrationSlack = 0.125f;
static constexpr float kMinMoveDist     = 1e-4f;
static constexpr float kMinVelocitySq  = 1e-8f;
static constexpr int   kMaxSlideIter   = 4;
static constexpr float kGravityDefault = 800.0f;
static constexpr float kStepHeight     = 18.0f;

// FIX 10: Full Q2 MASK_PLAYERSOLID.
// Missing CONTENTS_WINDOW was causing pass-through on glass/window brushes.
static constexpr int32_t CONTENTS_SOLID       = 0x0001;
static constexpr int32_t CONTENTS_WINDOW      = 0x0002;  // glass, grates
static constexpr int32_t CONTENTS_PLAYERCLIP  = 0x10000;
static constexpr int32_t CONTENTS_MONSTERCLIP = 0x20000;
static constexpr int32_t kSolidMask =
    CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP;

// -----------------------------------------------------------------------
//  Helpers
// -----------------------------------------------------------------------

static inline float planeSignedDist(const BSPPlane& plane, const Vec3& p)
{
    return plane.normal.x * p.x
         + plane.normal.y * p.y
         + plane.normal.z * p.z
         - plane.dist;
}

static inline float planeBoxOffset(const Vec3& normal, const Vec3& extents)
{
    return std::abs(normal.x * extents.x)
         + std::abs(normal.y * extents.y)
         + std::abs(normal.z * extents.z);
}

// -----------------------------------------------------------------------
//  Constructor
// -----------------------------------------------------------------------
AABBPhysics::AABBPhysics()
{
    m_bsp         = nullptr;
    m_gravity     = kGravityDefault;
    m_entOrigin   = nullptr;
    m_entVelocity = nullptr;
    m_entCount    = 0;
    m_playerMins  = { -16.f, -36.f, -16.f };
    m_playerMaxs  = {  16.f,  36.f,  16.f };
}

void AABBPhysics::setWorld(IBSPCollisionWorld* bsp) { m_bsp = bsp; }
void AABBPhysics::setEntityStorage(Vec3* o, Vec3* v, size_t count)
{
    m_entOrigin   = o;
    m_entVelocity = v;
    m_entCount    = count;
}

static bool isValidEntityIndex(EntityHandle e, size_t count)
{
    return e.isValid() && static_cast<size_t>(e.index()) < count;
}

void AABBPhysics::setOrigin(EntityHandle e, const Vec3& o)
{
    if (!m_entOrigin || !isValidEntityIndex(e, m_entCount)) return;
    m_entOrigin[e.index()] = o;
}

void AABBPhysics::setVelocity(EntityHandle e, const Vec3& v)
{
    if (!m_entVelocity || !isValidEntityIndex(e, m_entCount)) return;
    m_entVelocity[e.index()] = v;
}

Vec3 AABBPhysics::getOrigin(EntityHandle e) const
{
    if (!m_entOrigin || !isValidEntityIndex(e, m_entCount)) return Vec3{};
    return m_entOrigin[e.index()];
}

Vec3 AABBPhysics::getVelocity(EntityHandle e) const
{
    if (!m_entVelocity || !isValidEntityIndex(e, m_entCount)) return Vec3{};
    return m_entVelocity[e.index()];
}
void AABBPhysics::setGravity(float g) { m_gravity = g; }
float AABBPhysics::getGravity() const { return m_gravity; }

// -----------------------------------------------------------------------
//  testSolid
//
//  Returns true if the hull placed at 'origin' is fully inside any
//  solid brush (all Minkowski-expanded plane tests are negative).
//  Used for safe spawn placement.
// -----------------------------------------------------------------------
bool AABBPhysics::testSolid(const Vec3& origin,
                             const Vec3& mins, const Vec3& maxs) const
{
    if (!m_bsp) return false;

    const Vec3 center {
        origin.x + (mins.x + maxs.x) * 0.5f,
        origin.y + (mins.y + maxs.y) * 0.5f,
        origin.z + (mins.z + maxs.z) * 0.5f };
    const Vec3 extents {
        (maxs.x - mins.x) * 0.5f,
        (maxs.y - mins.y) * 0.5f,
        (maxs.z - mins.z) * 0.5f };

    for (int bi = 0; bi < m_bsp->brushCount(); ++bi)
    {
        const BSPBrush& brush = m_bsp->brushes()[bi];
        if (!(brush.contents & kSolidMask)) continue;   // FIX 14

        bool allInside = true;
        for (uint32_t si = brush.firstBrushSide;
             si < brush.firstBrushSide + brush.numBrushSides; ++si)
        {
            if (si >= (uint32_t)m_bsp->brushSideCount()) { allInside = false; break; }
            const BSPBrushSide& bs = m_bsp->brushSides()[si];
            if (bs.plane >= (uint16_t)m_bsp->planeCount()) { allInside = false; break; }
            const BSPPlane& plane = m_bsp->planes()[bs.plane];

            float d = planeSignedDist(plane, center);
            float o = planeBoxOffset(plane.normal, extents);
            if (d > -o) { allInside = false; break; }
        }

        if (allInside) return true;
    }
    return false;
}

// -----------------------------------------------------------------------
//  traceWorld — brute-force swept AABB vs all solid brushes
//
//  Uses the Minkowski expansion + slab intersection method:
//    For each brush, maintain [enterT, exitT].
//    For each plane: expand by boxOffset, find entry/exit times.
//    If enterT < exitT and hull started outside: record hit.
//
//  FIX 11: startSolid correctly sets fraction=0, endPos=start.
//  Previously fraction stayed at 1.0, causing pass-through.
// -----------------------------------------------------------------------
TraceResult AABBPhysics::traceWorld(const Vec3& start, const Vec3& dir, float dist,
                                    const Vec3& mins, const Vec3& maxs)
{
    TraceResult r;
    r.fraction = 1.0f;
    r.endPos   = start + dir;

    if (!m_bsp || m_bsp->brushCount() == 0)
    {
        return r;
    }

    if (dist < kMinMoveDist)
    {
        r.endPos = start;
        return r;
    }

    const Vec3 center {
        start.x + (mins.x + maxs.x) * 0.5f,
        start.y + (mins.y + maxs.y) * 0.5f,
        start.z + (mins.z + maxs.z) * 0.5f };
    const Vec3 extents {
        (maxs.x - mins.x) * 0.5f,
        (maxs.y - mins.y) * 0.5f,
        (maxs.z - mins.z) * 0.5f };
    const Vec3 moveDir = dir * (1.0f / dist);

    float bestT = dist;
    Vec3  bestN = Vec3::zero();

    for (int brushIdx = 0; brushIdx < m_bsp->brushCount(); ++brushIdx)
    {
        const BSPBrush& brush = m_bsp->brushes()[brushIdx];
        if (!(brush.contents & kSolidMask)) continue;   // FIX 14

        float enterT = 0.0f;
        float exitT  = dist;
        Vec3  enterN = Vec3::zero();
        bool  startsOutside = false;

        for (uint32_t si = brush.firstBrushSide;
             si < brush.firstBrushSide + brush.numBrushSides; ++si)
        {
            if (si >= (uint32_t)m_bsp->brushSideCount()) break;
            const BSPBrushSide& bs = m_bsp->brushSides()[si];
            if (bs.plane >= (uint16_t)m_bsp->planeCount()) continue;

            const BSPPlane& plane = m_bsp->planes()[bs.plane];
            const Vec3& n = plane.normal;

            float boxOff = std::abs(n.x*extents.x) + std::abs(n.y*extents.y) + std::abs(n.z*extents.z);
            float d0     = n.x*center.x + n.y*center.y + n.z*center.z - plane.dist;
            float dd     = n.x*moveDir.x + n.y*moveDir.y + n.z*moveDir.z;

            // nearD0: signed distance of the hull's near-face from the plane.
            // Positive  → hull is outside this plane's half-space.
            // Negative  → hull is inside this plane's half-space.
            float nearD0 = d0 - boxOff;

            if (nearD0 > 0.0f)
                startsOutside = true;

            if (std::abs(dd) < 1e-7f)
            {
                // Parallel to plane.
                if (nearD0 > 0.0f) { exitT = -1.0f; break; }  // never enters
                continue;
            }

            float t = -nearD0 / dd;
            if (dd < 0.0f)            // entering the solid half-space
            {
                if (t > enterT) { enterT = t; enterN = n; }
            }
            else                      // leaving the solid half-space
            {
                if (t < exitT) exitT = t;
            }
        }

        if (exitT < 0.0f || enterT >= exitT) continue;

        if (!startsOutside)
        {
            // FIX 11: Hull started inside this brush.
            // Mark startSolid but do NOT register a forward hit.
            // After the loop we will clamp fraction to 0.
            r.startSolid = true;
            r.allSolid   = true;   // conservatively mark allSolid too
            continue;
        }

        if (enterT < bestT && enterT >= 0.0f)
        {
            bestT = enterT - kClipEpsilon;
            if (bestT < 0.0f) bestT = 0.0f;
            bestN = enterN;
        }
    }

    // FIX 11: If we are inside solid and found no outward collision,
    // return fraction=0 / endPos=start so the caller does NOT move the
    // entity. The old code fell through here with fraction=1.0 and
    // endPos=start+dir, teleporting the player through solid geometry.
    if (r.startSolid && bestT >= dist)
    {
        r.fraction = 0.0f;
        r.endPos   = start;
        r.normal   = Vec3::zero();
        return r;
    }

    if (bestT < dist)
    {
        r.fraction = bestT / dist;
        r.endPos   = start + moveDir * bestT;
        r.normal   = bestN;
    }
    return r;
}

// -----------------------------------------------------------------------
//  trace
// -----------------------------------------------------------------------
TraceResult AABBPhysics::trace(const Vec3& start, const Vec3& end,
                               const Vec3& mins, const Vec3& maxs)
{
    Vec3  delta = end - start;
    float dist  = delta.length();
    if (dist < kMinMoveDist)
    {
        TraceResult r; r.endPos = start; r.fraction = 1.0f; return r;
    }
    return traceWorld(start, delta, dist, mins, maxs);
}

TraceResult AABBPhysics::traceEntity(EntityHandle, const Vec3& start, const Vec3& end,
                                     const Vec3& mins, const Vec3& maxs)
{
    return trace(start, end, mins, maxs);
}

// -----------------------------------------------------------------------
//  isOnGround / getGroundEntity / getGroundElevation
// -----------------------------------------------------------------------
bool AABBPhysics::isOnGround(EntityHandle e)
{
    if (!m_entOrigin || !isValidEntityIndex(e, m_entCount)) return false;
    const Vec3& o = m_entOrigin[e.index()];
    TraceResult tr = trace(o, { o.x, o.y - 4.0f, o.z }, m_playerMins, m_playerMaxs);
    return tr.fraction < 1.0f && tr.normal.y > 0.7f;
}

EntityHandle AABBPhysics::getGroundEntity(EntityHandle e)
{
    return isOnGround(e) ? e : EntityHandle();
}

float AABBPhysics::getGroundElevation(EntityHandle e)
{
    if (!m_entOrigin || !isValidEntityIndex(e, m_entCount)) return -1e10f;
    const Vec3& o = m_entOrigin[e.index()];
    TraceResult tr = trace(o, { o.x, o.y - 400.0f, o.z }, m_playerMins, m_playerMaxs);
    return (tr.fraction < 1.0f) ? tr.endPos.y : o.y - 400.0f;
}

// -----------------------------------------------------------------------
//  step
// -----------------------------------------------------------------------
void AABBPhysics::step(float dt)
{
    if (!m_entOrigin || !m_entVelocity || !m_bsp || m_entCount == 0) return;

    Vec3& vel = m_entVelocity[0];
    vel.y -= m_gravity * dt;

    Vec3 pos = m_entOrigin[0];
    TraceResult gnd = traceWorld(pos, {0,-1,0}, 3.0f, m_playerMins, m_playerMaxs);
    bool grounded = (gnd.fraction < 1.0f && gnd.normal.y > 0.7f);

    if (!grounded)
    {
        float drag = std::pow(1.0f - 2.0f / 60.0f, dt * 60.0f);
        vel.x *= drag; vel.z *= drag;
    }
    else
    {
        if (vel.y < 0.0f) vel.y = 0.0f;
        float spd = std::sqrt(vel.x*vel.x + vel.z*vel.z);
        if (spd > 1.0f)
        {
            float drop  = spd * 6.0f * dt;
            float scale = std::max(0.0f, (spd - drop) / spd);
            vel.x *= scale; vel.z *= scale;
        }
        else { vel.x = vel.z = 0.0f; }
    }
}

// -----------------------------------------------------------------------
//  moveSlide — Quake-style slide-move with step-up and crease fix
//
//  FIX 12: step-up now traces upward before the lateral step trace.
//  FIX 13: slide loop breaks immediately on startSolid.
// -----------------------------------------------------------------------
TraceResult AABBPhysics::moveSlide(EntityHandle e, const Vec3& wishVel,
                                   float dt, float wishSpeed)
{
    TraceResult result;
    if (!m_entOrigin || !m_entVelocity || !isValidEntityIndex(e, m_entCount)) return result;

    Vec3 pos = m_entOrigin[e.index()];
    Vec3 vel = m_entVelocity[e.index()];

    // Optional Q2-style acceleration blend
    if (wishSpeed > 0.0f && wishVel.lengthSq() > kMinVelocitySq)
    {
        Vec3  wDir   = wishVel.normalized();
        float curSpd = vel.dot(wDir);
        float addSpd = wishSpeed - curSpd;
        if (addSpd > 0.0f)
        {
            float accelSpd = 10.0f * wishSpeed * dt;
            if (accelSpd > addSpd) accelSpd = addSpd;
            vel = vel + wDir * accelSpd;
        }
    }

    // ---- Step-up -------------------------------------------------------
    //
    // FIX 12: Must trace UPWARD to stepPos before doing the lateral step
    // trace. The old code hard-set stepPos = pos + {0,kStepHeight,0} and
    // did a lateral trace from there without verifying the raised position
    // was clear. If that position was inside a ceiling brush, traceWorld()
    // returned startSolid → (bug 11) fraction=1.0 → stepTr.fraction was
    // always > groundTr.fraction → step accepted → player placed inside
    // ceiling → every subsequent trace startSolid → infinite fall-through.
    //
    // Fix:
    //   1. Trace UP from pos toward pos+{0,kStepHeight,0}.
    //   2. Use the actual clear stepPos (upTr.endPos).
    //   3. If the vertical trace is blocked (upTr.fraction < epsilon),
    //      skip the step-up entirely (no room above).
    // --------------------------------------------------------------------
    Vec3  delta    = vel * dt;
    float moveDist = delta.length();
    float stepRemain = 1.0f;

    if (moveDist > kMinMoveDist)
    {
        TraceResult groundTr = traceWorld(pos, delta, moveDist, m_playerMins, m_playerMaxs);
        if (groundTr.fraction < 1.0f && isOnGround(e))
        {
            // FIX 12 step 1: verify upward clearance
            Vec3 stepTarget { pos.x, pos.y + kStepHeight, pos.z };
            TraceResult upTr = traceWorld(pos,
                                          { 0.f, kStepHeight, 0.f },
                                          kStepHeight,
                                          m_playerMins, m_playerMaxs);

            // Only proceed if we actually cleared some upward space
            if (!upTr.startSolid && upTr.fraction > 0.01f)
            {
                // FIX 12 step 2: use actual clear position
                Vec3 stepPos = upTr.endPos;

                TraceResult stepTr = traceWorld(stepPos, delta, moveDist,
                                                m_playerMins, m_playerMaxs);

                // FIX 12 step 3: also reject if the step trace itself started solid
                if (!stepTr.startSolid &&
                    stepTr.fraction > groundTr.fraction + 0.01f)
                {
                    Vec3 topPos   = stepPos + delta * stepTr.fraction;
                    Vec3 downEnd  { topPos.x, topPos.y - (kStepHeight + 2.0f), topPos.z };
                    TraceResult downTr = trace(topPos, downEnd, m_playerMins, m_playerMaxs);

                    if (!downTr.startSolid &&
                        downTr.fraction < 1.0f && downTr.normal.y > 0.7f)
                    {
                        pos = downTr.endPos;
                        if (vel.y < 0.0f) vel.y = 0.0f;
                        stepRemain = 1.0f - stepTr.fraction;
                    }
                }
            }
        }
    }

    // ---- Slide loop ----------------------------------------------------
    {
        Vec3  slideVel  = vel;
        float remainFrac = stepRemain;
        Vec3  clipNorm1  = Vec3::zero();
        bool  hasNorm1   = false;

        for (int iter = 0; iter < kMaxSlideIter; ++iter)
        {
            if (slideVel.lengthSq() < kMinVelocitySq || remainFrac < kClipEpsilon)
                break;

            Vec3  d    = slideVel * (dt * remainFrac);
            float dist = d.length();
            if (dist < kMinMoveDist) break;

            TraceResult tr = traceWorld(pos, d, dist, m_playerMins, m_playerMaxs);

            // FIX 13: explicit startSolid break.
            // tr.fraction = 0, tr.normal = zero. Old code would
            // remainFrac *= (1-0) = unchanged, then break on zero-normal —
            // but only after a spurious push. Break immediately.
            if (tr.startSolid)
            {
                // Don't move. Zero velocity to prevent tunnelling next frame.
                slideVel = Vec3::zero();
                break;
            }

            pos = tr.endPos;
            remainFrac *= (1.0f - tr.fraction);

            if (tr.fraction >= 1.0f - kClipEpsilon) break;  // moved freely

            // Guard against degenerate zero-length normals
            if (tr.normal.lengthSq() < 0.5f) break;

            float vDotN = slideVel.dot(tr.normal);
            if (vDotN >= 0.0f)
            {
                // Already separating — push out and continue
                pos = pos + tr.normal * kPenetrationSlack;
                continue;
            }

            if (!hasNorm1)
            {
                slideVel  = slideVel - tr.normal * vDotN;
                pos       = pos + tr.normal * kPenetrationSlack;
                clipNorm1 = tr.normal;
                hasNorm1  = true;
            }
            else
            {
                // Crease / edge: project onto the intersection edge
                float normDot = clipNorm1.dot(tr.normal);
                if (normDot > 0.99f)
                {
                    slideVel = slideVel - tr.normal * vDotN;
                    pos      = pos + tr.normal * kPenetrationSlack;
                }
                else
                {
                    Vec3  edge    = clipNorm1.cross(tr.normal).normalized();
                    float edgeSpd = slideVel.dot(edge);
                    slideVel = edge * edgeSpd;
                    if (slideVel.dot(clipNorm1) < 0.0f || slideVel.dot(tr.normal) < 0.0f)
                        slideVel = Vec3::zero();
                    pos = pos + tr.normal * kPenetrationSlack;
                }
            }

            // Kill velocity into ceilings
            if (tr.normal.y < -0.1f && slideVel.y > 0.0f)
                slideVel.y = 0.0f;
        }

        vel = slideVel;
    }

    m_entOrigin[e.index()]   = pos;
    m_entVelocity[e.index()] = vel;

    result.endPos   = pos;
    result.fraction = 1.0f;
    result.normal   = vel.lengthSq() > kMinVelocitySq ? vel.normalized() : Vec3::zero();
    return result;
}

} // namespace nova
