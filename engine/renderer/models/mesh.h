// ============================================================
// FILE:    engine/renderer/models/mesh.h
// MODULE:  Renderer > Models
// PHASE:   3
// PURPOSE: Generic mesh and material abstractions for multi-format
//          model support (OBJ+MTL, MD2, etc.).
// DEPENDS: irender_backend.h, core/asset_fs.h
// ============================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/core/math/vec.h"
#include "engine/core/math/mat4.h"
#include "engine/renderer/irender_backend.h"
#include "engine/core/asset_fs.h"

namespace nova
{

// ---- Shared vertex layout (44 bytes, matches kLayoutBSP / kLayoutMD2) ----
// OBJ models set lmUV = (-1, -1) sentinel and color = white (255,255,255,255).
struct VertexPacked
{
    float   pos[3];      // offset  0
    float   uv[2];       // offset 12
    float   lmUV[2];     // offset 20  (sentinel: -1, -1)
    float   normal[3];   // offset 28
    uint8_t color[4];    // offset 40  (255, 255, 255, 255)
};
static_assert(sizeof(VertexPacked) == 44, "VertexPacked must be 44 bytes");

constexpr VertexLayout kLayoutStatic = {
    "Static",
    44,
    {
        {"a_position", VertexType::Float3, 3, 0},
        {"a_uv",       VertexType::Float2, 2, 12},
        {"a_lmUV",     VertexType::Float2, 2, 20},
        {"a_normal",   VertexType::Float3, 3, 28},
        {"a_color",    VertexType::Byte4N, 4, 40},
    },
    5};

// ---- Material ----
// Describes surface properties for a sub-mesh. Maps closely to MTL format.
struct Material
{
    std::string name;

    // Colors
    Vec3 ambient  = Vec3{0.2f, 0.2f, 0.2f};
    Vec3 diffuse  = Vec3{0.8f, 0.8f, 0.8f};
    Vec3 specular = Vec3{0.0f, 0.0f, 0.0f};
    Vec3 emissive = Vec3::zero();
    float shininess = 0.0f;

    // Texture file paths (resolved after MTL parsing)
    std::string diffusePath;
    std::string ambientPath;
    std::string specularPath;
    std::string normalPath;

    // GPU handles (populated after paths are resolved)
    TextureHandle  diffuseMap  = INVALID_TEXTURE;
    SamplerHandle  diffuseSamp = INVALID_SAMPLER;
    TextureHandle  ambientMap  = INVALID_TEXTURE;
    SamplerHandle  ambientSamp = INVALID_SAMPLER;
    TextureHandle  specularMap = INVALID_TEXTURE;
    SamplerHandle  specularSamp = INVALID_SAMPLER;
    TextureHandle  normalMap   = INVALID_TEXTURE;
    SamplerHandle  normalSamp  = INVALID_SAMPLER;

    // Transparency
    float opacity = 1.0f;
    bool  transparent = false;
};

// ---- Sub-mesh ----
// A contiguous range of indices within the shared index buffer,
// rendered with a single material.
struct SubMesh
{
    int indexOffset = 0;   // first index in the shared index buffer
    int indexCount  = 0;   // number of indices in this sub-mesh
    int materialIdx = 0;   // index into the parent mesh's material list
};

// ---- Abstract mesh interface ----
// Base class for all mesh types (static, animated, etc.).
// Allows ModelRenderer to hold heterogeneous meshes.
class IMesh
{
public:
    virtual ~IMesh() = default;
    virtual void release(IRenderBackend* backend) = 0;

    // Draw the entire mesh.
    virtual void draw(IRenderBackend* backend) const = 0;

    int  numVertices()  const { return m_numVerts; }
    int  numTriangles() const { return m_numTris; }

protected:
    int m_numVerts = 0;
    int m_numTris  = 0;
};

// ---- StaticMesh ----
// Generic static geometry with one vertex buffer, one index buffer,
// and multiple sub-meshes (each with its own material).
// Designed for OBJ+MTL loading, but reusable for any static format.
class StaticMesh : public IMesh
{
public:
    // Build from CPU-side vertex + index data.
    bool build(IRenderBackend* backend,
               const VertexPacked* vertices, int numVerts,
               const uint32_t* indices, int numIndices,
               const SubMesh* subMeshes, int numSubMeshes,
               const Material* materials, int numMaterials);

    // Build by parsing an OBJ file (with MTL material references).
    bool build(IRenderBackend* backend, AssetFS* assets, const char* objPath);

    void release(IRenderBackend* backend) override;

    // Draw the entire mesh (all sub-meshes).
    void draw(IRenderBackend* backend) const override;

    // Draw all sub-meshes in sequence.
    void drawAll(IRenderBackend* backend) const;

    // Upload a single sub-mesh's indices bound to its material, then draw.
    void drawSubMesh(IRenderBackend* backend, int subMeshIdx) const;

    int numSubMeshes()  const { return (int)m_subMeshes.size(); }
    int numMaterials()  const { return (int)m_materials.size(); }

    const SubMesh&  subMesh(int i) const { return m_subMeshes[i]; }
    const Material& material(int i) const { return m_materials[i]; }

    BufferHandle vertexBuffer() const { return m_vertexBuffer; }
    BufferHandle indexBuffer()  const { return m_indexBuffer; }

private:
    BufferHandle       m_vertexBuffer = INVALID_BUFFER;
    BufferHandle       m_indexBuffer  = INVALID_BUFFER;
    std::vector<SubMesh>   m_subMeshes;
    std::vector<Material>  m_materials;
};

// ---- MeshInstance ----
// Per-entity transform and render state, independent of mesh type.
struct MeshInstance
{
    Vec3  origin = Vec3::zero();
    Vec3  angles = Vec3::zero(); // pitch, yaw, roll (radians)
    float scale  = 1.0f;

    Mat4 worldMatrix() const
    {
        // Rotation order: yaw (Y) * pitch (X) * roll (Z)
        Mat4 rotY = Mat4::rotate(angles.y, Vec3::up());
        Mat4 rotX = Mat4::rotate(angles.x, Vec3::right());
        Mat4 rotZ = Mat4::rotate(angles.z, Vec3::forward());
        Mat4 rot = rotY * rotX * rotZ;

        Mat4 trans = Mat4::translate(origin);
        Mat4 sc = Mat4::scale(Vec3{scale, scale, scale});

        return trans * rot * sc;
    }
};

} // namespace nova
