#include "engine/core/memory/arena.h"

#include <cstdlib>
#include <cstring>

namespace nova
{

MemoryArena::MemoryArena(void *buffer, size_t capacity)
    : m_base(static_cast<char *>(buffer))
    , m_capacity(capacity)
{
}

MemoryArena::~MemoryArena() = default;

void *MemoryArena::alloc(size_t size, size_t alignment)
{
    size_t misalignment = reinterpret_cast<uintptr_t>(m_base + m_offset) % alignment;
    size_t padding = misalignment ? alignment - misalignment : 0;
    size_t requested = size + padding;

    if (m_offset + requested > m_capacity)
    {
        return nullptr;
    }

    void *result = m_base + m_offset + padding;
    m_offset += requested;
    return result;
}

void MemoryArena::reset()
{
    m_offset = 0;
}

} // namespace nova