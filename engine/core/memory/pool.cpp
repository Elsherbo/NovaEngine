#include "engine/core/memory/pool.h"

#include <cstdlib>

namespace nova
{

PoolAllocator::PoolAllocator(void *buffer, size_t blockSize, size_t blockCount)
    : m_blockSize(blockSize)
    , m_totalBlocks(blockCount)
    , m_freeCount(blockCount)
{
    char *mem = static_cast<char *>(buffer);
    Block *prev = nullptr;
    for (size_t i = 0; i < blockCount; ++i)
    {
        Block *b = reinterpret_cast<Block *>(mem + i * blockSize);
        b->next = prev;
        prev = b;
    }
    m_freeList = prev; // last block becomes head of freelist
}

PoolAllocator::~PoolAllocator() = default;

void *PoolAllocator::alloc()
{
    if (!m_freeList)
        return nullptr;

    Block *block = m_freeList;
    m_freeList = block->next;
    --m_freeCount;
    return block;
}

void PoolAllocator::free(void *block)
{
    if (!block)
        return;

    Block *b = static_cast<Block *>(block);
    b->next = m_freeList;
    m_freeList = b;
    ++m_freeCount;
}

} // namespace nova