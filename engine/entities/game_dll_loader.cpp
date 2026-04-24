// ============================================================
// FILE:    engine/entities/game_dll_loader.cpp
// MODULE:  Entities
// PHASE:   2
// STATUS:  IN_PROGRESS
// PURPOSE: Load game DLL at runtime.
//          Allows modding without rebuilding engine.
// DEPENDS:  entities/igame_module.h
// ============================================================

#include "engine/entities/game_dll_loader.h"

#include <cstdio>
#include <cstring>
#include <windows.h>

namespace nova
{

// -----------------------------------------------------------------------
// GameDLLLoader constructor
// -----------------------------------------------------------------------
GameDLLLoader::GameDLLLoader()
{
    m_dll = nullptr;
    m_module = nullptr;
}

// -----------------------------------------------------------------------
// GameDLLLoader destructor
// -----------------------------------------------------------------------
GameDLLLoader::~GameDLLLoader()
{
    unload();
}

// -----------------------------------------------------------------------
// load - open DLL and get IGameModule
// -----------------------------------------------------------------------
bool GameDLLLoader::load(const char *path)
{
    if (m_dll)
    {
        fprintf(stderr, "GameDLLLoader: already loaded\n");
        return false;
    }

    m_dll = LoadLibraryA(path);
    if (!m_dll)
    {
        fprintf(stderr, "GameDLLLoader: failed to load '%s'\n", path);
        return false;
    }

    void *sym = (void*)GetProcAddress((HMODULE)m_dll, "GetGameModule");
    PFN_GetGameModule getModule = (PFN_GetGameModule)sym;

    if (!getModule)
    {
        fprintf(stderr, "GameDLLLoader: no GetGameModule in '%s'\n", path);
        unload();
        return false;
    }

    m_module = getModule();
    if (!m_module)
    {
        fprintf(stderr, "GameDLLLoader: GetGameModule returned null\n");
        unload();
        return false;
    }

    if (!m_module->init())
    {
        fprintf(stderr, "GameDLLLoader: module init failed\n");
        m_module->shutdown();
        m_module = nullptr;
        unload();
        return false;
    }

    fprintf(stdout, "GameDLLLoader: loaded '%s'\n", path);
    return true;
}

// -----------------------------------------------------------------------
// unload - close DLL
// -----------------------------------------------------------------------
void GameDLLLoader::unload()
{
    if (m_module)
    {
        m_module->shutdown();
        m_module = nullptr;
    }

    if (m_dll)
    {
        FreeLibrary((HMODULE)m_dll);
        m_dll = nullptr;
    }
}

// -----------------------------------------------------------------------
// isLoaded - check if DLL is loaded
// -----------------------------------------------------------------------
bool GameDLLLoader::isLoaded() const
{
    return m_module != nullptr;
}

// -----------------------------------------------------------------------
// get - get the loaded module
// -----------------------------------------------------------------------
IGameModule *GameDLLLoader::get() const
{
    return m_module;
}

} // namespace nova