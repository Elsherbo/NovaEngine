// ============================================================
// FILE:    engine/entities/game_dll_loader.cpp
// MODULE:  Entities
// PHASE:   2
// STATUS:  FIXED
// PURPOSE: Load game DLL at runtime.
//          Windows: LoadLibrary / GetProcAddress / FreeLibrary.
//          Linux / macOS: dlopen / dlsym / dlclose.
// DEPENDS:  entities/igame_module.h
// ============================================================

#include "engine/entities/game_dll_loader.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace nova
{

// -----------------------------------------------------------------------
// Thin OS shims so the rest of the file is platform-agnostic.
// -----------------------------------------------------------------------
#ifdef _WIN32
    static HMODULE dll_open(const char* path)      { return LoadLibraryA(path); }
    static void    dll_close(HMODULE h)             { FreeLibrary(h); }
    static void*   dll_sym(HMODULE h, const char* n){ return (void*)GetProcAddress(h, n); }
    static const char* dll_error()                  { return "LoadLibrary failed"; }
#else
    static void* dll_open(const char* path)         { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
    static void  dll_close(void* h)                 { dlclose(h); }
    static void* dll_sym(void* h, const char* n)    { return dlsym(h, n); }
    static const char* dll_error()                  { return dlerror(); }
#endif

// -----------------------------------------------------------------------
GameDLLLoader::GameDLLLoader()
{
    m_dll    = nullptr;
    m_module = nullptr;
}

GameDLLLoader::~GameDLLLoader()
{
    unload();
}

// -----------------------------------------------------------------------
// load
// -----------------------------------------------------------------------
bool GameDLLLoader::load(const char* path)
{
    if (m_dll)
    {
        fprintf(stderr, "GameDLLLoader: already loaded\n");
        return false;
    }

    m_dll = dll_open(path);

#ifndef _WIN32
    // On Linux the caller may pass a .dll path (Windows convention).
    // Try replacing .dll → .so and also "lib" prefix forms.
    if (!m_dll)
    {
        char altPath[512];
        strncpy(altPath, path, sizeof(altPath) - 1);
        altPath[sizeof(altPath) - 1] = '\0';
        char* dot = strrchr(altPath, '.');
        if (dot && strcmp(dot, ".dll") == 0)
        {
            strncpy(dot, ".so", 4);
            m_dll = dll_open(altPath);
        }
    }
    if (!m_dll)
    {
        // Try libname.so in the same directory
        char libPath[520];
        const char* slash = strrchr(path, '/');
        const char* base  = slash ? slash + 1 : path;
        snprintf(libPath, sizeof(libPath), "%.*slib%s",
                 (int)(base - path), path, base);
        char* dot = strrchr(libPath, '.');
        if (dot) strncpy(dot, ".so", 4);
        m_dll = dll_open(libPath);
    }
#endif

    if (!m_dll)
    {
        fprintf(stderr, "GameDLLLoader: failed to load '%s' (%s)\n",
                path, dll_error());
        return false;
    }

#ifdef _WIN32
    void* sym = dll_sym(m_dll, "GetGameModule");
#else
    void* sym = dll_sym(m_dll, "GetGameModule");
#endif
    PFN_GetGameModule getModule = reinterpret_cast<PFN_GetGameModule>(sym);

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
// unload
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
        dll_close(m_dll);
        m_dll = nullptr;
    }
}

// -----------------------------------------------------------------------
bool GameDLLLoader::isLoaded() const { return m_module != nullptr; }
IGameModule* GameDLLLoader::get()  const { return m_module; }

} // namespace nova