// ============================================================
// FILE:    engine/core/memory/pool.h
// MODULE:  Core > Memory
// PHASE:   1
// STATUS:  TODO
// PURPOSE: Fixed-size block allocator with free list.
//          O(1) alloc/free. Used for entities, components.
// DEPENDS: (none)
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace nova
{

class PoolAllocator
{
public:
    PoolAllocator(void *buffer, size_t blockSize, size_t blockCount);
    ~PoolAllocator();

    void *alloc();
    void free(void *block);

    size_t blockSize() const { return m_blockSize; }
    size_t totalBlocks() const { return m_totalBlocks; }
    size_t freeBlocks() const { return m_freeCount; }

private:
    union Block
    {
        Block *next;
        char data;
    };

    Block *m_freeList = nullptr;
    size_t m_blockSize = 0;
    size_t m_totalBlocks = 0;
    size_t m_freeCount = 0;
};

} // namespace nova