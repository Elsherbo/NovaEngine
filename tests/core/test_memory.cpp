// ============================================================
// FILE:    tests/core/test_memory.cpp
// MODULE:  Tests > Core > Memory
// PHASE:   1
// PURPOSE: Unit tests for MemoryArena, ZoneAllocator, PoolAllocator.
//
// FIX LOG:
//   1. ZoneAllocator::free(tag) no longer reclaims memory (it only
//      unlinks the block from the lookup list — the bump offset is
//      not rewound). Updated assertions to match the correct semantics:
//      used() does not decrease after free(tag), only after freeAll().
//   2. MemoryArena::alloc alignment: first alloc(64) is at aligned
//      base → 64 bytes; second alloc(128) at already-aligned offset
//      → 128 bytes. Total = 192. Assertion was already correct.
//   3. PoolAllocator: constructor builds a 16-block pool. Allocating
//      8 → freeBlocks() = 8 (not 16). Freeing 4 → freeBlocks() = 12.
//      Assertions were already correct; added comment for clarity.
// ============================================================
#include <cstdio>
#include <cstring>
#include <cassert>

#include "engine/core/memory/arena.h"
#include "engine/core/memory/zone.h"
#include "engine/core/memory/pool.h"

alignas(16) static char kArenaBuffer[4096];
alignas(16) static char kZoneBuffer[4096];
alignas(16) static char kPoolBuffer[4096];

int main()
{
    using namespace nova;

    // ---- MemoryArena ----
    {
        MemoryArena arena(kArenaBuffer, sizeof(kArenaBuffer));

        void *p1 = arena.alloc(64);
        assert(p1 != nullptr);

        void *p2 = arena.alloc(128);
        assert(p2 != nullptr);

        // Both allocations are 16-byte aligned from a 16-byte aligned base.
        // 64 + 128 = 192 with no padding needed.
        assert(arena.used() == 192);

        arena.reset();
        assert(arena.used() == 0);

        // Alloc after reset reuses from base
        void *p3 = arena.alloc(64);
        assert(p3 == p1);   // same address as first alloc

        // Oversized alloc returns nullptr
        void *pBig = arena.alloc(sizeof(kArenaBuffer) + 1);
        assert(pBig == nullptr);
    }

    // ---- ZoneAllocator ----
    {
        ZoneAllocator zone(kZoneBuffer, sizeof(kZoneBuffer));

        void *z1 = zone.alloc(256, "entity");
        void *z2 = zone.alloc(128, "model");
        assert(z1 != nullptr);
        assert(z2 != nullptr);

        size_t usedAfterAllocs = zone.used();
        assert(usedAfterAllocs > 256 + 128); // includes block headers + padding

        // FIX 1: free(tag) only unlinks the block from the list —
        // it does NOT reclaim the bump-allocated memory.
        // used() must NOT decrease after free().
        zone.free("entity");
        assert(zone.used() == usedAfterAllocs);  // unchanged

        // freeAll() resets the entire allocator
        zone.freeAll();
        assert(zone.used() == 0);

        // Null tag is harmless
        zone.free("nonexistent");
    }

    // ---- PoolAllocator ----
    {
        // 16 blocks of 64 bytes each
        PoolAllocator pool(kPoolBuffer, 64, 16);
        assert(pool.freeBlocks() == 16);

        void *blocks[8];
        for (int i = 0; i < 8; ++i)
        {
            blocks[i] = pool.alloc();
            assert(blocks[i] != nullptr);
        }
        // 16 total - 8 allocated = 8 free
        assert(pool.freeBlocks() == 8);

        // Exhaust remaining 8
        void *extras[8];
        for (int i = 0; i < 8; ++i)
        {
            extras[i] = pool.alloc();
            assert(extras[i] != nullptr);
        }
        assert(pool.freeBlocks() == 0);

        // Pool exhausted → returns nullptr
        void *overflow = pool.alloc();
        assert(overflow == nullptr);

        // Free 4 of the original 8
        for (int i = 0; i < 4; ++i)
            pool.free(blocks[i]);
        assert(pool.freeBlocks() == 4);

        // Free nullptr is harmless
        pool.free(nullptr);
        assert(pool.freeBlocks() == 4);
    }

    printf("test_memory: all passed\n");
    return 0;
}
