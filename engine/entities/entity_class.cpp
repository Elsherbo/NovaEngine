// ============================================================
// FILE:    engine/entities/entity_class.cpp
// MODULE:  Entities
// PHASE:   2
// PURPOSE: Entity class registry implementation.
// ============================================================

#include "engine/entities/entity_class.h"
#include "engine/core/log.h"
#include <cstring>

namespace nova
{

EntityClassRegistry g_entityClasses;

void EntityClassRegistry::registerClass(EntityClass* cls)
{
    if (!cls) return;

    for (size_t i = 0; i < m_count; ++i)
    {
        if (std::strcmp(m_classes[i]->classname(), cls->classname()) == 0)
        {
            Logger::instance().warn("EntityClassRegistry: duplicate class '%s', ignoring",
                                    cls->classname());
            return;
        }
    }

    if (m_count >= kMaxClasses)
    {
        Logger::instance().error("EntityClassRegistry: max classes reached (%zu)", kMaxClasses);
        return;
    }

    m_classes[m_count++] = cls;
    Logger::instance().info("EntityClassRegistry: registered '%s'", cls->classname());
}

EntityClass* EntityClassRegistry::find(const char* classname) const
{
    if (!classname) return nullptr;

    for (size_t i = 0; i < m_count; ++i)
    {
        if (std::strcmp(m_classes[i]->classname(), classname) == 0)
            return m_classes[i];
    }

    return nullptr;
}

} // namespace nova
