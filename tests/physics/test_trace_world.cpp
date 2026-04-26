// Quick test: check BSP planes
#include <cstdio>
#include "engine/renderer/bsp/bsp.h"
#include "engine/physics/aabb_physics.h"

using namespace nova;

int main()
{
    BSPMap bsp;
    if (!bsp.load(nullptr, "D:\\Games\\Quake II Enhanced\\mapping\\spirit2dm2\\maps\\spirit2dm2.bsp"))
    {
        printf("FAIL\n");
        return 1;
    }

    printf("Loaded: %zu planes, %zu leaves\n", bsp.m_planes.size(), bsp.m_leaves.size());

    // Check first few planes
    for (size_t i = 0; i < std::min((size_t)5, bsp.m_planes.size()); ++i)
    {
        const auto& p = bsp.m_planes[i];
        printf("Plane %zu: n=(%.1f,%.1f,%.1f) d=%.1f\n",
               i, p.normal.x, p.normal.y, p.normal.z, p.dist);
    }

    // Check spawn
    Vec3 spawn = bsp.getSpawnOrigin();
    printf("Spawn: (%.1f, %.1f, %.1f)\n", spawn.x, spawn.y, spawn.z);

    AABBPhysics phys;
    phys.setWorld(&bsp);

    // Simple test: trace down
    Vec3 start = spawn;
    Vec3 down = {0, 0, -100};
    
    TraceResult tr = phys.trace(start, start + down, {-16, -16, -36}, {16, 16, 36});
    printf("Trace down: fraction=%.2f\n", tr.fraction);

    return 0;
}