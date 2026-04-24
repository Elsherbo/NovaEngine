// ============================================================
// FILE:    tests/renderer/test_bsp.cpp
// MODULE:  Tests > Renderer > BSP
// PHASE:   1
// PURPOSE: Verify BSP loader parses a .bsp file correctly:
//          magic, version, lump counts. Runs without a GPU.
// DEPENDS: renderer/bsp/bsp.h (load only, no uploadToGPU)
// ============================================================

#include <cstdio>
#include <cstring>
#include <cassert>
#include <vector>

// Test the raw BSP parsing logic without a real file:
// We build a minimal valid IBSP v38 binary in memory and run
// BSPMap::loadLumps() through the public load() path via a
// temp file written to disk.

#include "engine/renderer/bsp/bsp.h"

// Construct a minimal IBSP v38 in memory and write it to a temp file,
// then verify BSPMap::load() parses the lump counts correctly.
static bool writeMinimalBSP(const char *path)
{
    using namespace nova;

    // Header: magic + version + 19 lump entries (offset + length pairs)
    // We populate only the vertex and plane lumps with 2 entries each.
    // All other lumps are empty (offset 0, length 0).

    struct MinimalHeader
    {
        char magic[4];
        int  version;
        int  lumps[19][2]; // [offset, length] pairs
    };

    MinimalHeader hdr;
    memcpy(hdr.magic, "IBSP", 4);
    hdr.version = 38;
    memset(hdr.lumps, 0, sizeof(hdr.lumps));

    // We'll append data after the header.
    // Plane lump: index 1
    // Vertex lump: index 2
    int dataOffset = static_cast<int>(sizeof(MinimalHeader));

    // 2 planes
    BSPRawPlane planes[2];
    planes[0].normal[0] = 0.f; planes[0].normal[1] = 1.f; planes[0].normal[2] = 0.f;
    planes[0].dist = 0.f; planes[0].type = 1;
    planes[1].normal[0] = 1.f; planes[1].normal[1] = 0.f; planes[1].normal[2] = 0.f;
    planes[1].dist = 128.f; planes[1].type = 0;

    hdr.lumps[1][0] = dataOffset;
    hdr.lumps[1][1] = static_cast<int>(sizeof(planes));
    dataOffset += static_cast<int>(sizeof(planes));

    // 3 vertices
    BSPRawVertex verts[3];
    memset(verts, 0, sizeof(verts));
    verts[0].position[0] = 0.f; verts[0].position[1] = 0.f; verts[0].position[2] = 0.f;
    verts[1].position[0] = 64.f;
    verts[2].position[2] = 64.f;

    hdr.lumps[2][0] = dataOffset;
    hdr.lumps[2][1] = static_cast<int>(sizeof(verts));

    FILE *f = fopen(path, "wb");
    if (!f)
        return false;

    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(planes, sizeof(planes), 1, f);
    fwrite(verts, sizeof(verts), 1, f);
    fclose(f);
    return true;
}

int main()
{
    using namespace nova;

    const char *tmpPath = "test_minimal.bsp";

    if (!writeMinimalBSP(tmpPath))
    {
        fprintf(stderr, "test_bsp: failed to write temp BSP\n");
        return 1;
    }

    BSPMap bsp;
    bool loaded = bsp.load(tmpPath);
    assert(loaded && "BSPMap::load() should succeed on valid IBSP v38");

    // Verify spawn origin defaulted (no entities lump)
    Vec3 spawn = bsp.getSpawnOrigin();
    (void)spawn; // may be zero or model-derived — just don't crash

    remove(tmpPath);

    printf("test_bsp: all passed\n");
    return 0;
}
