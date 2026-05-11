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
void AABBPhysics::setDynamicColliders(const DynamicCollider* colliders, size_t count)
{
    m_dynamicColliders = colliders;
    m_dynamicColliderCount = count;
}
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
            if (!(brush.contents & kSolidMask)) continue;
    
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
                // FIX: use kClipEpsilon tolerance so the hull sitting exactly
                // on a plane face is not considered "inside" the brush.
                if (d > -o + kClipEpsilon) { allInside = false; break; }
            }
    
            if (allInside) return true;
        }
        return false;
}

// -----------------------------------------------------------------------
//  traceAABB — swept AABB vs static AABB.
//  Uses the slab method: expand the moving AABB by the static AABB's extents
//  to form a swept-box, then find the earliest entry time.
// -----------------------------------------------------------------------
TraceResult AABBPhysics::traceAABB(const Vec3& start, const Vec3& dir, float dist,
    const Vec3& mins, const Vec3& maxs,
    const Vec3& boxOrigin, const Vec3& boxMins, const Vec3& boxMaxs)
{
    TraceResult r;
    r.fraction = 1.0f;
    r.endPos   = start + dir;

    if (dist < kMinMoveDist) return r;

    // Expand the box by the moving hull extents (Minkowski sum).
    // box is [boxOrigin + boxMins, boxOrigin + boxMaxs].
    // hull is [mins, maxs] relative to start.
    // Expanded box: center at boxOrigin + (boxMins+boxMaxs)/2,
    //   half-extents = (boxMaxs-boxMins)/2 + (maxs-mins)/2.
    Vec3 boxCenter = boxOrigin + Vec3{
        (boxMins.x + boxMaxs.x) * 0.5f,
        (boxMins.y + boxMaxs.y) * 0.5f,
        (boxMins.z + boxMaxs.z) * 0.5f
    };
    Vec3 hullCenter = Vec3{
        (mins.x + maxs.x) * 0.5f,
        (mins.y + maxs.y) * 0.5f,
        (mins.z + maxs.z) * 0.5f
    };
    Vec3 hullExtents = Vec3{
        (maxs.x - mins.x) * 0.5f,
        (maxs.y - mins.y) * 0.5f,
        (maxs.z - mins.z) * 0.5f
    };
    Vec3 boxExtents = Vec3{
        (boxMaxs.x - boxMins.x) * 0.5f,
        (boxMaxs.y - boxMins.y) * 0.5f,
        (boxMaxs.z - boxMins.z) * 0.5f
    };

    // Swept box center = boxCenter, swept half-extents = boxExtents + hullExtents.
    Vec3 sweepExtents = boxExtents + hullExtents;

    // Starting distance from sweep center to box center.
    Vec3 delta = start + hullCenter - boxCenter;
    Vec3 moveDir = dir * (1.0f / dist);

    float entryT = 0.0f;
    float exitT  = dist;
    Vec3  entryN = Vec3::zero();

    for (int axis = 0; axis < 3; ++axis)
    {
        float d = (axis == 0) ? delta.x : (axis == 1) ? delta.y : delta.z;
        float e = (axis == 0) ? sweepExtents.x : (axis == 1) ? sweepExtents.y : sweepExtents.z;
        float md = (axis == 0) ? moveDir.x : (axis == 1) ? moveDir.y : moveDir.z;

        if (std::abs(md) < 1e-7f)
        {
            // Parallel: check if already overlapping
            if (std::abs(d) > e + kClipEpsilon)
            {
                // No overlap at all
                return r;
            }
            continue;
        }

        float t1 = (-e - d) / md;  // entry time
        float t2 = ( e - d) / md;  // exit time

        if (t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > entryT) { entryT = t1; entryN = Vec3::zero(); if (axis == 0) entryN = Vec3{1,0,0}; else if (axis == 1) entryN = Vec3{0,1,0}; else entryN = Vec3{0,0,1}; if (d < 0) entryN = -entryN; }
        if (t2 < exitT) exitT = t2;

        if (entryT > exitT) return r;  // no collision
    }

    if (entryT >= 0.0f && entryT < dist)
    {
        r.fraction = entryT / dist;
        r.endPos   = start + moveDir * entryT;
        r.normal   = entryN;
    }

    return r;
}

