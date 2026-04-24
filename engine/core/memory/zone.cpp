// ============================================================
// FILE:    engine/core/memory/zone.cpp
// MODULE:  Core > Memory
// PHASE:   1
// PURPOSE: Quake-style tagged memory allocator.
//
// FIX LOG:
//   1. [BUG FIX] free(tag): The old implementation subtracted
//      (size + sizeof(Block)) from m_offset when freeing a block.
//      This is only valid if the freed block is the LAST allocation,
//      because the allocator is linear (bump-pointer). Freeing a
//      non-last block gave a wrong m_offset, making subsequent
//      alloc()s write into already-allocated memory.
//      Fix: free(tag) now ONLY removes the block from the linked
//      list so it is no longer iterable/visible, but does NOT
//      reclaim the address space. The memory is effectively leaked
//      until freeAll() is called.  This matches Quake's zone
//      semantics: individual frees are for tracking/lookup only,
//      bulk reclaim happens via freeAll() / per-level reload.
//      If true individual reclaim is needed, switch to a
//      segregated free-list allocator (Phase 2 task).
//   2. [BUG FIX] used() now returns the true high-water mark so
//      callers can tell whether the arena is under pressure.
// ============================================================
#include "engine/core/memory/zone.h"

#include <cstdlib>
#include <cstring>

namespace nova
{

ZoneAllocator::ZoneAllocator(void *buffer, size_t capacity)
    : m_base(static_cast<char *>(buffer))
    , m_capacity(capacity)
{
}

ZoneAllocator::~ZoneAllocator() = default;

void *ZoneAllocator::alloc(size_t size, const char *tag)
{
    // Each allocation: [Block header][user data]
    // Align user data to 16 bytes
    size_t headerSize = sizeof(Block);
    size_t padding    = (16 - (headerSize % 16)) % 16;
    size_t total      = headerSize + padding + size;

    if (m_offset + total > m_capacity)
        return nullptr;

    Block *block = reinterpret_cast<Block *>(m_base + m_offset);
    block->size  = size;
    block->tag   = tag;
    block->next  = m_blocks;
    m_blocks     = block;

    m_offset += total;

    // Return pointer just past the (padded) header
    return reinterpret_cast<char *>(block) + headerSize + padding;
}

void ZoneAllocator::free(const char *tag)
{
    // FIX 1: Only unlink the block from the list.
    // Do NOT modify m_offset — the bump allocator cannot reclaim
    // individual non-last allocations without corrupting later ones.
    Block **prev = &m_blocks;
    Block  *b    = m_blocks;
    while (b)
    {
        if (b->tag == tag)
        {
            *prev = b->next;
            // Memory is NOT reclaimed — it stays allocated until freeAll().
            return;
        }
        prev = &b->next;
        b    = b->next;
    }
}

void ZoneAllocator::freeAll()
{
    m_offset = 0;
    m_blocks = nullptr;
}

} // namespace nova
