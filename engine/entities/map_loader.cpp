// ============================================================
// FILE:    engine/entities/map_loader.cpp
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: BSP entity lump parser + entity spawner.
//          No external libs — custom hand-written parser only.
// DEPENDS: entities/map_loader.h, entities/entity_factory.h,
//          entities/entity_list.h, renderer/bsp/bsp.h
// ============================================================

#include "engine/entities/map_loader.h"
#include "engine/entities/entity_factory.h"
#include "engine/entities/entity_list.h"
#include "engine/entities/property_store.h"
#include "engine/world/iworld.h"

#include <cstring>   // strncpy, strcmp, strlen, strncmp
#include <cstdio>    // fprintf, sscanf
#include <cctype>    // isspace

namespace nova
{

extern EntityList g_entityList;

// ============================================================
// ParsedEntity
// ============================================================

const char* ParsedEntity::get(const char* key) const
{
    for (int i = 0; i < count; ++i)
        if (std::strcmp(pairs[i].key, key) == 0)
            return pairs[i].val;
    return nullptr;
}

void ParsedEntity::add(const char* key, const char* val)
{
    if (count >= kMaxPairs) return;
    std::strncpy(pairs[count].key, key, kMaxKeyLen - 1);
    pairs[count].key[kMaxKeyLen - 1] = '\0';
    std::strncpy(pairs[count].val, val, kMaxValLen - 1);
    pairs[count].val[kMaxValLen - 1] = '\0';
    ++count;
}

// ============================================================
// Parser helpers
// ============================================================

/// Skip whitespace and C++ // comments (Q2 entity lumps may have them).
static const char* skipWS(const char* p)
{
    while (*p)
    {
        if (std::isspace((unsigned char)*p))        { ++p; continue; }
        if (p[0] == '/' && p[1] == '/') {            // line comment
            while (*p && *p != '\n') ++p;
            continue;
        }
        break;
    }
    return p;
}

/// Read a quoted string starting at the leading `"`.
/// Stores the contents (without quotes) in `buf` up to `bufSize-1` chars.
/// Returns pointer just past the closing `"`, or nullptr on error.
static const char* readQuotedString(const char* p, char* buf, int bufSize)
{
    if (*p != '"') return nullptr;
    ++p;  // skip opening quote

    int len = 0;
    while (*p && *p != '"')
    {
        // Handle backslash-newline continuation (multi-line values).
        if (p[0] == '\\' && p[1] == '\n')
        {
            p += 2;
            continue;
        }
        if (len < bufSize - 1)
            buf[len++] = *p;
        ++p;
    }
    buf[len] = '\0';

    if (*p != '"') return nullptr;  // unterminated string
    return p + 1;                   // skip closing quote
}

// ============================================================
// parseBlock
// ============================================================

// Q2 entity lump format:
//
//   {
//   "key" "value"
//   "key" "value"
//   }
//
// The Quake2 Tools BSP format also allows nested braces for
// some submodels, but the entity lump itself only has flat blocks.

const char* MapLoader::parseBlock(const char* src, ParsedEntity& out)
{
    out.count = 0;

    src = skipWS(src);
    if (!src || *src != '{') return nullptr;
    ++src;  // consume '{'

    while (true)
    {
        src = skipWS(src);
        if (!src || !*src) return nullptr;   // unexpected EOS
        if (*src == '}') return src + 1;     // end of block

        // Expect: "key" "value"
        if (*src != '"') { ++src; continue; } // skip garbage

        char key[ParsedEntity::kMaxKeyLen];
        char val[ParsedEntity::kMaxValLen];

        src = readQuotedString(src, key, sizeof(key));
        if (!src) return nullptr;

        src = skipWS(src);
        if (!src || *src != '"') return nullptr;

        src = readQuotedString(src, val, sizeof(val));
        if (!src) return nullptr;

        out.add(key, val);
    }
}

// ============================================================
// linkTargets — post-spawn target resolution
// ============================================================

void MapLoader::linkTargets()
{
    // For every entity that has a "target" stored in its model field
    // (we reuse model[16..31] as a scratch targetname store during load —
    // see the NOTE in load() below), find the entity whose classname area
    // stores the matching targetname, then wire the teamMaster handle.
    //
    // Q2 stores target/targetname as arbitrary strings; Entity has no
    // dedicated fields for them yet, so we use model[] as temp storage:
    //   model[0..15]  = actual model path (normally empty at load time)
    //   model[16..31] = target string (what this entity fires at)
    //   The target entity's model[0..15] = its own targetname
    //
    // Iterate all active entities twice:
    //   Pass 1: collect entities with a target set
    //   Pass 2: for each, find the entity whose "targetname" matches

    for (size_t i = 0; i < EntityList::kMaxEntities; ++i)
    {
        // Access via iterateActive would be cleaner but EntityList
        // doesn't expose the raw array — use findByClassname won't
        // work for all classes. Instead we rely on the public get()
        // with a fabricated handle (generation 1 = post-create default).
        EntityHandle h = EntityHandle::make(static_cast<uint16_t>(i), 0);
        // Try generation 1 as well since create() starts at gen 0
        for (uint16_t gen = 0; gen <= 1; ++gen)
        {
            h = EntityHandle::make(static_cast<uint16_t>(i), gen);
            Entity* src = g_entityList.get(h);
            if (!src) continue;

            // model[16..31] = target this entity fires at
            const char* target = src->model + 16;
            if (target[0] == '\0') continue;

            // Search for an entity whose targetname (model[0..15]) matches.
            for (size_t j = 0; j < EntityList::kMaxEntities; ++j)
            {
                for (uint16_t gen2 = 0; gen2 <= 1; ++gen2)
                {
                    EntityHandle dh = EntityHandle::make(static_cast<uint16_t>(j), gen2);
                    Entity* dst = g_entityList.get(dh);
                    if (!dst || dst == src) continue;

                    // model[0..15] = targetname of this entity
                    if (std::strncmp(dst->model, target, 15) == 0 && dst->model[0] != '\0')
                    {
                        src->teamMaster = dh;
                        fprintf(stdout, "MapLoader: linked '%s' → '%s'\n",
                                src->classname, dst->classname);
                        goto nextSrc;
                    }
                }
            }
            nextSrc:;
            break; // only process the valid gen
        }
    }
}

// ============================================================
// load
// ============================================================

int MapLoader::load(IWorld* world)
{
    if (!world) return 0;

    const char* lump = world->getEntityString();
    if (!lump || !*lump) return 0;

    // Compile-time-only classes that consume an entity slot but serve
    // no runtime purpose. Must be skipped before EntityFactory::spawn().
    static const char* kSkipClasses[] = {
        "light", "light_spot", "light_environment",
        "light_surface", "_skybox", "func_detail",
        nullptr
    };

    int spawned = 0;
    const char* p = lump;

    while (*p)
    {
        p = skipWS(p);
        if (!*p) break;
        if (*p != '{') { ++p; continue; }

        ParsedEntity pe;
        const char* next = parseBlock(p, pe);
        if (!next) break;
        p = next;

        if (pe.count == 0) continue;

        // ---- Extract mandatory keys ----
        const char* classname = pe.get("classname");
        if (!classname || !*classname) continue;

        // ---- Skip compile-time-only entities ----
        bool shouldSkip = false;
        for (int si = 0; kSkipClasses[si]; ++si)
        {
            if (std::strcmp(classname, kSkipClasses[si]) == 0)
            {
                shouldSkip = true;
                break;
            }
        }
        if (shouldSkip) continue;   // <-- clean, no goto, no uninitialized ent

        // ---- Origin ----
        Vec3 origin{0.f, 0.f, 0.f};
        if (const char* oStr = pe.get("origin"))
            origin = EntityFactory::parseOrigin(oStr);

        // ---- Spawn ----
        Entity* ent = EntityFactory::spawn(classname, origin);
        if (!ent)
        {
            fprintf(stderr, "MapLoader: failed to spawn '%s' (pool full?)\n", classname);
            continue;
        }

        // ---- Angles ----
        {
            const char* aStr = pe.get("angles");
            if (!aStr) aStr = pe.get("angle");
            if (aStr) ent->angles = EntityFactory::parseAngles(aStr);
        }

        // ---- Spawnflags ----
        if (const char* sfStr = pe.get("spawnflags"))
        {
            uint32_t sf = (uint32_t)EntityFactory::parseInt(sfStr);
            ent->flags |= (sf << 16);
        }

        // ---- Health ----
        if (const char* hStr = pe.get("health"))
            ent->health = EntityFactory::parseFloat(hStr);

        // ---- target / targetname ----
        if (const char* tnStr = pe.get("targetname"))
            std::strncpy(ent->model,      tnStr, 15);
        if (const char* tStr  = pe.get("target"))
            std::strncpy(ent->model + 16, tStr,  15);

        // ---- Remaining keys → PropertyStore ----
        uint16_t entIdx = ent->handle.index();
        for (int i = 0; i < pe.count; ++i)
        {
            const char* k = pe.pairs[i].key;
            if (std::strcmp(k, "classname")  == 0) continue;
            if (std::strcmp(k, "origin")     == 0) continue;
            if (std::strcmp(k, "angles")     == 0) continue;
            if (std::strcmp(k, "angle")      == 0) continue;
            if (std::strcmp(k, "spawnflags") == 0) continue;
            if (std::strcmp(k, "health")     == 0) continue;
            if (std::strcmp(k, "targetname") == 0) continue;
            if (std::strcmp(k, "target")     == 0) continue;
            g_propertyStore.set(entIdx, k, pe.pairs[i].val);
        }

        // Call EntityClass::onSpawn() now that all properties are set
        g_entityList.finalize(ent->handle);

        fprintf(stdout, "MapLoader: spawned '%s' at (%.0f, %.0f, %.0f)\n",
                classname, origin.x, origin.y, origin.z);
        ++spawned;
    }

    fprintf(stdout, "MapLoader: %d entities spawned from BSP lump\n", spawned);
    linkTargets();
    return spawned;
}

} // namespace nova

// ============================================================
// REQUIRED BSPMap ACCESSOR (add to bsp.h public section):
//
//   /// Returns the raw entity lump string (null-terminated).
//   const char* getEntityString() const { return m_entities.c_str(); }
//
// This is the minimal change needed — it exposes only a const
// char* into the existing std::string, no new data members.
// ============================================================