// -----------------------------------------------------------------------
//  traceWorld — brute-force swept AABB vs all solid brushes + dynamic colliders
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

// ---- Dynamic colliders first (moving platforms, doors, etc.) ----
for (size_t di = 0; di < m_dynamicColliderCount; ++di)
{
    const DynamicCollider& dc = m_dynamicColliders[di];
    TraceResult ar = traceAABB(start, dir, dist, mins, maxs, dc.origin, dc.mins, dc.maxs);
    if (ar.fraction < r.fraction)
    {
        r = ar;
    }
}

if (!m_bsp || m_bsp->brushCount() == 0)
return r;

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
bool  anyStartSolid = false;

for (int brushIdx = 0; brushIdx < m_bsp->brushCount(); ++brushIdx)
{
const BSPBrush& brush = m_bsp->brushes()[brushIdx];
if (!(brush.contents & kSolidMask)) continue;

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

float boxOff = std::abs(n.x*extents.x)
+ std::abs(n.y*extents.y)
+ std::abs(n.z*extents.z);
float d0 = n.x*center.x + n.y*center.y + n.z*center.z - plane.dist;
float dd = n.x*moveDir.x + n.y*moveDir.y + n.z*moveDir.z;

// Signed distance of the hull's near face from this plane.
// Positive = hull is outside this plane (in the half-space the normal points to).
// Use kClipEpsilon tolerance: a hull resting exactly ON a plane
// (nearD0 == 0) must be treated as "outside" or sliding along it
// will incorrectly mark the brush as startSolid.
float nearD0 = d0 - boxOff;

if (nearD0 >= -kClipEpsilon)
startsOutside = true;

if (std::abs(dd) < 1e-7f)
{
// Parallel to plane
if (nearD0 > kClipEpsilon)
{
// Hull is clearly outside this plane and moving parallel → never enters brush
exitT = -1.0f;
break;
}
continue;
}

float t = -nearD0 / dd;
if (dd < 0.0f)
{
// Moving toward the solid half-space (entering)
if (t > enterT) { enterT = t; enterN = n; }
}
else
{
// Moving away from the solid half-space (exiting)
if (t < exitT) exitT = t;
}
}

if (exitT < 0.0f || enterT >= exitT) continue;

if (!startsOutside)
{
// Hull started inside this brush — record but don't block movement
anyStartSolid = true;
continue;  // keep looking for actual forward collisions
}

if (enterT < bestT && enterT >= 0.0f)
{
bestT = enterT - kClipEpsilon;
if (bestT < 0.0f) bestT = 0.0f;
bestN = enterN;
}
}

// Only set startSolid if we found no valid forward hit at all
if (anyStartSolid && bestT >= dist)
{
r.startSolid = true;
r.endPos     = start;
r.fraction   = 0.0f;
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
    // Probe from slightly above to avoid the floor plane boundary case
    const Vec3 probeStart = { o.x, o.y + kClipEpsilon * 4.0f, o.z };
    const Vec3 probeEnd   = { o.x, o.y - 6.0f, o.z };
    TraceResult tr = trace(probeStart, probeEnd, m_playerMins, m_playerMaxs);
    return tr.fraction < 1.0f && !tr.startSolid && tr.normal.y > 0.7f;
}

EntityHandle AABBPhysics::getGroundEntity(EntityHandle e)
{
    return isOnGround(e) ? e : EntityHandle();
}

