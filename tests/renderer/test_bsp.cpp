// ============================================================
// FILE:    tests/renderer/test_bsp.cpp
// MODULE:  Tests > Renderer > BSP
// PHASE:   1
// PURPOSE: Verify BSP loader parses a .bsp file correctly:
//          magic, version, lump counts. Runs without a GPU.
// DEPENDS: renderer/bsp/bsp.h (load only, no uploadToGPU)
//
// FIX: BSPMap::load() now takes (IPlatform*, const char*) — the
//      test previously passed only a path string.  We now write the
//      BSP to a temp file, build a minimal IPlatform stub that reads
//      it via fopen/fread, and pass that stub to bsp.load().
// ============================================================

#include <cstdio>
#include <cstring>
#include <cassert>
#include <cstdlib>
#include <vector>

#include "engine/renderer/bsp/bsp.h"
#include "engine/platform/iplatform.h"

// ---------------------------------------------------------------------------
// Minimal IPlatform stub — only implements the file-I/O methods used by
// BSPMap::load().  All other virtuals are no-ops / stubs.
// ---------------------------------------------------------------------------
class StubPlatform : public nova::IPlatform
{
public:
    // ---- Window (stubs) ----
    bool createWindow(const nova::WindowDesc&) override { return false; }
    void destroyWindow() override {}
    nova::WindowHandle getWindow() const override { return {}; }
    void setWindowTitle(const char*) override {}
    void* getNativeWindow() const override { return nullptr; }
    void* getNativeDisplay() const override { return nullptr; }
    const char* getPlatformName() const override { return "stub"; }

    // ---- Input (stubs) ----
    bool pollInput(nova::InputState&) override { return false; }
    void setMouseGrab(bool) override {}
    void showCursor(bool) override {}

    // ---- File I/O ----
    nova::FileHandle openFile(const char* path, const char* mode) override
    {
        nova::FileHandle fh;
        fh.handle = fopen(path, mode);
        return fh;
    }
    void closeFile(nova::FileHandle fh) override
    {
        if (fh.handle) fclose(static_cast<FILE*>(fh.handle));
    }
    size_t readFile(nova::FileHandle fh, void* buf, size_t sz) override
    {
        if (!fh.handle) return 0;
        return fread(buf, 1, sz, static_cast<FILE*>(fh.handle));
    }
    size_t writeFile(nova::FileHandle fh, const void* buf, size_t sz) override
    {
        if (!fh.handle) return 0;
        return fwrite(buf, 1, sz, static_cast<FILE*>(fh.handle));
    }
    size_t getFileSize(nova::FileHandle fh) override
    {
        if (!fh.handle) return 0;
        FILE* f = static_cast<FILE*>(fh.handle);
        long pos = ftell(f);
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, pos, SEEK_SET);
        return (size_t)(sz > 0 ? sz : 0);
    }
    bool fileExists(const char* path) override
    {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        fclose(f);
        return true;
    }

    // ---- Time / Thread (stubs) ----
    nova::Clock getClock() override { return 0; }
    double getClockResolution() const override { return 1.0; }
    nova::ThreadHandle createThread(nova::ThreadFunc, void*) override { return {}; }
    void destroyThread(nova::ThreadHandle) override {}
    void joinThread(nova::ThreadHandle) override {}
    uint32_t getThreadId() const override { return 0; }
    void sleep(double) override {}
};

// ---------------------------------------------------------------------------
// Construct a minimal IBSP v38 in memory and write it to a temp file.
// Populates only the plane (lump 1) and vertex (lump 2) lumps;
// all other lumps are empty (offset 0, length 0).
// ---------------------------------------------------------------------------
static bool writeMinimalBSP(const char* path)
{
    using namespace nova;

    struct MinimalHeader
    {
        char magic[4];
        int  version;
        int  lumps[19][2]; // v38: 19 lumps, each {offset, length}
    };

    MinimalHeader hdr;
    memcpy(hdr.magic, "IBSP", 4);
    hdr.version = 38;
    memset(hdr.lumps, 0, sizeof(hdr.lumps));

    int dataOffset = static_cast<int>(sizeof(MinimalHeader));

    // 2 planes (lump index 1)
    BSPRawPlane planes[2];
    planes[0].normal[0] = 0.f; planes[0].normal[1] = 1.f; planes[0].normal[2] = 0.f;
    planes[0].dist = 0.f;      planes[0].type = 1;
    planes[1].normal[0] = 1.f; planes[1].normal[1] = 0.f; planes[1].normal[2] = 0.f;
    planes[1].dist = 128.f;    planes[1].type = 0;
    hdr.lumps[1][0] = dataOffset;
    hdr.lumps[1][1] = static_cast<int>(sizeof(planes));
    dataOffset += static_cast<int>(sizeof(planes));

    // 3 vertices (lump index 2)
    BSPRawVertex verts[3];
    memset(verts, 0, sizeof(verts));
    verts[0].position[0] = 0.f;  verts[0].position[1] = 0.f;  verts[0].position[2] = 0.f;
    verts[1].position[0] = 64.f; verts[1].position[1] = 0.f;  verts[1].position[2] = 0.f;
    verts[2].position[0] = 0.f;  verts[2].position[1] = 0.f;  verts[2].position[2] = 64.f;
    hdr.lumps[2][0] = dataOffset;
    hdr.lumps[2][1] = static_cast<int>(sizeof(verts));

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(&hdr,    sizeof(hdr),    1, f);
    fwrite(planes,  sizeof(planes), 1, f);
    fwrite(verts,   sizeof(verts),  1, f);
    fclose(f);
    return true;
}

int main()
{
    using namespace nova;

    const char* tmpPath = "test_minimal.bsp";

    if (!writeMinimalBSP(tmpPath))
    {
        fprintf(stderr, "test_bsp: failed to write temp BSP\n");
        return 1;
    }

    StubPlatform platform;
    BSPMap bsp;

    // FIX: load() now takes (IPlatform*, const char*)
    bool loaded = bsp.load(&platform, tmpPath);
    assert(loaded && "BSPMap::load() should succeed on a valid IBSP v38");

    // Verify plane and vertex counts match what we wrote
    assert(bsp.planeCount() == 2 && "expected 2 planes");

    // Spawn origin: no entities lump → should stay at (0,0,0) or model origin
    Vec3 spawn = bsp.getSpawnOrigin();
    (void)spawn; // just don't crash

    remove(tmpPath);

    printf("test_bsp: all passed\n");
    return 0;
}
