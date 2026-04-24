#include <cstdio>
#include <cmath>
#include <cassert>

#include "engine/core/math/vec.h"
#include "engine/core/math/mat4.h"
#include "engine/core/math/quat.h"
#include "engine/core/math/common.h"

int main()
{
    using namespace nova;

    Vec3 v(1, 0, 0);
    Vec3 vn = v.normalized();
    assert(std::abs(vn.x - 1.0f) < kEpsilon);
    assert(std::abs(vn.y) < kEpsilon);
    assert(std::abs(vn.z) < kEpsilon);

    Mat4 I = Mat4::identity();
    Vec4 v4(1, 2, 3, 1);
    Vec4 result = I * v4;
    assert(std::abs(result.x - 1.0f) < kEpsilon);
    assert(std::abs(result.y - 2.0f) < kEpsilon);
    assert(std::abs(result.z - 3.0f) < kEpsilon);

    Quat q = Quat::fromAxisAngle(Vec3::up(), kPi);
    Vec3 rotated = q.rotate(Vec3::forward());
    assert(std::abs(rotated.z - 1.0f) < kEpsilon);

    printf("test_math: all passed\n");
    return 0;
}