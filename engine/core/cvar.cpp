// ============================================================
// FILE:    engine/core/cvar.cpp
// MODULE:  Core > CVar
// PURPOSE: CVarSystem implementation.
// ============================================================

#include "engine/core/cvar.h"
#include "engine/core/log.h"

#include <cstdio>
#include <cstdlib>   // std::atof
#include <cstring>
#include <algorithm>
#include <cctype>

namespace nova
{

// ---------------------------------------------------------------------------
// instance() — Meyer's singleton, thread-safe under C++11 and later
// ---------------------------------------------------------------------------
CVarSystem& CVarSystem::instance()
{
    static CVarSystem s_instance;
    return s_instance;
}

// ---------------------------------------------------------------------------
// reg()
// ---------------------------------------------------------------------------
CVar* CVarSystem::reg(const char* name, float defaultVal, const char* desc,
                       CVarFlags flags)
{
    if (!name || name[0] == '\0')
        return nullptr;

    // Idempotent: if already registered, return existing pointer silently.
    auto it = m_map.find(name);
    if (it != m_map.end())
        return &it->second;

    // Insert into map — the map owns the CVar struct.
    auto [newIt, inserted] = m_map.emplace(name, CVar{});
    (void)inserted; // always true — existence was checked above
    CVar& cv      = newIt->second;
    cv.name       = newIt->first.c_str();  // stable key pointer
    cv.desc       = desc ? desc : "";
    cv.value      = defaultVal;
    cv.defaultVal = defaultVal;
    cv.flags      = flags;

    m_ordered.push_back(&cv);

    // Keep sv_cheats pointer cached for fast cheat-check in set().
    if (std::strcmp(name, "sv_cheats") == 0)
        m_svCheats = &cv;

    return &cv;
}

// ---------------------------------------------------------------------------
// find()
// ---------------------------------------------------------------------------
CVar* CVarSystem::find(const char* name)
{
    if (!name) return nullptr;
    auto it = m_map.find(name);
    return (it != m_map.end()) ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// set() — by name
// ---------------------------------------------------------------------------
void CVarSystem::set(const char* name, float val, bool quiet)
{
    CVar* cv = find(name);
    if (!cv)
    {
        Logger::instance().warn("CVar: '%s' not found", name);
        return;
    }
    set(cv, val, quiet);
}

// ---------------------------------------------------------------------------
// set() — by pointer (preferred in hot path)
// ---------------------------------------------------------------------------
void CVarSystem::set(CVar* cv, float val, bool quiet)
{
    if (!cv) return;

    if (hasFlag(cv->flags, CVarFlags::ReadOnly))
    {
        Logger::instance().warn("CVar: '%s' is read-only", cv->name);
        return;
    }

    if (hasFlag(cv->flags, CVarFlags::Cheat))
    {
        const bool cheatsOn = m_svCheats && m_svCheats->value != 0.0f;
        if (!cheatsOn)
        {
            Logger::instance().warn("CVar: '%s' is cheat-protected (set sv_cheats 1 first)",
                                    cv->name);
            return;
        }
    }

    cv->value = val;

    if (!quiet)
        Logger::instance().info("CVar: %s = %.4g", cv->name, cv->value);
}

// ---------------------------------------------------------------------------
// exec()
//
// Grammar (single line):
//   ""                   → ignore
//   "//"                 → comment, ignore rest
//   "listcvars"          → listAll()
//   "reset <name>"       → reset named cvar to default
//   "<name>"             → print current value of cvar
//   "<name> <value>"     → set cvar to value
// ---------------------------------------------------------------------------
void CVarSystem::exec(const char* line)
{
    if (!line) return;

    // Skip leading whitespace
    while (*line == ' ' || *line == '\t') ++line;

    // Empty or comment
    if (line[0] == '\0' || (line[0] == '/' && line[1] == '/'))
        return;

    // Copy into mutable buffer for tokenising
    char buf[256];
    std::strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    // Trim trailing whitespace / newline
    int len = (int)std::strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' ||
                        buf[len - 1] == ' '  || buf[len - 1] == '\t'))
        buf[--len] = '\0';

    if (len == 0) return;

    // Tokenise on first whitespace
    char* tok1 = buf;
    char* tok2 = nullptr;
    for (int i = 0; i < len; ++i)
    {
        if (buf[i] == ' ' || buf[i] == '\t')
        {
            buf[i] = '\0';
            // skip extra spaces
            int j = i + 1;
            while (buf[j] == ' ' || buf[j] == '\t') ++j;
            if (buf[j] != '\0')
                tok2 = &buf[j];
            break;
        }
    }

    // ---- "listcvars" ----
    if (std::strcmp(tok1, "listcvars") == 0)
    {
        listAll();
        return;
    }

    // ---- "reset <name>" ----
    if (std::strcmp(tok1, "reset") == 0)
    {
        if (!tok2)
        {
            Logger::instance().warn("CVar exec: usage: reset <name>");
            return;
        }
        CVar* cv = find(tok2);
        if (!cv)
        {
            Logger::instance().warn("CVar exec: unknown cvar '%s'", tok2);
            return;
        }
        set(cv, cv->defaultVal);
        return;
    }

    // ---- "<name>" or "<name> <value>" ----
    CVar* cv = find(tok1);
    if (!cv)
    {
        Logger::instance().warn("CVar exec: unknown cvar '%s'", tok1);
        return;
    }

    if (!tok2)
    {
        // Print current value
        Logger::instance().info("CVar: %s = %.4g  (default %.4g)  %s",
                                 cv->name, cv->value, cv->defaultVal, cv->desc);
        return;
    }

    // Parse value — accept both integer and float literals
    char* endptr = nullptr;
    const float val = static_cast<float>(std::strtod(tok2, &endptr));
    if (endptr == tok2)
    {
        Logger::instance().warn("CVar exec: '%s' is not a valid number for '%s'",
                                 tok2, cv->name);
        return;
    }

    set(cv, val);
}

// ---------------------------------------------------------------------------
// listAll()
// ---------------------------------------------------------------------------
void CVarSystem::listAll()
{
    Logger::instance().info("=== CVars (%zu registered) ===", m_ordered.size());
    for (const CVar* cv : m_ordered)
    {
        char flags[32] = {};
        int  fi        = 0;
        if (hasFlag(cv->flags, CVarFlags::Archive))  { flags[fi++] = 'A'; }
        if (hasFlag(cv->flags, CVarFlags::Cheat))    { flags[fi++] = 'C'; }
        if (hasFlag(cv->flags, CVarFlags::ReadOnly)) { flags[fi++] = 'R'; }
        flags[fi] = '\0';

        Logger::instance().info("  %-28s = %-10.4g  [%s]  %s",
                                 cv->name, cv->value,
                                 fi > 0 ? flags : "-",
                                 cv->desc);
    }
}

// ---------------------------------------------------------------------------
// resetAll()
// ---------------------------------------------------------------------------
void CVarSystem::resetAll()
{
    for (CVar* cv : m_ordered)
        cv->value = cv->defaultVal;
    Logger::instance().info("CVar: all cvars reset to defaults");
}

} // namespace nova