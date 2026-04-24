// ============================================================
// FILE:    engine/core/math/common.h
// MODULE:  Core > Math
// PHASE:   1
// STATUS:  IN_PROGRESS
// PURPOSE: Engine-wide math constants and scalar utilities.
//          Shared by all math headers.
// DEPENDS: (none)
// ============================================================
#pragma once

#include <cmath>
#include <cstddef>

namespace nova
{

// ---- Constants ---------------------------------------------
inline constexpr float kPi       = 3.14159265358979323846f;
inline constexpr float k2Pi      = 6.28318530717958647692f;
inline constexpr float kHalfPi   = 1.57079632679489661923f;
inline constexpr float kEpsilon  = 1e-6f;
inline constexpr float kEpsilonSq = kEpsilon * kEpsilon;

// ---- Scalar helpers ----------------------------------------
inline float toRadians(float degrees) { return degrees * (kPi / 180.f); }
inline float toDegrees(float radians) { return radians * (180.f / kPi); }

inline bool nearlyZero(float a, float eps = kEpsilon)
{
    return std::abs(a) < eps;
}

inline bool nearlyEqual(float a, float b, float eps = kEpsilon)
{
    return std::abs(a - b) < eps;
}

inline float clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

} // namespace nova
