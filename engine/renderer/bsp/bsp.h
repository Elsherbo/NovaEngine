// ============================================================
// FILE:    engine/renderer/bsp/bsp.h
// MODULE:  Renderer > BSP
// PHASE:   1
// STATUS:  FIXED
// PURPOSE: Load and render Quake 2 .bsp maps.
// DEPENDS: core/math, irender_backend.h
//
// FIX LOG (vs original):
//   1. BSPRawVertex: was 44-byte interleaved struct — Q2 only stores
//      float[3] position (12 bytes). Fixed to match binary format.
//   2. BSPRawFace.styles: was int[4] (16 bytes) — Q2 stores uint8_t[4]
//      (4 bytes). This single bug made every face struct 12 bytes too
//      large and caused all face data to be garbage.
//   3. BSPRawTexInfo: added char texture[32] name + nexttexinfo field.
//      Axes are float vecs[2][4], not split uvec/vvec + separate offsets.
//   4. BSPRawNode: mins/maxs were int[3] — Q2 stores int16_t[3].
//   5. BSPRawLeaf: completely wrong layout. Added brushOr, fixed
//      cluster/area to int16_t, mins/maxs to int16_t[3],
//      first/numFace to uint16_t, added brush fields.
//   6. BSPTexInfo (parsed): split uAxis+uOffset, vAxis+vOffset,
//      added textureName[32] and nextTexinfo.
//   7. BSPFace (parsed): styles uint8_t[4], lightmap → lightofs,
//      added sideFacing, lmMins[2].
//   8. LeafFaces: entries are uint16_t, not int32_t.
//   9. Static asserts added for all raw structs to guard regressions.
//  10. BSPRawHeader: lump table size is version-dependent.
//      v38 (vanilla Q2) = 19 lumps, v46 (KEX) = 31 lumps.
//      Reading header with fixed 31-entry table over-reads v38 files.
//      Fixed by reading only the header prefix (magic+version) first,
//      then reading the correct number of lump entries.
//  11. CRASH FIX: uploadLightmaps() was allocating one GL texture per
//      face (up to 84K textures on large maps) causing std::bad_alloc.
//      Fixed by packing all face lightmaps into a single GL_TEXTURE_2D
//      lightmap atlas (4096x4096 RGB8). Each face stores its UV offset
//      into the atlas. This reduces GPU texture objects from O(faces)
//      to exactly 1.
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <string>

#include "engine/core/math/vec.h"
#include "engine/renderer/irender_backend.h"
#include "engine/platform/iplatform.h"

