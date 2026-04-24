// ============================================================
// FILE:    engine/core/memory/arena.h
// MODULE:  Core > Memory
// PHASE:   1
// STATUS:  TODO
// PURPOSE: Linear bump allocator for per-frame scratch memory.
//          Fast allocation via pointer bump, reset() at frame end.
// DEPENDS: (none)
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace nova
{

class MemoryArena
{
public:
    explicit MemoryArena(void *buffer, size_t capacity);
    ~MemoryArena();

    void *alloc(size_t size, size_t alignment = 16);
    void reset();

    size_t used() const { return m_offset; }
    size_t capacity() const { return m_capacity; }
    void *base() const { return m_base; }

private:
    char *m_base = nullptr;
    size_t m_capacity = 0;
    size_t m_offset = 0;
};

} // namespace nova