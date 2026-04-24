// ============================================================
// FILE:    engine/platform/sdl3/sdl3_platform.cpp
// MODULE:  Platform > SDL3
// STATUS:  Updated for SDL3
// PURPOSE: IPlatform implementation using SDL3.
// ============================================================

#include "engine/platform/iplatform.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_events.h>

#include <cstdio>
#include <cstring>

namespace nova
{

struct ThreadInfo
{
    ThreadFunc func;
    void      *data;
    SDL_Thread *thread = nullptr;
};

static int threadWrapper(void *data)
{
    ThreadInfo *info = static_cast<ThreadInfo *>(data);
    info->func(info->data);
    return 0;
}

class SDL3Platform : public IPlatform
{
public:
    SDL3Platform() = default;
    ~SDL3Platform() override { destroyWindow(); }

    bool createWindow(const WindowDesc &desc) override
    {
        // SDL3: GL attributes MUST be set BEFORE creating the window
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,  SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,  24);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }

        Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL;
        if (desc.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;

        m_window = SDL_CreateWindow(desc.title, desc.width, desc.height, flags);
        if (!m_window)
        {
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            return false;
        }
        
        SDL_SetWindowPosition(m_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        return true;
    }

    void destroyWindow() override
    {
        if (m_window) { SDL_DestroyWindow(m_window); m_window = nullptr; }
    }

    WindowHandle getWindow() const override
    {
        WindowHandle h; h.native = m_window; return h;
    }

    void setWindowTitle(const char *title) override
    {
        if (m_window) SDL_SetWindowTitle(m_window, title);
    }

    void *getNativeWindow() const override { return m_window; }

    bool pollInput(InputState &state) override
    {
        // Reset per-frame delta fields
        state.mouseDeltaX = 0;
        state.mouseDeltaY = 0;
        state.mouseWheel  = 0;
        memset(state.mouseButtons, 0, sizeof(state.mouseButtons));

        // SDL3: GetKeyboardState needs int* parameter
        int numKeys = 0;
        const bool *sdlKeys = SDL_GetKeyboardState(&numKeys);
        int copyCount = (numKeys < 512) ? numKeys : 512;
        for (int i = 0; i < copyCount; ++i)
            state.keys[i] = sdlKeys[i];
        for (int i = copyCount; i < 512; ++i)
            state.keys[i] = false;

        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            switch (e.type)
            {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (e.button.button >= 1 && e.button.button <= 3)
                    state.mouseButtons[e.button.button - 1] = true;
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (e.button.button >= 1 && e.button.button <= 3)
                    state.mouseButtons[e.button.button - 1] = false;
                break;

            case SDL_EVENT_MOUSE_MOTION:
                state.mouseDeltaX += e.motion.xrel;
                state.mouseDeltaY += e.motion.yrel;
                state.mouseX = e.motion.x;
                state.mouseY = e.motion.y;
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                state.mouseWheel += e.wheel.y;
                break;

            case SDL_EVENT_QUIT:
                m_quitRequested = true;
                break;

            default:
                break;
            }
        }

        return !m_quitRequested;
    }

    void setMouseGrab(bool grab) override
    {
        if (m_window)
        {
            if (grab)
            {
                SDL_SetWindowMouseGrab(m_window, true);
                SDL_SetWindowKeyboardGrab(m_window, true);
            }
            else
            {
                SDL_SetWindowMouseGrab(m_window, false);
                SDL_SetWindowKeyboardGrab(m_window, false);
            }
        }
    }

    void showCursor(bool show) override
    {
        // SDL3: SDL_ShowCursor() and SDL_HideCursor() are separate functions
        if (show)
            SDL_ShowCursor();
        else
            SDL_HideCursor();
    }

    FileHandle openFile(const char *path, const char *mode) override
    {
        FileHandle fh; fh.handle = fopen(path, mode); return fh;
    }

    void closeFile(FileHandle fh) override
    {
        if (fh.handle) fclose(static_cast<FILE *>(fh.handle));
    }

    size_t readFile(FileHandle fh, void *buffer, size_t size) override
    {
        if (!fh.handle) return 0;
        return fread(buffer, 1, size, static_cast<FILE *>(fh.handle));
    }

    size_t writeFile(FileHandle fh, const void *buffer, size_t size) override
    {
        if (!fh.handle) return 0;
        return fwrite(buffer, 1, size, static_cast<FILE *>(fh.handle));
    }

    size_t getFileSize(FileHandle fh) override
    {
        if (!fh.handle) return 0;
        FILE *f = static_cast<FILE *>(fh.handle);
        long pos = ftell(f);
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, pos, SEEK_SET);
        return static_cast<size_t>(sz);
    }

    bool fileExists(const char *path) override
    {
        FILE *f = fopen(path, "r");
        if (f) { fclose(f); return true; }
        return false;
    }

    Clock getClock() override
    {
        return static_cast<Clock>(SDL_GetPerformanceCounter());
    }

    double getClockResolution() const override
    {
        return 1.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    }

    ThreadHandle createThread(ThreadFunc func, void *data) override
    {
        ThreadHandle h;
        ThreadInfo *info   = new ThreadInfo{func, data};
        SDL_Thread *thread = SDL_CreateThread(threadWrapper, "NovaThread", info);
        info->thread       = thread;
        h.native           = info;
        return h;
    }

    void destroyThread(ThreadHandle handle) override
    {
        if (!handle.native) return;
        ThreadInfo *info = static_cast<ThreadInfo *>(handle.native);
        if (info->thread) SDL_DetachThread(info->thread);
        delete info;
    }

    void joinThread(ThreadHandle handle) override
    {
        if (!handle.native) return;
        ThreadInfo *info = static_cast<ThreadInfo *>(handle.native);
        if (info->thread) SDL_WaitThread(info->thread, nullptr);
        delete info;
    }

    uint32_t getThreadId() const override { return SDL_GetThreadID(nullptr); }

    void sleep(double seconds) override
    {
        SDL_Delay(static_cast<Uint32>(seconds * 1000.0));
    }

    void *getNativeDisplay() const override { return nullptr; }
    const char *getPlatformName() const override { return "SDL3"; }

private:
    SDL_Window *m_window        = nullptr;
    bool        m_quitRequested = false;
};

IPlatform *createPlatform()
{
    return new SDL3Platform();
}

} // namespace nova