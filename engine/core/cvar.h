// ============================================================
// FILE:    engine/core/cvar.h
// MODULE:  Core > CVar
// PURPOSE: Console variable system.
//
//   CVars are named float values that can be read and written at
//   runtime via the in-game console.  They replace static constexpr
//   constants so feel constants can be tuned without recompiling.
//
// USAGE (registering, anywhere — before first use):
//   inline CVar* cv_friction =
//       CVarSystem::instance().reg("pc_friction", 6.0f, "ground friction");
//
// USAGE (reading, in hot path):
//   float f = cv_friction->value;   // direct member read — zero overhead
//
// USAGE (console):
//   CVarSystem::instance().exec("pc_friction 8");
//   CVarSystem::instance().exec("r_debugview 2");
//   CVarSystem::instance().listAll();   // dumps all cvars to Logger
//
// DESIGN NOTES:
//   - Registration is order-independent: reg() is safe to call from
//     static initializers, global inline vars, or DLL init — whichever
//     fires first wins; subsequent reg() calls for the same name are
//     silent no-ops that return the existing CVar*.
//   - The registry is a flat unordered_map; find() is O(1) average.
//   - CVar::value is a plain float — no atomic, no lock.  All CVar
//     writes must happen on the main thread (console exec is on main
//     thread, so this is always true).
//   - CVarFlags::Archive marks cvars to be written to nova.cfg on
//     shutdown (not yet implemented — hook goes in Engine::shutdown).
//   - CVarFlags::Cheat prevents setting the cvar unless sv_cheats is
//     non-zero (enforcement is in CVarSystem::set()).
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace nova
{

// ---------------------------------------------------------------------------
// CVarFlags
// ---------------------------------------------------------------------------
enum class CVarFlags : uint32_t
{
    None    = 0,
    Archive = 1 << 0,   // persist to nova.cfg
    Cheat   = 1 << 1,   // require sv_cheats != 0 to change
    ReadOnly= 1 << 2,   // cannot be changed at runtime (only via reg default)
};

inline CVarFlags operator|(CVarFlags a, CVarFlags b)
{
    return static_cast<CVarFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline CVarFlags operator&(CVarFlags a, CVarFlags b)
{
    uint32_t result = static_cast<uint32_t>(a) & static_cast<uint32_t>(b);
    return result != 0 ? static_cast<CVarFlags>(result) : CVarFlags::None;
}
inline bool hasFlag(CVarFlags val, CVarFlags flag)
{
    return (static_cast<uint32_t>(val) & static_cast<uint32_t>(flag)) != 0;
}

// ---------------------------------------------------------------------------
// CVar  — one registered console variable
//
// Do NOT construct directly.  Always obtain a pointer from
// CVarSystem::instance().reg() or CVarSystem::instance().find().
// ---------------------------------------------------------------------------
struct CVar
{
    const char* name       = nullptr;
    const char* desc       = nullptr;
    float       value      = 0.0f;
    float       defaultVal = 0.0f;
    CVarFlags   flags      = CVarFlags::None;

    // Convenience: reset to default
    void reset() { value = defaultVal; }
};

// ---------------------------------------------------------------------------
// CVarSystem  — singleton registry + console command executor
// ---------------------------------------------------------------------------
class CVarSystem
{
public:
    // ---- Singleton ----
    static CVarSystem& instance();

    // ---- Registration ----
    // Idempotent: if 'name' already exists the existing CVar* is returned
    // and the call is otherwise ignored (no default overwrite, no assert).
    // Safe to call from static initializers / inline variable definitions.
    CVar* reg(const char* name, float defaultVal, const char* desc,
              CVarFlags flags = CVarFlags::None);

    // ---- Lookup ----
    // Returns nullptr if not found.
    CVar* find(const char* name);

    // ---- Mutation ----
    // Respects ReadOnly and Cheat flags.
    // 'quiet' suppresses the log line (used internally by exec()).
    void set(const char* name, float val, bool quiet = false);
    void set(CVar* cv, float val, bool quiet = false);

    // ---- Console line execution ----
    // Parses a single line of the form:
    //   "cvar_name value"    → calls set()
    //   "cvar_name"          → prints current value
    //   "reset cvar_name"    → resets to default
    //   "listcvars"          → calls listAll()
    // Malformed or unknown names print a warning via Logger.
    void exec(const char* line);

    // ---- Bulk ops ----
    void listAll();                           // dump all cvars to Logger::info
    void resetAll();                          // reset every cvar to default
    const std::vector<CVar*>& allCVars() const { return m_ordered; }

private:
    CVarSystem()  = default;
    ~CVarSystem() = default;
    CVarSystem(const CVarSystem&) = delete;
    CVarSystem& operator=(const CVarSystem&) = delete;

    // Storage: map owns the CVar objects; ordered list for listAll / cfg dump.
    std::unordered_map<std::string, CVar> m_map;
    std::vector<CVar*>                    m_ordered;

    // Built-in: sv_cheats must be non-zero to set Cheat cvars.
    CVar* m_svCheats = nullptr;
};

} // namespace nova