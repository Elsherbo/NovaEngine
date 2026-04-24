// ============================================================
// FILE:    engine/core/memory/zone.h
// MODULE:  Core > Memory
// PHASE:   1
// STATUS:  DONE
// PURPOSE: Quake-style tagged memory allocator. Allocates
//          named blocks, frees by tag (or all at once).
//          Used for persistent game objects.
//
// NOTE ON free(tag):
//   Individual frees only unlink the block from the lookup list.
//   They do NOT reclaim address space — the bump offset is not
//   rewound. Call freeAll() to reset the entire arena (e.g. on
//   level change). This matches Quake zone semantics.
// DEPENDS: (none)
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace nova
{

class ZoneAllocator
{
public:
    explicit ZoneAllocator(void *buffer, size_t capacity);
    ~ZoneAllocator();

    // Allocate `size` bytes tagged with `tag`.
    // Returns nullptr if capacity is exhausted.
    void *alloc(size_t size, const char *tag);

    // Unlink the first block with matching tag from the list.
    // Does NOT reclaim memory — call freeAll() for bulk reclaim.
    void free(const char *tag);

    // Reset the entire allocator (rewind to zero).
    void freeAll();

    // High-water mark of bytes consumed (includes headers + padding).
    size_t used()     const { return m_offset; }
    size_t capacity() const { return m_capacity; }

private:
    struct Block
    {
        size_t      size;
        const char *tag;
        Block      *next;
    };

    char  *m_base     = nullptr;
    size_t m_capacity = 0;
    size_t m_offset   = 0;
    Block *m_blocks   = nullptr;
};

} // namespace nova