namespace nova
{

// ---- BSP Lump Indices (Quake 2 / KEX shared core lumps) ----
// Versions 38 (vanilla Q2) and 46 (KEX remaster) share the same
// lump layout for indices 0-18.  KEX adds lumps 19-30 at the end.
enum class BSPLump : int
{
    Entities   = 0, Planes = 1, Vertices = 2, Visibility = 3,
    Nodes = 4, TexInfo = 5, Faces = 6, Lightmaps = 7,
    Leaves = 8, LeafFaces = 9, LeafBrushes = 10,
    Edges = 11, SurfEdges = 12, Models = 13, Brushes = 14,
    BrushSides = 15, Pop = 16, Areas = 17, AreaPortals = 18,
    LUMP_COUNT_V38 = 19,
    LUMP_COUNT_V46 = 31  // KEX remaster adds lumps 19-30
};

// ---- Raw BSP Structures ----
// All raw structs must exactly match the on-disk Q2 BSP binary layout.
// static_asserts below enforce this — add new ones whenever you add a struct.

// FIX 10: BSP header lump count is version-dependent.
// v38 = 19 lumps (380 bytes of lump table), v46 = 31 lumps (496 bytes).
// We define the maximum here for the raw struct, but only READ the correct
// number of entries based on the version byte at offset 4.
static constexpr int BSP_MAX_LUMPS    = 31;
static constexpr int BSP_LUMPS_V38   = 19;
static constexpr int BSP_LUMPS_V46   = 31;

// On-disk header prefix: magic(4) + version(4) only.
// The lump table starts at offset 8.  Each entry is 2 ints (offset, length) = 8 bytes.
struct BSPRawHeader
{
    char  magic[4];    // "IBSP" or "QBSP"
    int   version;     // 38 = vanilla Q2, 46 = KEX
    int   lumps[BSP_MAX_LUMPS][2];  // [offset, length] — only first 19 valid for v38
};
// No sizeof assert: lump table is over-allocated; we only use the first N entries.

// FIX 1: Q2 vertex lump only stores a float[3] position — 12 bytes.
struct BSPRawVertex
{
    float position[3];
};
static_assert(sizeof(BSPRawVertex) == 12, "BSPRawVertex must be 12 bytes");

struct BSPRawPlane
{
    float normal[3];
    float dist;
    int   type;
};
static_assert(sizeof(BSPRawPlane) == 20, "BSPRawPlane must be 20 bytes");

struct BSPRawEdge
{
    uint16_t v0, v1;
};
static_assert(sizeof(BSPRawEdge) == 4, "BSPRawEdge must be 4 bytes");

// FIX 3: Correct Q2 texinfo layout.
struct BSPRawTexInfo
{
    float   vecs[2][4];    // [0]={s_axis x,y,z, s_offset}, [1]={t_axis x,y,z, t_offset}
    int32_t flags;
    int32_t value;
    char    texture[32];   // texture name (null-terminated)
    int32_t nexttexinfo;   // animation chain index, -1 = none
};
static_assert(sizeof(BSPRawTexInfo) == 76, "BSPRawTexInfo must be 76 bytes");

// FIX 2: styles must be uint8_t[4], not int[4].
struct BSPRawFace
{
    uint16_t plane;
    uint16_t side;
    int32_t  firstEdge;
    int16_t  numEdges;
    int16_t  texinfo;
    uint8_t  styles[4];   // light style indices; 0xFF = unused
    int32_t  lightofs;    // byte offset into lightmap lump; -1 = no lightmap
};
static_assert(sizeof(BSPRawFace) == 20, "BSPRawFace must be 20 bytes");

// FIX 4: mins/maxs are int16_t in Q2 (not int).
struct BSPRawNode
{
    int32_t  plane;
    int32_t  children[2];   // positive = node index, negative = ~leaf index
    int16_t  mins[3];
    int16_t  maxs[3];
    uint16_t firstFace;
    uint16_t numFaces;
};
static_assert(sizeof(BSPRawNode) == 28, "BSPRawNode must be 28 bytes");

// FIX 5: Completely rewritten to match Q2 binary layout (28 bytes).
struct BSPRawLeaf
{
    int32_t  brushOr;      // contents bitmask (SOLID, WATER, etc.)
    int16_t  cluster;      // PVS cluster; -1 = no cluster / solid
    int16_t  area;
    int16_t  mins[3];
    int16_t  maxs[3];
    uint16_t firstFace;    // index into LeafFaces lump
    uint16_t numFaces;
    uint16_t firstBrush;   // index into LeafBrushes lump
    uint16_t numBrushes;
};
static_assert(sizeof(BSPRawLeaf) == 28, "BSPRawLeaf must be 28 bytes");

struct BSPRawModel
{
    float mins[3], maxs[3];
    float origin[3];
    int   headNode;
    int   firstFace;
    int   numFaces;
};
static_assert(sizeof(BSPRawModel) == 48, "BSPRawModel must be 48 bytes");

// ---- Parsed BSP Structures ----
struct BSPPlane
{
    Vec3  normal;
    float dist;
};

struct BSPEdge
{
    uint16_t v0, v1;
};

struct BSPTexInfo
{
    Vec3  uAxis;
    float uOffset;
    Vec3  vAxis;
    float vOffset;
    int   flags;
    int   value;
    char  textureName[32];
    int   nextTexinfo;
};

struct BSPFace
{
    uint16_t plane     = 0;
    bool     sideFacing = false;
    int32_t  firstEdge = 0;
    int16_t  numEdges  = 0;
    int16_t  texinfo   = -1;
    uint8_t  styles[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    int32_t  lightofs  = -1;    // -1 = no lightmap
    int      lmWidth   = 0;
    int      lmHeight  = 0;
    float    lmMins[2] = {};    // texture-space minimum for lightmap UV

    // FIX 11: Atlas UV offset (in texels) — set during atlas packing.
    int      atlasX    = 0;
    int      atlasY    = 0;
    bool     hasAtlas  = false;
};

struct BSPNode
{
    int      plane;
    int      children[2];
    int16_t  mins[3];
    int16_t  maxs[3];
    uint16_t firstFace;
    uint16_t numFaces;
};

struct BSPLeaf
{
    int      brushOr  = 0;
    int16_t  cluster  = -1;
    int16_t  area     = 0;
    int16_t  mins[3]  = {};
    int16_t  maxs[3]  = {};
    uint16_t firstFace  = 0;
    uint16_t numFaces   = 0;
    uint16_t firstBrush = 0;
    uint16_t numBrushes = 0;
};

struct BSPModel
{
    float mins[3], maxs[3];
    float origin[3];
    int   headNode  = 0;
    int   firstFace = 0;
    int   numFaces  = 0;
};

// ---- BSP Map ----
class BSPMap
{
public:
    ~BSPMap();

