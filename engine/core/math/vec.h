// ============================================================
// FILE:    engine/core/math/vec.h
// MODULE:  Core > Math
// PHASE:   1
// STATUS:  DONE
// PURPOSE: Vec2, Vec3, Vec4 — float-based, 16-byte aligned for
//          future SIMD. No external dependencies.
// DEPENDS: (none)
// ============================================================
#pragma once

#include "engine/core/math/math.h"
#include <cmath>
#include <cassert>

namespace nova
{

    // ---- Vec2 --------------------------------------------------
    struct Vec2
    {
        float x = 0.f, y = 0.f;

        Vec2() = default;
        constexpr Vec2(float x, float y) : x(x), y(y) {}

        Vec2 operator+(const Vec2 &o) const { return {x + o.x, y + o.y}; }
        Vec2 operator-(const Vec2 &o) const { return {x - o.x, y - o.y}; }
        Vec2 operator*(float s) const { return {x * s, y * s}; }
        Vec2 operator/(float s) const
        {
            assert(s != 0.f);
            return {x / s, y / s};
        }
        Vec2 &operator+=(const Vec2 &o)
        {
            x += o.x;
            y += o.y;
            return *this;
        }
        Vec2 &operator-=(const Vec2 &o)
        {
            x -= o.x;
            y -= o.y;
            return *this;
        }
        Vec2 &operator*=(float s)
        {
            x *= s;
            y *= s;
            return *this;
        }
        Vec2 &operator/=(float s)
        {
            assert(s != 0.f);
            x /= s;
            y /= s;
            return *this;
        }

        bool operator==(const Vec2 &o) const { return x == o.x && y == o.y; }
        bool operator!=(const Vec2 &o) const { return !(*this == o); }

        float length() const { return std::sqrt(x * x + y * y); }
        float lengthSq() const { return x * x + y * y; }
        Vec2 normalized() const
        {
            float l = length();
            if (l < 1e-8f)
                return Vec2::zero();
            return *this / l;
        }
        float dot(const Vec2 &o) const { return x * o.x + y * o.y; }

        static constexpr Vec2 zero() { return {0, 0}; }
    };

    // ---- Vec3 --------------------------------------------------
    struct alignas(16) Vec3
    {
        float x = 0.f, y = 0.f, z = 0.f;
        float _pad = 0.f; // 16-byte alignment pad; not exposed to callers

        Vec3() = default;
        constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z), _pad(0.f) {}

        Vec3 operator+(const Vec3 &o) const { return {x + o.x, y + o.y, z + o.z}; }
        Vec3 operator-(const Vec3 &o) const { return {x - o.x, y - o.y, z - o.z}; }
        Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
        Vec3 operator/(float s) const
        {
            assert(s != 0.f);
            return {x / s, y / s, z / s};
        }
        Vec3 operator-() const { return {-x, -y, -z}; }
        Vec3 &operator+=(const Vec3 &o)
        {
            x += o.x;
            y += o.y;
            z += o.z;
            return *this;
        }
        Vec3 &operator-=(const Vec3 &o)
        {
            x -= o.x;
            y -= o.y;
            z -= o.z;
            return *this;
        }
        Vec3 &operator*=(float s)
        {
            x *= s;
            y *= s;
            z *= s;
            return *this;
        }
        Vec3 &operator/=(float s)
        {
            assert(s != 0.f);
            x /= s;
            y /= s;
            z /= s;
            return *this;
        }

        bool operator==(const Vec3 &o) const { return x == o.x && y == o.y && z == o.z; }
        bool operator!=(const Vec3 &o) const { return !(*this == o); }

        float length() const { return std::sqrt(x * x + y * y + z * z); }
        float lengthSq() const { return x * x + y * y + z * z; }
        Vec3 normalized() const
        {
            float l = length();
            if (l < 1e-8f)
                return Vec3::zero();
            return *this / l;
        }
        float dot(const Vec3 &o) const { return x * o.x + y * o.y + z * o.z; }
        Vec3 cross(const Vec3 &o) const
        {
            return {y * o.z - z * o.y,
                    z * o.x - x * o.z,
                    x * o.y - y * o.x};
        }

        // Common constants
        static constexpr Vec3 zero() { return {0, 0, 0}; }
        static constexpr Vec3 up() { return {0, 1, 0}; }
        static constexpr Vec3 forward() { return {0, 0, -1}; }
        static constexpr Vec3 right() { return {1, 0, 0}; }
    };

    inline Vec3 operator*(float s, const Vec3 &v) { return v * s; }

    // ---- Vec4 --------------------------------------------------
    struct alignas(16) Vec4
    {
        float x = 0.f, y = 0.f, z = 0.f, w = 0.f;

        Vec4() = default;
        constexpr Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
        explicit Vec4(const Vec3 &v, float w = 1.f) : x(v.x), y(v.y), z(v.z), w(w) {}

        Vec4 operator+(const Vec4 &o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
        Vec4 operator*(float s) const { return {x * s, y * s, z * s, w * s}; }
        Vec4 &operator+=(const Vec4 &o)
        {
            x += o.x;
            y += o.y;
            z += o.z;
            w += o.w;
            return *this;
        }

        Vec3 xyz() const { return {x, y, z}; }
        float dot(const Vec4 &o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
    };

} // namespace nova
