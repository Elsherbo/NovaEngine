// ============================================================
// FILE:    engine/core/math/mat4.h
// MODULE:  Core > Math
// PHASE:   1
// STATUS:  IN_PROGRESS
// PURPOSE: Column-major 4x4 float matrix. Perspective, ortho,
//          lookAt, and basic transform constructors included.
// DEPENDS: core/math/vec.h
// ============================================================
#pragma once

#include "engine/core/math/math.h"
#include "engine/core/math/vec.h"
#include <cmath>
#include <cstring>

namespace nova
{

    struct alignas(16) Mat4
    {
        // Stored column-major: col[c][r]
        float col[4][4] = {};

        Mat4() = default;

        // Identity
        static Mat4 identity()
        {
            Mat4 m;
            m.col[0][0] = 1;
            m.col[1][1] = 1;
            m.col[2][2] = 1;
            m.col[3][3] = 1;
            return m;
        }

        // Element access (column, row)
        float &at(int c, int r) { return col[c][r]; }
        float at(int c, int r) const { return col[c][r]; }

        // Multiply
        Mat4 operator*(const Mat4 &b) const
        {
            Mat4 r{};
            for (int c = 0; c < 4; ++c)
            {
                for (int r_i = 0; r_i < 4; ++r_i)
                {
                    r.col[c][r_i] =
                        col[0][r_i] * b.col[c][0] +
                        col[1][r_i] * b.col[c][1] +
                        col[2][r_i] * b.col[c][2] +
                        col[3][r_i] * b.col[c][3];
                }
            }
            return r;
        }

        // Transform Vec4
        Vec4 operator*(const Vec4 &v) const
        {
            return {
                col[0][0] * v.x + col[1][0] * v.y + col[2][0] * v.z + col[3][0] * v.w,
                col[0][1] * v.x + col[1][1] * v.y + col[2][1] * v.z + col[3][1] * v.w,
                col[0][2] * v.x + col[1][2] * v.y + col[2][2] * v.z + col[3][2] * v.w,
                col[0][3] * v.x + col[1][3] * v.y + col[2][3] * v.z + col[3][3] * v.w,
            };
        }

        // Transform point (w=1)
        Vec3 transformPoint(const Vec3 &v) const
        {
            Vec4 r = *this * Vec4(v, 1.f);
            return r.xyz() * (1.f / r.w);
        }

        // Transform direction (w=0, ignores translation)
        Vec3 transformDir(const Vec3 &v) const
        {
            return (*this * Vec4(v, 0.f)).xyz();
        }

        // Transpose
        Mat4 transposed() const
        {
            Mat4 r;
            for (int c = 0; c < 4; ++c)
                for (int row = 0; row < 4; ++row)
                    r.col[row][c] = col[c][row];
            return r;
        }

        // Raw pointer for OpenGL upload (column-major already — no conversion needed)
        const float *data() const { return &col[0][0]; }

        float determinant() const
        {
            const float *m = &col[0][0];

            return m[12] * m[9] * m[6] * m[3] - m[8] * m[13] * m[6] * m[3] -
                   m[12] * m[5] * m[10] * m[3] + m[4] * m[13] * m[10] * m[3] +
                   m[8] * m[5] * m[14] * m[3] - m[4] * m[9] * m[14] * m[3] -
                   m[12] * m[9] * m[2] * m[7] + m[8] * m[13] * m[2] * m[7] +
                   m[12] * m[1] * m[10] * m[7] - m[0] * m[13] * m[10] * m[7] -
                   m[8] * m[1] * m[14] * m[7] + m[0] * m[9] * m[14] * m[7] +
                   m[12] * m[5] * m[2] * m[11] - m[4] * m[13] * m[2] * m[11] -
                   m[12] * m[1] * m[6] * m[11] + m[0] * m[13] * m[6] * m[11] +
                   m[4] * m[1] * m[14] * m[11] - m[0] * m[5] * m[14] * m[11] -
                   m[8] * m[5] * m[2] * m[15] + m[4] * m[9] * m[2] * m[15] +
                   m[8] * m[1] * m[6] * m[15] - m[0] * m[9] * m[6] * m[15] -
                   m[4] * m[1] * m[10] * m[15] + m[0] * m[5] * m[10] * m[15];
        }

        Mat4 inverseAffine() const
        {
            Mat4 r;

            // rotation 3x3 transpose (inverse of rotation)
            r.col[0][0] = col[0][0];
            r.col[0][1] = col[1][0];
            r.col[0][2] = col[2][0];

            r.col[1][0] = col[0][1];
            r.col[1][1] = col[1][1];
            r.col[1][2] = col[2][1];

            r.col[2][0] = col[0][2];
            r.col[2][1] = col[1][2];
            r.col[2][2] = col[2][2];

            r.col[3][3] = 1.0f;

            // inverse translation
            Vec3 t(col[3][0], col[3][1], col[3][2]);
            Vec3 invT = r.transformDir(-t);

            r.col[3][0] = invT.x;
            r.col[3][1] = invT.y;
            r.col[3][2] = invT.z;

            return r;
        }

