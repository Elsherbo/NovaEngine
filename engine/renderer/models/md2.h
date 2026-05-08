// ============================================================
// FILE:    engine/renderer/models/md2.h
// MODULE:  Renderer > Models
// PHASE:   2
// PURPOSE: MD2 model loading, animation interpolation, rendering.
//          Quake 2 character/weapon model format (IDP2 v8).
// DEPENDS: core/math, irender_backend.h, core/asset_fs.h
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "engine/core/math/vec.h"
#include "engine/core/math/mat4.h"
#include "engine/renderer/irender_backend.h"
#include "engine/core/asset_fs.h"
#include "engine/renderer/models/mesh.h"

namespace nova
{

// Forward declarations
class BSPMap;

// ---- MD2 vertex layout (44 bytes, identical stride to kLayoutBSP) ----
// Models set lmUV = (-1, -1) sentinel and color = white (255,255,255,255).
struct MD2VertexPacked
{
    float    pos[3];     // offset  0
    float    uv[2];      // offset 12
    float    lmUV[2];    // offset 20  (sentinel: -1, -1)
    float    normal[3];  // offset 28
    uint8_t  color[4];   // offset 40  (255, 255, 255, 255)
};
static_assert(sizeof(MD2VertexPacked) == 44, "MD2VertexPacked must be 44 bytes");

constexpr VertexLayout kLayoutMD2 = {
    "MD2",
    44,
    {
        {"a_position", VertexType::Float3, 3, 0},
        {"a_uv",       VertexType::Float2, 2, 12},
        {"a_lmUV",     VertexType::Float2, 2, 20},
        {"a_normal",   VertexType::Float3, 3, 28},
        {"a_color",    VertexType::Byte4N, 4, 40},
    },
    5};

// ---- MD2 file format constants ----
constexpr uint32_t MD2_MAGIC = 0x32504449; // "IDP2"
constexpr int      MD2_VERSION = 8;
constexpr int      MD2_MAX_SKINNAME = 64;
constexpr int      MD2_MAX_FRAME_NAME = 16;
constexpr int      MD2_ALIAS_SKIN_NAMES = 32; // max skin names per alias frame
constexpr int      MD2_NUM_ANORMS = 162;      // precomputed normals in anorms.h

// ---- MD2 on-disk structures (little-endian) ----
struct MD2Header
{
    int32_t magic;
    int32_t version;
    int32_t skinWidth;   // texture width (must be power of 2)
    int32_t skinHeight;  // texture height (must be power of 2)
    int32_t frameSize;   // bytes per frame
    int32_t numSkins;
    int32_t numVertices; // vertices per frame
    int32_t numST;       // texture coordinates
    int32_t numTriangles;
    int32_t numGLCommands;
    int32_t numFrames;   // total animation frames
    int32_t offsetSkins;
    int32_t offsetST;
    int32_t offsetTriangles;
    int32_t offsetFrames;
    int32_t offsetGLCommands;
    int32_t offsetEnd;
};
static_assert(sizeof(MD2Header) == 68, "MD2Header must be 68 bytes");

struct MD2Triangle
{
    uint16_t vertexIndex[3];
    uint16_t stIndex[3];
};
static_assert(sizeof(MD2Triangle) == 12, "MD2Triangle must be 12 bytes");

struct MD2TexCoord
{
    int16_t s; // [0, skinWidth]
    int16_t t; // [0, skinHeight]
};
static_assert(sizeof(MD2TexCoord) == 4, "MD2TexCoord must be 4 bytes");

struct MD2AliasFrame
{
    float    scale[3];      // scale * vertex + translate
    float    translate[3];
    char     name[MD2_MAX_FRAME_NAME];
    // Followed by numVertices × MD2AliasVertex
};
static_assert(sizeof(MD2AliasFrame) == 40, "MD2AliasFrame must be 40 bytes");

struct MD2AliasVertex
{
    uint8_t v[3];      // compressed position
    uint8_t normalIndex; // index into anorms table
};
static_assert(sizeof(MD2AliasVertex) == 4, "MD2AliasVertex must be 4 bytes");

// ---- Standard MD2 normal table (162 entries) ----
// Populated in md2_loader.cpp from the Quake 2 anorms.h data.
extern const Vec3 g_md2Normals[MD2_NUM_ANORMS];

// ---- Animation frame range (named sequences) ----
struct MD2AnimRange
{
    int first = 0;
    int last  = 0;
    float fps = 10.0f;
};

// Standard Quake 2 animation names and frame ranges are typically
// embedded in the frame names (e.g., "stand01", "run05", "attack01").
// The model loader auto-detects these ranges.

// ---- Per-model GPU data ----
struct MD2Mesh : public IMesh
{
    bool load(IRenderBackend* backend, AssetFS* assets, const char* md2Path);
    void release(IRenderBackend* backend) override;
    void draw(IRenderBackend* backend) const override;

