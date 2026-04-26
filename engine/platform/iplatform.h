// ============================================================
// FILE:    engine/platform/iplatform.h
// MODULE:  Platform
// PHASE:   1
// STATUS:  DONE
// PURPOSE: Pure virtual interface for platform services:
//          window creation, input polling, file I/O,
//          high-resolution timer, threading.
// DEPENDS: (none - this is the interface all platforms implement)
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace nova
{

// ---- Window ----
struct WindowDesc
{
    const char *title = "Nova Engine";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
};

struct WindowHandle
{
    void *native = nullptr;
};

// ---- Input ----
// keys[] is indexed by SDL_Scancode (hardware layout, not char codes).
// Use SDL_SCANCODE_* constants directly (e.g., SDL_SCANCODE_W, SDL_SCANCODE_SPACE).

struct InputState
{
    bool keys[512] = {};        // indexed by SDL_Scancode
    bool mouseButtons[3] = {};  // Left=0, Right=1, Middle=2
    int mouseX      = 0;
    int mouseY      = 0;
    int mouseDeltaX = 0;
    int mouseDeltaY = 0;
    int mouseWheel  = 0;
};

// ---- File I/O ----
struct FileHandle
{
    void *handle = nullptr;
};

// ---- Time ----
using Clock = uint64_t;

// ---- Thread ----
struct ThreadHandle
{
    void *native = nullptr;
};

using ThreadFunc = void (*)(void *);

// ---- IPlatform interface ----
class IPlatform
{
public:
    virtual ~IPlatform() = default;

    virtual bool createWindow(const WindowDesc &desc) = 0;
    virtual void destroyWindow() = 0;
    virtual WindowHandle getWindow() const = 0;
    virtual void setWindowTitle(const char *title) = 0;
    virtual void *getNativeWindow() const = 0;

    virtual bool pollInput(InputState &state) = 0;
    virtual void setMouseGrab(bool grab) = 0;
    virtual void showCursor(bool show) = 0;

    virtual FileHandle openFile(const char *path, const char *mode) = 0;
    virtual void closeFile(FileHandle fh) = 0;
    virtual size_t readFile(FileHandle fh, void *buffer, size_t size) = 0;
    virtual size_t writeFile(FileHandle fh, const void *buffer, size_t size) = 0;
    virtual size_t getFileSize(FileHandle fh) = 0;
    virtual bool fileExists(const char *path) = 0;

    virtual Clock getClock() = 0;
    virtual double getClockResolution() const = 0;

    virtual ThreadHandle createThread(ThreadFunc func, void *data) = 0;
    virtual void destroyThread(ThreadHandle handle) = 0;
    virtual void joinThread(ThreadHandle handle) = 0;
    virtual uint32_t getThreadId() const = 0;

    virtual void sleep(double seconds) = 0;
    virtual void *getNativeDisplay() const = 0;
    virtual const char *getPlatformName() const = 0;
};

IPlatform *createPlatform();

} // namespace nova