    bool load(IPlatform *platform, const char *path);
    void buildGeometry();
    void uploadToGPU(IRenderBackend *backend);
    void render(IRenderBackend *backend);

    Vec3 getSpawnOrigin() const { return m_spawnOrigin; }
    Vec3 getSpawnAngles() const { return m_spawnAngles; }

private:
    bool loadLumps(const uint8_t *data, size_t size);
    void freeLumps();
    void parseSpawnFromEntities();
    void buildFaces();
    void uploadLightmapAtlas(IRenderBackend *backend);
    void computeFaceExtents(int faceIdx);

    // Convert Q2 Z-up world-space position to GL Y-up
    static Vec3 q2ToGL(float x, float y, float z) { return {x, z, -y}; }

    // ---- Parsed lumps ----
    std::string m_path;
    IPlatform  *m_platform = nullptr;
    std::vector<Vec3>       m_vertices;
    std::vector<BSPPlane>   m_planes;
    std::vector<BSPEdge>    m_edges;
    std::vector<int>        m_surfEdges;
    std::vector<BSPTexInfo> m_texInfos;
    std::vector<BSPFace>    m_faces;
    std::vector<uint8_t>    m_lightmapData;  // raw bytes from lightmap lump
    std::vector<BSPModel>   m_models;
    std::vector<BSPNode>    m_nodes;
    std::vector<BSPLeaf>    m_leaves;
    std::vector<int>        m_leafFaces;
    std::string             m_entities;

    // ---- Intermediate geometry ----
    struct BSPVertexPacked  // matches gl_backend.cpp 44-byte layout exactly
    {
        float   pos[3];      // offset  0
        float   uv[2];       // offset 12
        float   lmUV[2];     // offset 20
        float   normal[3];   // offset 28
        uint8_t color[4];    // offset 40
    };                       // total = 44 bytes
    static_assert(sizeof(BSPVertexPacked) == 44, "BSPVertexPacked must be 44 bytes");

    std::vector<BSPVertexPacked> m_verticesPacked;
    std::vector<uint32_t>        m_indicesRaw;

    // ---- Per-surface GPU data ----
    struct Surface
    {
        uint32_t indexOffset = 0;
        int      indexCount  = 0;
        int      faceIndex   = -1;   // index into m_faces for lightmap UV lookup
    };
    std::vector<Surface> m_surfaces;

    // ---- GPU resources (chunked to support maps of any size) ----
    struct RenderChunk
    {
        BufferHandle vertexBuffer = INVALID_BUFFER;
        BufferHandle indexBuffer  = INVALID_BUFFER;
        int          indexCount   = 0;
    };
    std::vector<RenderChunk> m_chunks;
    ShaderHandle  m_shader        = INVALID_SHADER;

    // FIX 11: Single lightmap atlas instead of one texture per face.
    // Stores all face lightmaps packed into a 4096x4096 RGBA8 texture.
    TextureHandle m_lmAtlasHandle  = INVALID_TEXTURE;
    SamplerHandle m_lmAtlasSampler = INVALID_SAMPLER;

    static constexpr int kAtlasSize = 4096;

    int  m_totalIndexCount  = 0;  // sum across all chunks, for stats
    int  m_totalVertexCount = 0;

    Vec3 m_spawnOrigin = {0, 0, 0};
    Vec3 m_spawnAngles = {0, 0, 0};
};

} // namespace nova