    // Upload interpolated CPU vertices to GPU (called each frame if animating).
    void updateVertices(IRenderBackend* backend,
                        const MD2VertexPacked* vertices, int vertexCount);
    int  numTriangles() const { return m_numTris; }
    int  numFrames() const { return m_numFrames; }

    // Frame data access (for interpolation)
    const float* frameScale(int frame) const { return &m_frameData[frame * 6 + 0]; }
    const float* frameTranslate(int frame) const { return &m_frameData[frame * 6 + 3]; }
    const char*  frameName(int frame) const;
    const uint8_t* frameVerts(int frame) const { return &m_frameVerts[frame * m_frameVertCount * 4]; }

    // Per-vertex UVs (constant across frames)
    float uv(int vertex, int channel) const { return m_uvs[vertex * 2 + channel]; }

    // Frame vertex count (from header, may differ from m_numVerts if expanded).
    int frameVertCount() const { return m_frameVertCount; }

    // Interpolate two frames and scatter to split vertices (called each frame if animating).
    void interpolateFrame(int frameA, int frameB, float t,
                          MD2VertexPacked* outVerts, int outVertCount) const;

    // Skin texture handles (loaded from skin files referenced in MD2).
    TextureHandle  skinTexture(int i) const { return m_skinTextures[i]; }
    SamplerHandle  skinSampler(int i) const { return m_skinSamplers[i]; }
    int            numSkins() const { return (int)m_skinTextures.size(); }

    // Auto-detected animation ranges (keyed by prefix: "stand", "run", etc.)
    const MD2AnimRange* findAnim(const char* prefix) const;
    const std::unordered_map<std::string, MD2AnimRange>& anims() const { return m_anims; }

private:
    void buildFrameData(const uint8_t* frameBytes, int frameIdx,
                        MD2VertexPacked* outVerts, int frameVertCount = -1) const;
    void buildFrameDataSplit(const uint8_t* frameBytes, int frameIdx,
                             MD2VertexPacked* outVerts, int frameVertCount) const;
    void detectAnimations();

    BufferHandle    m_vertexBuffer = INVALID_BUFFER;
    BufferHandle    m_indexBuffer  = INVALID_BUFFER;
    int             m_numFrames = 0;
    int             m_frameVertCount = 0; // per-frame vertex count (from header, used for frame data)

    // Per-frame raw compressed data (flattened for cache locality).
    // Layout: [frame][vertex][4 bytes: v[3] + normalIndex]
    std::vector<uint8_t> m_frameVerts;

    // Per-frame scale[3] + translate[3] (6 floats per frame).
    std::vector<float> m_frameData;

    // Per-frame names (null-terminated, stored contiguously).
    std::vector<char> m_frameNames;
    int m_frameNameStride = MD2_MAX_FRAME_NAME;

    // Per-vertex normalized UVs [0,1] (constant across all frames).
    std::vector<float> m_uvs; // 2 floats per vertex