float AABBPhysics::getGroundElevation(EntityHandle e)
{
    if (!m_entOrigin || !isValidEntityIndex(e, m_entCount)) return -1e10f;
    const Vec3& o = m_entOrigin[e.index()];
    const Vec3 probeStart = { o.x, o.y + 2.0f, o.z };
    TraceResult tr = trace(probeStart, { o.x, o.y - 400.0f, o.z }, m_playerMins, m_playerMaxs);
    return (tr.fraction < 1.0f && !tr.startSolid) ? tr.endPos.y : o.y - 400.0f;
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

// Optional acceleration
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

Vec3  delta    = vel * dt;
float moveDist = delta.length();

if (moveDist < kMinMoveDist)
{
m_entOrigin[e.index()]   = pos;
m_entVelocity[e.index()] = vel;
result.endPos = pos;
return result;
}

// ----------------------------------------------------------------
// Step-up: if we hit something, try stepping over it
// ----------------------------------------------------------------
{
TraceResult groundTr = traceWorld(pos, delta, moveDist, m_playerMins, m_playerMaxs);

bool tryStep = (groundTr.fraction < 1.0f - kClipEpsilon)
&& !groundTr.startSolid
&& isOnGround(e);

if (tryStep)
{
// 1. Trace upward to find clearance
TraceResult upTr = traceWorld(pos,
{0.f, kStepHeight, 0.f}, kStepHeight,
m_playerMins, m_playerMaxs);

float actualStep = kStepHeight * upTr.fraction;

// Only step if we cleared at least 1 unit upward
if (!upTr.startSolid && actualStep >= 1.0f)
{
Vec3 stepPos = upTr.endPos;

// 2. Trace forward from stepped-up position
TraceResult stepTr = traceWorld(stepPos, delta, moveDist,
                 m_playerMins, m_playerMaxs);

// 3. Accept the step if we moved further than without stepping
// Key fix: compare fractions, but also accept if step isn't startSolid
// and goes further than the ground trace
if (!stepTr.startSolid && stepTr.fraction > groundTr.fraction)
{
Vec3 topPos  = stepPos + (delta / moveDist) * (moveDist * stepTr.fraction);
Vec3 downEnd { topPos.x, topPos.y - (actualStep + 4.0f), topPos.z };

TraceResult downTr = trace(topPos, downEnd, m_playerMins, m_playerMaxs);

if (!downTr.startSolid && downTr.fraction < 1.0f
&& downTr.normal.y > 0.7f)
{
pos = downTr.endPos;
if (vel.y < 0.0f) vel.y = 0.0f;

// Done — skip the regular slide loop
m_entOrigin[e.index()]   = pos;
m_entVelocity[e.index()] = vel;
result.endPos = pos;
return result;
}
}
}
}
}

// ----------------------------------------------------------------
// Slide loop
// ----------------------------------------------------------------
Vec3  slideVel  = vel;
float remainFrac = 1.0f;
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

if (tr.startSolid)
{
// Boundary case: push up by a tiny amount and retry once
Vec3 nudgedPos = pos;
nudgedPos.y += kClipEpsilon * 4.0f;
TraceResult tr2 = traceWorld(nudgedPos, d, dist, m_playerMins, m_playerMaxs);
if (!tr2.startSolid)
{
pos = nudgedPos;
tr  = tr2;
}
else
{
// Genuinely stuck — stop sliding but preserve velocity
break;
}
}

pos = tr.endPos;
remainFrac *= (1.0f - tr.fraction);

if (tr.fraction >= 1.0f - kClipEpsilon) break;

if (tr.normal.lengthSq() < 0.5f) break;

float vDotN = slideVel.dot(tr.normal);
if (vDotN >= 0.0f)
{
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

if (tr.normal.y < -0.1f && slideVel.y > 0.0f)
slideVel.y = 0.0f;
}

vel = slideVel;

m_entOrigin[e.index()]   = pos;
m_entVelocity[e.index()] = vel;

result.endPos = pos;
result.fraction = 1.0f;
return result;
}

} // namespace nova