        // ---- Common constructors --------------------------------

        static Mat4 translate(const Vec3 &t)
        {
            Mat4 m = identity();
            m.col[3][0] = t.x;
            m.col[3][1] = t.y;
            m.col[3][2] = t.z;
            return m;
        }

        static Mat4 scale(const Vec3 &s)
        {
            Mat4 m = identity();
            m.col[0][0] = s.x;
            m.col[1][1] = s.y;
            m.col[2][2] = s.z;
            return m;
        }

        // Rotate by angle (radians) around an arbitrary axis
        static Mat4 rotate(float radians, Vec3 axis)
        {
            axis = axis.normalized();
            float c = std::cos(radians), s = std::sin(radians), t = 1.f - c;
            float x = axis.x, y = axis.y, z = axis.z;
            Mat4 m = identity();
            m.col[0][0] = t * x * x + c;
            m.col[1][0] = t * x * y - s * z;
            m.col[2][0] = t * x * z + s * y;
            m.col[0][1] = t * x * y + s * z;
            m.col[1][1] = t * y * y + c;
            m.col[2][1] = t * y * z - s * x;
            m.col[0][2] = t * x * z - s * y;
            m.col[1][2] = t * y * z + s * x;
            m.col[2][2] = t * z * z + c;
            return m;
        }

        // Perspective projection (right-handed, depth [-1, 1] for OpenGL)
        // Use this when glClipControl is NOT active (standard OpenGL default).
        static Mat4 perspective(float fovYRad, float aspect, float nearZ, float farZ)
        {
            float tanHalf = std::tan(fovYRad * 0.5f);
            Mat4 m;
            m.col[0][0] = 1.f / (aspect * tanHalf);
            m.col[1][1] = 1.f / tanHalf;
            m.col[2][2] = -(farZ + nearZ) / (farZ - nearZ);
            m.col[2][3] = -1.f;
            m.col[3][2] = -(2.f * farZ * nearZ) / (farZ - nearZ);
            return m;
        }

        // Reversed-Z perspective projection for [0, 1] clip space.
        //
        // Requires glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE) and
        // glDepthFunc(GL_GREATER) with glClearDepth(0.0).
        //
        // Why bother? Standard [0,1] depth maps:
        //   near  →  0.0   (most float precision lives near 0)
        //   far   →  1.0   (least precision — far objects z-fight)
        //
        // Reversed-Z flips that:
        //   near  →  1.0
        //   far   →  0.0   (far objects now get the dense float exponent range)
        //
        // With near=2, far=4096 the standard buffer has ~10 bits of precision
        // past 1000 units. Reversed-Z gives ~22 bits there. Z-fighting in large
        // Q2 maps is essentially eliminated without changing near/far at all.
        //
        // Matrix derivation (right-handed, clip w = -z_view):
        //   NDC_z = (A * z_view + B) / (-z_view)
        //   near → 1:  A = n/(f-n),  B = nf/(f-n)
        static Mat4 perspectiveReverseZ(float fovYRad, float aspect, float nearZ, float farZ)
        {
            float tanHalf = std::tan(fovYRad * 0.5f);
            Mat4 m;
            m.col[0][0] =  1.f / (aspect * tanHalf);
            m.col[1][1] =  1.f / tanHalf;
            m.col[2][2] =  nearZ / (farZ - nearZ);           // n/(f-n)
            m.col[2][3] = -1.f;
            m.col[3][2] =  (nearZ * farZ) / (farZ - nearZ);  // nf/(f-n)
            return m;
        }

        // Orthographic projection
        static Mat4 ortho(float left, float right, float bottom, float top, float nearZ, float farZ)
        {
            Mat4 m = identity();
            m.col[0][0] = 2.f / (right - left);
            m.col[1][1] = 2.f / (top - bottom);
            m.col[2][2] = -2.f / (farZ - nearZ);
            m.col[3][0] = -(right + left) / (right - left);
            m.col[3][1] = -(top + bottom) / (top - bottom);
            m.col[3][2] = -(farZ + nearZ) / (farZ - nearZ);
            return m;
        }

        // LookAt (right-handed, camera at eye looking at center)
        static Mat4 lookAt(const Vec3 &eye, const Vec3 &center, const Vec3 &up)
        {
            Vec3 f = (center - eye).normalized();
            Vec3 r = f.cross(up);
            if (r.lengthSq() < 1e-6f)
                r = Vec3::right(); // fallback
            r = r.normalized();
            Vec3 u = r.cross(f);
            Mat4 m = identity();
            m.col[0][0] = r.x;
            m.col[1][0] = r.y;
            m.col[2][0] = r.z;
            m.col[0][1] = u.x;
            m.col[1][1] = u.y;
            m.col[2][1] = u.z;
            m.col[0][2] = -f.x;
            m.col[1][2] = -f.y;
            m.col[2][2] = -f.z;
            m.col[3][0] = -r.dot(eye);
            m.col[3][1] = -u.dot(eye);
            m.col[3][2] = f.dot(eye);
            return m;
        }
    };

} // namespace nova
