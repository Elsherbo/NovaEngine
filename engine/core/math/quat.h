// ============================================================
// FILE:    engine/core/math/quat.h
// MODULE:  Core > Math
// PHASE:   1
// STATUS:  IN_PROGRESS
// PURPOSE: Unit quaternion for entity rotation. Slerp for
//          smooth interpolation. Converts to/from Mat4.
// DEPENDS: core/math/vec.h, core/math/mat4.h
// ============================================================
#pragma once

#include "engine/core/math/math.h"
#include "engine/core/math/vec.h"
#include "engine/core/math/mat4.h"
#include <cmath>

namespace nova
{

    struct Quat
    {
        float x = 0.f, y = 0.f, z = 0.f, w = 1.f; // identity

        Quat() = default;
        constexpr Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

        static Quat identity() { return {0, 0, 0, 1}; }

        // Construct from axis-angle (axis must be normalized, angle in radians)
        static Quat fromAxisAngle(const Vec3 &axis, float rad)
        {
            float s = std::sin(rad * 0.5f);
            return {axis.x * s, axis.y * s, axis.z * s, std::cos(rad * 0.5f)};
        }

        // Construct from Euler angles (pitch, yaw, roll in radians, applied YXZ)
        static Quat fromEuler(float pitch, float yaw, float roll)
        {
            Quat qP = fromAxisAngle({1, 0, 0}, pitch);
            Quat qY = fromAxisAngle({0, 1, 0}, yaw);
            Quat qR = fromAxisAngle({0, 0, 1}, roll);
            return qY * qP * qR;
        }

        // Hamilton product (does NOT normalize - caller must ensure unit quaternions if needed)
        Quat operator*(const Quat &b) const
        {
            return {
                w * b.x + x * b.w + y * b.z - z * b.y,
                w * b.y - x * b.z + y * b.w + z * b.x,
                w * b.z + x * b.y - y * b.x + z * b.w,
                w * b.w - x * b.x - y * b.y - z * b.z};
        }

        Quat conjugate() const { return {-x, -y, -z, w}; }

        float lengthSq() const { return x * x + y * y + z * z + w * w; }
        Quat normalized() const
        {
            float l = std::sqrt(lengthSq());
            if (l < 1e-8f)
                return identity();
            float inv = 1.0f / l;
            return {x * inv, y * inv, z * inv, w * inv};
        }

        // Rotate a vector (normalizes if needed to prevent drift)
        Vec3 rotate(const Vec3 &v) const
        {
            float lSq = lengthSq();
            if (std::abs(lSq - 1.0f) > 1e-6f)
            {
                Quat n = normalized();
                return n.rotate(v);
            }
            Quat p = {v.x, v.y, v.z, 0.f};
            Quat r = (*this) * p * conjugate();
            return {r.x, r.y, r.z};
        }

        // Convert to rotation matrix (assumes unit quaternion)
        Mat4 toMat4() const
        {
            Mat4 m = Mat4::identity();
            float xx = x * x, yy = y * y, zz = z * z;
            float xy = x * y, xz = x * z, yz = y * z;
            float wx = w * x, wy = w * y, wz = w * z;
            m.col[0][0] = 1 - 2 * (yy + zz);
            m.col[1][0] = 2 * (xy - wz);
            m.col[2][0] = 2 * (xz + wy);
            m.col[0][1] = 2 * (xy + wz);
            m.col[1][1] = 1 - 2 * (xx + zz);
            m.col[2][1] = 2 * (yz - wx);
            m.col[0][2] = 2 * (xz - wy);
            m.col[1][2] = 2 * (yz + wx);
            m.col[2][2] = 1 - 2 * (xx + yy);
            return m;
        }

        // Spherical linear interpolation
        static Quat slerp(Quat a, Quat b, float t)
        {
            float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            if (dot < 0.f)
            {
                b = {-b.x, -b.y, -b.z, -b.w};
                dot = -dot;
            }
            if (dot > 0.9995f)
            {
                // Linear fallback when nearly identical
                Quat r = {a.x + t * (b.x - a.x), a.y + t * (b.y - a.y),
                          a.z + t * (b.z - a.z), a.w + t * (b.w - a.w)};
                return r.normalized();
            }
            float theta0 = std::acos(dot);
            float theta = theta0 * t;
            float sa = std::sin(theta0);
            float s0 = std::cos(theta) - dot * std::sin(theta) / sa;
            float s1 = std::sin(theta) / sa;
            return {s0 * a.x + s1 * b.x, s0 * a.y + s1 * b.y,
                    s0 * a.z + s1 * b.z, s0 * a.w + s1 * b.w};
        }
    };

} // namespace nova
