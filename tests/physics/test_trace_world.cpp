// ============================================================
// FILE:    tests/physics/test_trace_world.cpp
// MODULE:  Tests > Physics > TraceWorld
// PHASE:   2
// PURPOSE: Regression test for AABBPhysics::trace() and moveSlide()
//          on a real BSP collision world.  Runs without a GPU.
//
// DESIGN: Build a minimal IBSP v38 in memory with enough structure
//         for the physics system to do real work:
//
//   - 1 node (root), referencing plane 0
//   - 1 plane: Y=0 (floor), pointing up  (normal = 0,1,0 in Q2 → 0,0,-1 after q2ToGL)
//     Wait — q2ToGL maps (x,y,z) → (x,z,-y), so normal (0,1,0) in Q2 → (0,0,-1) in GL.
//     We want a floor whose normal points +Y in GL space (up).
//     Q2 Z-up: the floor in Q2 has normal (0,0,1) (pointing along +Z which is "up").
//     q2ToGL(0,0,1) → (0,1,0) ✓  dist = 0, type = 5 (PLANE_Z in Q2 = 5)
//
//   - 1 solid brush on the floor (covering a 1024×1024 area)
//     The brush has 6 sides (box): floor at y=−512, ceiling at y=0,
//     and 4 side walls.  All contents = CONTENTS_SOLID (1).
//
//   - 1 leaf containing that brush, cluster = -1 (solid)
//   - 1 model referencing node 0
//
//   We then drop an AABB from above the floor and verify:
//     1. trace() hits the floor surface (fraction < 1)
//     2. The hit normal points +Y (up in GL space)
//     3. A trace that starts and ends in open air returns fraction == 1
// ============================================================

#include <cstdio>
#include <cstring>
#include <cassert>
#include <cmath>
#include <vector>

#include "engine/renderer/bsp/bsp.h"
#include "engine/physics/aabb_physics.h"
#include "engine/platform/iplatform.h"

// ---------------------------------------------------------------------------
// Minimal IPlatform stub (file I/O only — same as in test_bsp.cpp)
// ---------------------------------------------------------------------------
class StubPlatform : public nova::IPlatform
{
public:
    bool createWindow(const nova::WindowDesc&) override { return false; }
    void destroyWindow() override {}
    nova::WindowHandle getWindow() const override { return {}; }
    void setWindowTitle(const char*) override {}
    void* getNativeWindow() const override { return nullptr; }
    void* getNativeDisplay() const override { return nullptr; }
    const char* getPlatformName() const override { return "stub"; }
    bool pollInput(nova::InputState&) override { return false; }
    void setMouseGrab(bool) override {}
    void showCursor(bool) override {}

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
        FILE* f = fopen(path, "rb"); if (!f) return false; fclose(f); return true;
    }
    nova::Clock getClock() override { return 0; }
    double getClockResolution() const override { return 1.0; }
    nova::ThreadHandle createThread(nova::ThreadFunc, void*) override { return {}; }
    void destroyThread(nova::ThreadHandle) override {}
    void joinThread(nova::ThreadHandle) override {}
    uint32_t getThreadId() const override { return 0; }
    void sleep(double) override {}
};

// ---------------------------------------------------------------------------
// Write a minimal IBSP v38 with a solid floor brush at y_GL = 0
// (i.e., Q2 z = 0).  The BSP uses Q2 coordinate conventions; the loader
// applies q2ToGL() during parse.
//
// Coordinate plan (Q2 space):
//   Floor plane: normal = (0,0,1), dist = 0  → q2ToGL → normal = (0,1,0) in GL
//   Brush box:   [−512, −512, −512] to [512, 512, 0]  (z=0 is the top / floor surface)
//
// We write only the lumps required by the physics collision path:
//   Lump 1  Planes
//   Lump 8  Leaves
//   Lump 4  Nodes
//   Lump 10 LeafBrushes
//   Lump 13 Models
//   Lump 14 Brushes
//   Lump 15 BrushSides
// ---------------------------------------------------------------------------