    // Split vertex mapping: for each original frame vertex, list of GPU split indices.
    // Used during frame interpolation to duplicate vertex positions across UV splits.
    struct SplitInfo { int splitIdx; int stIdx; };
    std::vector<std::vector<SplitInfo>> m_splitInfo;

    // Skin textures
    std::vector<TextureHandle> m_skinTextures;
    std::vector<SamplerHandle> m_skinSamplers;

    // Animation ranges
    std::unordered_map<std::string, MD2AnimRange> m_anims;
};

// ---- Per-entity animation playback state ----
struct MD2Instance
{
    int  currentFrame = 0;
    int  nextFrame    = 0;
    float lerpT       = 0.0f; // [0, 1] interpolation factor
    float frameTime   = 0.0f; // accumulated time for current frame
    float fps         = 10.0f;

    // Animation range being played
    int animFirst = 0;
    int animLast  = 0;

    // Skin index
    int skinIndex = 0;

    // World transform
    Vec3    origin = Vec3::zero();
    Vec3    angles = Vec3::zero(); // pitch, yaw, roll (radians)
    float   scale  = 1.0f;

    void update(float dt);
    void setAnim(int first, int last, float fps);
    void setAnim(const MD2AnimRange& range);
};

// ---- Model Registry / Renderer ----
// Manages loaded MD2Meshes and drives per-entity rendering.
class ModelRenderer
{
public:
    ~ModelRenderer();

    // Load an MD2 model. Returns model index (>= 0) or -1 on failure.
    int loadMD2Model(IRenderBackend* backend, AssetFS* assets, const char* md2Path);

    // Load an OBJ model (with MTL materials). Returns model index (>= 0) or -1 on failure.
    int loadOBJModel(IRenderBackend* backend, AssetFS* assets, const char* objPath);

    // Get a loaded mesh by index.
    const IMesh* getMesh(int modelIndex) const;

    // Get an MD2 mesh by index (for animation). Returns nullptr if not MD2.
    const MD2Mesh* getMD2Mesh(int modelIndex) const;

    // Render a single entity instance (MD2 with animation).
    void renderEntity(IRenderBackend* backend, int modelIndex,
                      const MD2Instance& instance, ShaderHandle shader) const;

    // Render a static model (OBJ) at the given transform.
    void renderStatic(IRenderBackend* backend, int modelIndex,
                      const MeshInstance& instance, ShaderHandle shader) const;

    // Render all entities (handles uniforms, lightmap sampling, frustum culling internally).
    // bsp and cameraPos are optional for BSP lightmap sampling and frustum culling.
    void renderAll(IRenderBackend* backend, ShaderHandle shader,
                   const BSPMap* bsp = nullptr, const Vec3* cameraPos = nullptr) const;

    // Register an entity for rendering (called by EntityFactory or game code).
    void registerEntity(int entityIndex, int modelIndex);

    // Update an entity's animation state (called each frame before render).
    void updateEntity(int entityIndex, float dt, const Vec3& origin, const Vec3& angles);

    // Set entity animation by name (e.g., "stand", "run"). Auto-detects from frame names.
    void setEntityAnim(int entityIndex, const char* animName);

    // Set entity animation by explicit frame range.
    void setEntityAnimRange(int entityIndex, int first, int last, float fps);

    // Get mutable access to entity instance (for origin, angles, scale, animation control).
    MD2Instance* getMD2Instance(int entityIndex);
    MeshInstance* getStaticInstance(int entityIndex);

    int numModels() const { return (int)m_meshes.size(); }

private:
    enum class ModelType { MD2, Static };

    struct ModelEntry
    {
        ModelType type;
        std::unique_ptr<IMesh> mesh;
    };

    struct EntityRecord
    {
        int modelIndex = -1;
        ModelType type = ModelType::MD2;
        MD2Instance instance;
        MeshInstance staticInstance; // for Static/OBJ models
    };

    std::vector<ModelEntry>       m_meshes;
    std::vector<EntityRecord>     m_entities;
};

} // namespace nova
