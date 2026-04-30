// ============================================================
// FILE:    engine/entities/game_dll_loader.h
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Runtime game DLL loading.
// DEPENDS:  entities/igame_module.h
// ============================================================

#pragma once

#include "engine/entities/igame_module.h"

#ifdef _WIN32
    #include <windows.h>
#endif

namespace nova
{

// -----------------------------------------------------------------------
// GameDLLLoader - loads game logic at runtime
// -----------------------------------------------------------------------
class GameDLLLoader
{
public:
    GameDLLLoader();
    ~GameDLLLoader();

    bool load(const char *path);
    void unload();

    bool isLoaded() const;
    IGameModule *get() const;

private:
    #ifdef _WIN32
        HMODULE m_dll = nullptr;
    #else
        void* m_dll = nullptr;
    #endif

    IGameModule *m_module = nullptr;
};

} // namespace nova