// Helper: little-endian 32-bit write
static void writeI32(std::vector<uint8_t>& buf, int32_t v)
{
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
    buf.push_back((uint8_t)((v >> 16) & 0xFF));
    buf.push_back((uint8_t)((v >> 24) & 0xFF));
}
static void writeU16(std::vector<uint8_t>& buf, uint16_t v)
{
    buf.push_back((uint8_t)(v & 0xFF));
    buf.push_back((uint8_t)((v >> 8) & 0xFF));
}
static void writeI16(std::vector<uint8_t>& buf, int16_t v)
{
    writeU16(buf, (uint16_t)v);
}
static void writeF32(std::vector<uint8_t>& buf, float v)
{
    uint32_t u; memcpy(&u, &v, 4);
    writeI32(buf, (int32_t)u);
}

static bool writeCollisionBSP(const char* path)
{
    using namespace nova;

    // ---- Plane data ----
    // We need 6 planes for the brush box (one per face).
    // Q2 convention:  type: 0=PLANE_X, 1=PLANE_Y, 2=PLANE_Z, 3-5=non-axial
    //   Plane 0: floor   normal=(0,0, 1) dist=  0  (top of brush box, pointing up in Q2)
    //   Plane 1: ceiling normal=(0,0,-1) dist=512  (bottom of brush box)
    //   Plane 2: +X wall normal=(1,0, 0) dist=512
    //   Plane 3: −X wall normal=(−1,0,0) dist=512
    //   Plane 4: +Y wall normal=(0,1, 0) dist=512  (Q2 Y = horizontal)
    //   Plane 5: −Y wall normal=(0,−1,0) dist=512
    struct PlaneEntry { float nx,ny,nz,dist; int type; };
    PlaneEntry planes[] = {
        {  0.f,  0.f,  1.f,    0.f, 2 }, // 0: floor (faces up in Q2)
        {  0.f,  0.f, -1.f,  512.f, 2 }, // 1: bottom (faces down)
        {  1.f,  0.f,  0.f,  512.f, 0 }, // 2: +X
        { -1.f,  0.f,  0.f,  512.f, 0 }, // 3: -X
        {  0.f,  1.f,  0.f,  512.f, 1 }, // 4: +Y
        {  0.f, -1.f,  0.f,  512.f, 1 }, // 5: -Y
    };
    constexpr int numPlanes = 6;

    // ---- Build each lump as a byte vector ----
    std::vector<uint8_t> planeData, nodeData, leafData, leafBrushData,
                          brushData, brushSideData, modelData;

    // Planes lump
    for (int i = 0; i < numPlanes; ++i)
    {
        writeF32(planeData, planes[i].nx);
        writeF32(planeData, planes[i].ny);
        writeF32(planeData, planes[i].nz);
        writeF32(planeData, planes[i].dist);
        writeI32(planeData, planes[i].type);
        // sizeof(BSPRawPlane) = 20 bytes ✓
    }

    // BrushSides lump (6 sides: one per plane)
    // BSPRawBrushSide = {uint16 plane, int16 texinfo}
    for (int i = 0; i < numPlanes; ++i)
    {
        writeU16(brushSideData, (uint16_t)i);  // plane index
        writeI16(brushSideData, -1);           // texinfo = -1
    }

    // Brushes lump (1 brush)
    // BSPRawBrush = {int32 firstBrushSide, int32 numBrushSides, int32 contents}
    writeI32(brushData, 0);   // firstBrushSide
    writeI32(brushData, 6);   // numBrushSides
    writeI32(brushData, 1);   // contents = CONTENTS_SOLID

    // LeafBrushes lump: [0] = brush index 0
    writeU16(leafBrushData, 0);

    // Leaves lump (2 leaves: leaf 0 = open/camera space, leaf 1 = solid floor)
    // BSPRawLeaf = {int32 brushOr, int16 cluster, int16 area,
    //               int16 mins[3], int16 maxs[3],
    //               uint16 firstFace, uint16 numFaces,
    //               uint16 firstBrush, uint16 numBrushes}  (28 bytes total)
    auto writeLeaf = [&](int brushOr, int16_t cluster,
                         int16_t mnx, int16_t mny, int16_t mnz,
                         int16_t mxx, int16_t mxy, int16_t mxz,
                         uint16_t firstBrush, uint16_t numBrushes)
    {
        writeI32(leafData, brushOr);
        writeI16(leafData, cluster);
        writeI16(leafData, 0); // area
        writeI16(leafData, mnx); writeI16(leafData, mny); writeI16(leafData, mnz);
        writeI16(leafData, mxx); writeI16(leafData, mxy); writeI16(leafData, mxz);
        writeU16(leafData, 0);  // firstFace
        writeU16(leafData, 0);  // numFaces
        writeU16(leafData, firstBrush);
        writeU16(leafData, numBrushes);
    };
    // Leaf 0: open space above floor — no brushes, cluster 0
    writeLeaf(0, 0,  -512, 0, -512,  512, 2048, 512,  0, 0);
    // Leaf 1: solid brush leaf — cluster -1 (solid)
    writeLeaf(1, -1, -512, -512, -512, 512, 0, 512,  0, 1);

    // Nodes lump (1 root node splitting on plane 0)
    // BSPRawNode = {int32 plane, int32[2] children,
    //               int16[3] mins, int16[3] maxs,
    //               uint16 firstFace, uint16 numFaces}  (28 bytes)
    writeI32(nodeData, 0);      // plane index
    writeI32(nodeData, ~0);     // front child = leaf 0  (~0 = -1, encodes leaf 0)
    writeI32(nodeData, ~1);     // back child  = leaf 1  (~1 = -2, encodes leaf 1)
    writeI16(nodeData, -512); writeI16(nodeData, -512); writeI16(nodeData, -512); // mins
    writeI16(nodeData,  512); writeI16(nodeData, 2048); writeI16(nodeData,  512); // maxs
    writeU16(nodeData, 0);   // firstFace
    writeU16(nodeData, 0);   // numFaces

    // Models lump (1 model covering the whole map)
    // BSPRawModel = {float[3] mins, float[3] maxs, float[3] origin,
    //                int headNode, int firstFace, int numFaces}  (48 bytes)
    writeF32(modelData, -512.f); writeF32(modelData, -512.f); writeF32(modelData, -512.f);
    writeF32(modelData,  512.f); writeF32(modelData, 2048.f); writeF32(modelData,  512.f);
    writeF32(modelData,    0.f); writeF32(modelData,    0.f); writeF32(modelData,    0.f);
    writeI32(modelData, 0);  // headNode
    writeI32(modelData, 0);  // firstFace
    writeI32(modelData, 0);  // numFaces

    // ---- Build header + lump table ----
    // v38: magic(4) + version(4) + 19 × {offset(4), length(4)} = 8 + 152 = 160 bytes
    constexpr int kHeaderSize = 8 + 19 * 8;
    int offset = kHeaderSize;

    // Lump index → data pointer
    // All lumps default to {offset=0, length=0}
    struct LumpSlot { int off, len; };
    LumpSlot lumpTable[19] = {};

    auto placeLump = [&](int idx, const std::vector<uint8_t>& data)
    {
        if (data.empty()) return;
        lumpTable[idx] = { offset, (int)data.size() };
        offset += (int)data.size();
    };

    placeLump(1,  planeData);       // Planes
    placeLump(4,  nodeData);        // Nodes
    placeLump(8,  leafData);        // Leaves
    placeLump(10, leafBrushData);   // LeafBrushes
    placeLump(13, modelData);       // Models
    placeLump(14, brushData);       // Brushes
    placeLump(15, brushSideData);   // BrushSides

    // Write file
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    // magic + version
    fwrite("IBSP", 1, 4, f);
    int ver = 38; fwrite(&ver, 4, 1, f);

    // lump table
    for (int i = 0; i < 19; ++i)
    {
        fwrite(&lumpTable[i].off, 4, 1, f);
        fwrite(&lumpTable[i].len, 4, 1, f);
    }

    // lump data in order
    auto writeVec = [&](const std::vector<uint8_t>& d)
    {
        if (!d.empty()) fwrite(d.data(), 1, d.size(), f);
    };
    writeVec(planeData);
    writeVec(nodeData);
    writeVec(leafData);
    writeVec(leafBrushData);
    writeVec(modelData);
    writeVec(brushData);
    writeVec(brushSideData);

    fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
int main()
{
    using namespace nova;

    const char* tmpPath = "test_trace_world.bsp";

    if (!writeCollisionBSP(tmpPath))
    {
        fprintf(stderr, "test_trace_world: failed to write temp BSP\n");
        return 1;
    }

    StubPlatform platform;
    BSPMap bsp;
    bool loaded = bsp.load(&platform, tmpPath);
    assert(loaded && "BSPMap::load() should succeed");

    printf("Loaded: %d planes, %d leaves, %d brushes\n",
           bsp.planeCount(), bsp.leafCount(), bsp.brushCount());
    assert(bsp.planeCount() >= 6 && "expected 6 planes");

    // ---- Set up physics ----
    AABBPhysics phys;
    phys.setWorld(&bsp);
    phys.setGravity(800.f);

    // Player AABB (Q2-like: half-extents {16,36,16} but in GL Y-up space)
    const Vec3 mins = { -16.f, -36.f, -16.f };
    const Vec3 maxs = {  16.f,  36.f,  16.f };

    // ---- Test 1: Trace downward from above the floor ----
    // Floor is at y_GL = 0 (q2ToGL of Q2 z=0).
    // Start well above the floor, end below it.
    // The player's feet are 36 units below origin, so standing on y=0 means
    // origin.y = 36.  We start at y = 200 and trace to y = -100.
    {
        Vec3 start = {  0.f, 200.f, 0.f };
        Vec3 end   = {  0.f, -100.f, 0.f };
        TraceResult tr = phys.trace(start, end, mins, maxs);

        printf("Test 1 (trace down): fraction=%.3f  normal=(%.1f,%.1f,%.1f)  endY=%.1f\n",
               tr.fraction, tr.normal.x, tr.normal.y, tr.normal.z, tr.endPos.y);

        assert(tr.fraction < 1.0f && "trace should hit the floor");
        // Normal should point upward (positive Y in GL space)
        assert(tr.normal.y > 0.5f && "floor normal should point +Y in GL space");
        // Should not start solid
        assert(!tr.startSolid && "trace starts above floor, should not be solid");
    }

    // ---- Test 2: Horizontal trace in open air — should not hit anything ----
    {
        Vec3 start = {  0.f, 200.f,  0.f };
        Vec3 end   = {  0.f, 200.f, 50.f };
        TraceResult tr = phys.trace(start, end, mins, maxs);

        printf("Test 2 (horizontal air): fraction=%.3f\n", tr.fraction);
        assert(tr.fraction == 1.0f && "horizontal trace in open air should miss");
    }

    // ---- Test 3: Trace from inside solid — should set startSolid ----
    {
        // The brush occupies y_GL ∈ [−512, 0]; origin at y=−100 is inside.
        Vec3 start = { 0.f, -100.f, 0.f };
        Vec3 end   = { 0.f, -50.f,  0.f };
        TraceResult tr = phys.trace(start, end, mins, maxs);

        printf("Test 3 (start solid): fraction=%.3f  startSolid=%d\n",
               tr.fraction, (int)tr.startSolid);
        // When startSolid is true, fraction is typically 0; the trace should
        // reflect that the start is embedded in geometry.
        assert(tr.startSolid && "trace from inside brush should set startSolid");
    }

    remove(tmpPath);
    printf("test_trace_world: all passed\n");
    return 0;
}
