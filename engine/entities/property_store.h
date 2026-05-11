// ============================================================
// FILE:    engine/entities/property_store.h
// MODULE:  Entities
// STATUS:  NEW
// PURPOSE: Stores custom key-value properties for entities.
//          Used for TrenchBroom-defined entity keys that aren't
//          mapped to fixed Entity struct fields.
// ============================================================

#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace nova
{

// -----------------------------------------------------------------------
// PropertyStore - maps entity indices to key-value dictionaries
// -----------------------------------------------------------------------
class PropertyStore
{
public:
    using KeyValues = std::unordered_map<std::string, std::string>;

    PropertyStore() = default;

    // ---- Set a property on an entity ----
    void set(uint16_t entityIndex, const std::string& key, const std::string& val)
    {
        if (entityIndex >= m_properties.size())
            m_properties.resize(entityIndex + 1);
        m_properties[entityIndex][key] = val;
    }

    // ---- Get a property (returns nullptr if not found) ----
    const char* get(uint16_t entityIndex, const std::string& key) const
    {
        if (entityIndex >= m_properties.size())
            return nullptr;
        auto it = m_properties[entityIndex].find(key);
        if (it == m_properties[entityIndex].end())
            return nullptr;
        return it->second.c_str();
    }

    // ---- Check if a key exists ----
    bool has(uint16_t entityIndex, const std::string& key) const
    {
        if (entityIndex >= m_properties.size())
            return false;
        return m_properties[entityIndex].count(key) > 0;
    }

    // ---- Get all keys for an entity ----
    const KeyValues* getAll(uint16_t entityIndex) const
    {
        if (entityIndex >= m_properties.size())
            return nullptr;
        return &m_properties[entityIndex];
    }

    // ---- Remove an entity's properties ----
    void clear(uint16_t entityIndex)
    {
        if (entityIndex >= m_properties.size())
            return;
        m_properties[entityIndex].clear();
    }

    // ---- Clear all properties (on map unload) ----
    void clearAll()
    {
        for (auto& kv : m_properties)
            kv.clear();
    }

    // ---- Get count of entities with properties ----
    size_t size() const { return m_properties.size(); }

private:
    std::vector<KeyValues> m_properties;
};

// -----------------------------------------------------------------------
// Global property store instance — declared extern, defined in .cpp
// to avoid cross-TU duplication (static lib globals copied per binary).
// -----------------------------------------------------------------------
extern PropertyStore g_propertyStore;

} // namespace nova