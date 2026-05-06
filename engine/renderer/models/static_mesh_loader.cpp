// ============================================================
// FILE:    engine/renderer/models/static_mesh_loader.cpp
// MODULE:  Renderer > Models
// PHASE:   3
// PURPOSE: StaticMesh build logic + OBJ+MTL parser.
//          Parses Wavefront .obj files with .mtl materials,
//          uploads to GPU via IRenderBackend.
// DEPENDS: models/mesh.h, core/image_load.h, core/asset_fs.h
// ============================================================

#include "engine/renderer/models/mesh.h"
#include "engine/core/image_load.h"
#include "engine/core/log.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace nova
{

// =====================================================================
//  StaticMesh Implementation
// =====================================================================

bool StaticMesh::build(IRenderBackend* backend,
                       const VertexPacked* vertices, int numVerts,
                       const uint32_t* indices, int numIndices,
                       const SubMesh* subMeshes, int numSubMeshes,
                       const Material* materials, int numMaterials)
{
    if (!backend || !vertices || numVerts <= 0 || !indices || numIndices <= 0)
        return false;

    BufferDesc vbDesc{};
    vbDesc.type = BufferType::Vertex;
    vbDesc.usage = BufferUsage::Static;
    vbDesc.size = numVerts * sizeof(VertexPacked);
    vbDesc.initialData = vertices;
    m_vertexBuffer = backend->createBuffer(vbDesc);

    BufferDesc ibDesc{};
    ibDesc.type = BufferType::Index;
    ibDesc.usage = BufferUsage::Static;
    ibDesc.size = numIndices * sizeof(uint32_t);
    ibDesc.initialData = indices;
    m_indexBuffer = backend->createBuffer(ibDesc);

    m_numVerts = numVerts;
    m_numTris = numIndices / 3;

    m_subMeshes.assign(subMeshes, subMeshes + numSubMeshes);
    m_materials.assign(materials, materials + numMaterials);

    return true;
}

void StaticMesh::release(IRenderBackend* backend)
{
    if (!backend) return;
    if (m_vertexBuffer != INVALID_BUFFER) { backend->destroyBuffer(m_vertexBuffer); m_vertexBuffer = INVALID_BUFFER; }
    if (m_indexBuffer != INVALID_BUFFER)  { backend->destroyBuffer(m_indexBuffer);  m_indexBuffer = INVALID_BUFFER; }
    for (auto& mat : m_materials)
    {
        if (mat.diffuseMap != INVALID_TEXTURE)  backend->destroyTexture(mat.diffuseMap);
        if (mat.diffuseSamp != INVALID_SAMPLER) backend->destroySampler(mat.diffuseSamp);
        if (mat.ambientMap != INVALID_TEXTURE)  backend->destroyTexture(mat.ambientMap);
        if (mat.ambientSamp != INVALID_SAMPLER) backend->destroySampler(mat.ambientSamp);
        if (mat.specularMap != INVALID_TEXTURE) backend->destroyTexture(mat.specularMap);
        if (mat.specularSamp != INVALID_SAMPLER) backend->destroySampler(mat.specularSamp);
        if (mat.normalMap != INVALID_TEXTURE)   backend->destroyTexture(mat.normalMap);
        if (mat.normalSamp != INVALID_SAMPLER)  backend->destroySampler(mat.normalSamp);
    }
    m_subMeshes.clear();
    m_materials.clear();
}

void StaticMesh::drawSubMesh(IRenderBackend* backend, int subMeshIdx) const
{
    if (!backend || subMeshIdx < 0 || subMeshIdx >= (int)m_subMeshes.size()) return;

    const SubMesh& sub = m_subMeshes[subMeshIdx];
    backend->bindVertexBuffer(m_vertexBuffer, 0, &kLayoutStatic);
    backend->bindIndexBuffer(m_indexBuffer);

    if (sub.materialIdx >= 0 && sub.materialIdx < (int)m_materials.size())
    {
        const Material& mat = m_materials[sub.materialIdx];
        backend->bindTexture(mat.diffuseMap, mat.diffuseSamp, 1); // slot 1 = uDiffuse
    }

    backend->drawIndexed(sub.indexCount, sub.indexOffset);
}

void StaticMesh::draw(IRenderBackend* backend) const
{
    drawAll(backend);
}

void StaticMesh::drawAll(IRenderBackend* backend) const
{
    for (int i = 0; i < (int)m_subMeshes.size(); ++i)
        drawSubMesh(backend, i);
}

// =====================================================================
//  OBJ+MTL Parser
// =====================================================================

static Vec3 parseVec3(const char* str)
{
    Vec3 v;
    sscanf(str, "%f %f %f", &v.x, &v.y, &v.z);
    return v;
}

static Vec3 parseVec2(const char* str)
{
    Vec3 v; // use Vec3.xy
    v.z = 0;
    sscanf(str, "%f %f", &v.x, &v.y);
    return v;
}

// Simple MTL parser: reads a .mtl file and returns a map of material name -> Material.
static std::unordered_map<std::string, Material> parseMTL(AssetFS* assets, const std::string& mtlPath)
{
    std::unordered_map<std::string, Material> mats;
    std::vector<uint8_t> fileData;

    if (!assets->readAllBytes(mtlPath, fileData))
    {
        Logger::instance().warn("MTL: cannot read '%s'", mtlPath.c_str());
        return mats;
    }

    // Null-terminate for safe string ops
    fileData.push_back('\0');
    const char* data = reinterpret_cast<const char*>(fileData.data());
    const char* end  = data + fileData.size() - 1;

    // Resolve directory for texture paths
    std::string mtlDir;
    size_t slashPos = mtlPath.find_last_of("/\\");
    if (slashPos != std::string::npos)
        mtlDir = mtlPath.substr(0, slashPos + 1);

    Material current;
    current.name = "default";

    const char* lineStart = data;
    while (lineStart < end)
    {
        const char* lineEnd = lineStart;
        while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
            lineEnd++;

        // Skip whitespace and comments
        const char* p = lineStart;
        while (p < lineEnd && (*p == ' ' || *p == '\t'))
            p++;

        if (p >= lineEnd || *p == '#')
        {
            lineStart = lineEnd + 1;
            continue;
        }

        if (strncmp(p, "newmtl ", 7) == 0)
        {
            // Save previous material
            if (!current.name.empty())
                mats[current.name] = current;

            current = Material{};
            current.name = std::string(p + 7, lineEnd - (p + 7));
        }
        else if (strncmp(p, "Ka ", 3) == 0)
        {
            current.ambient = parseVec3(p + 3);
        }
        else if (strncmp(p, "Kd ", 3) == 0)
        {
            current.diffuse = parseVec3(p + 3);
        }
        else if (strncmp(p, "Ks ", 3) == 0)
        {
            current.specular = parseVec3(p + 3);
        }
        else if (strncmp(p, "Ns ", 3) == 0)
        {
            current.shininess = (float)atof(p + 3);
        }
        else if (strncmp(p, "d ", 2) == 0 || strncmp(p, "Tr ", 3) == 0)
        {
            float val = (float)atof(p + (*p == 'T' ? 3 : 2));
            if (*p == 'T')
                current.opacity = 1.0f - val; // Tr is transparency
            else
                current.opacity = val;         // d is dissolve (opacity)
            current.transparent = (current.opacity < 1.0f);
        }
        else if (strncmp(p, "map_Kd ", 7) == 0)
        {
            std::string texName(p + 7, lineEnd - (p + 7));
            while (!texName.empty() && (texName.back() == ' ' || texName.back() == '\t' || texName.back() == '\r'))
                texName.pop_back();
            current.diffusePath = texName;
        }
        else if (strncmp(p, "map_Ka ", 7) == 0)
        {
            std::string texName(p + 7, lineEnd - (p + 7));
            while (!texName.empty() && (texName.back() == ' ' || texName.back() == '\t' || texName.back() == '\r'))
                texName.pop_back();
            current.ambientPath = texName;
        }
        else if (strncmp(p, "map_Ks ", 7) == 0)
        {
            std::string texName(p + 7, lineEnd - (p + 7));
            while (!texName.empty() && (texName.back() == ' ' || texName.back() == '\t' || texName.back() == '\r'))
                texName.pop_back();
            current.specularPath = texName;
        }
        else if (strncmp(p, "map_Bump ", 9) == 0 || strncmp(p, "bump ", 5) == 0)
        {
            std::string texName(p + (*p == 'm' ? 9 : 5), lineEnd - (p + (*p == 'm' ? 9 : 5)));
            while (!texName.empty() && (texName.back() == ' ' || texName.back() == '\t' || texName.back() == '\r'))
                texName.pop_back();
            current.normalPath = texName;
        }

        lineStart = lineEnd + 1;
    }

    // Save last material
    if (!current.name.empty())
        mats[current.name] = current;

    return mats;
}

// Load a texture from the asset filesystem with extension fallback.
static bool loadTextureWithFallback(IRenderBackend* backend, AssetFS* assets,
                                     const std::string& basePath, const std::string& dir,
                                     TextureHandle& texOut, SamplerHandle& sampOut)
{
    std::string fullPath = dir.empty() ? basePath : (dir + basePath);
    const char* exts[] = { nullptr, ".jpg", ".jpeg", ".png", ".tga" };
    ImageRGBA8 img;
    std::vector<uint8_t> fileData;
    bool loaded = false;

    // Try exact path first
    if (assets->readAllBytes(fullPath.c_str(), fileData) &&
        loadImageRGBA8FromMemory(fileData.data(), fileData.size(), img, nullptr))
    {
        loaded = true;
    }
    else
    {
        // If path contains backslashes (broken Windows absolute path from Blender export),
        // fall back to just the filename in the model directory.
        size_t lastSlash = fullPath.find_last_of("/\\");
        if (lastSlash != std::string::npos)
        {
            std::string fileName = fullPath.substr(lastSlash + 1);
            // Strip drive letter garbage like "C:" or ":\"
            size_t colonPos = fileName.find(':');
            if (colonPos != std::string::npos)
                fileName = fileName.substr(colonPos + 1);

            std::string dirOnly = fullPath.substr(0, lastSlash + 1);
            if (dirOnly.empty() && !dir.empty())
                dirOnly = dir;

            std::string trialPath = dirOnly + fileName;

            // Try filename as-is
            if (assets->readAllBytes(trialPath.c_str(), fileData) &&
                loadImageRGBA8FromMemory(fileData.data(), fileData.size(), img, nullptr))
            {
                loaded = true;
            }
            else
            {
                // Try extension variants
                std::string base = trialPath;
                size_t dotPos = base.find_last_of('.');
                if (dotPos != std::string::npos)
                    base = base.substr(0, dotPos);

                for (int e = 1; e < 5 && !loaded; ++e)
                {
                    std::string trial = base + exts[e];
                    if (assets->readAllBytes(trial.c_str(), fileData) &&
                        loadImageRGBA8FromMemory(fileData.data(), fileData.size(), img, nullptr))
                    {
                        loaded = true;
                    }
                }
            }
        }

        // If still not loaded, try original path with extension variants
        if (!loaded)
        {
            std::string base = fullPath;
            size_t dotPos = base.find_last_of('.');
            if (dotPos != std::string::npos)
                base = base.substr(0, dotPos);

            for (int e = 1; e < 5 && !loaded; ++e)
            {
                std::string trial = base + exts[e];
                if (assets->readAllBytes(trial.c_str(), fileData) &&
                    loadImageRGBA8FromMemory(fileData.data(), fileData.size(), img, nullptr))
                {
                    loaded = true;
                }
            }
        }
    }

    if (loaded)
    {
        TextureDesc td{};
        td.type = TextureType::Texture2D;
        td.width = img.width;
        td.height = img.height;
        td.format = TextureFormat::SRGBA8;
        td.minFilter = TextureFilter::Trilinear;
        td.magFilter = TextureFilter::Linear;
        td.wrapU = TextureWrap::Repeat;
        td.wrapV = TextureWrap::Repeat;
        td.mipLevels = 4;
        td.initialData = img.rgba.data();

        texOut = backend->createTexture(td);
        sampOut = backend->createSampler(td);
        return true;
    }

    return false;
}

// ---- OBJ vertex cache key ----
// OBJ allows shared pos/uv/normal indices, but GPU needs unique vertices.
// We deduplicate by (posIdx, uvIdx, normalIdx) tuple.
struct ObjVertKey
{
    int posIdx;
    int uvIdx;
    int normalIdx;

    bool operator==(const ObjVertKey& o) const
    {
        return posIdx == o.posIdx && uvIdx == o.uvIdx && normalIdx == o.normalIdx;
    }
};

struct ObjVertKeyHash
{
    size_t operator()(const ObjVertKey& k) const
    {
        return ((size_t)k.posIdx * 73856093) ^
               ((size_t)k.uvIdx * 19349663) ^
               ((size_t)k.normalIdx * 83492791);
    }
};

// ---- OBJ face vertex reference ----
struct ObjFaceVertex
{
    int posIdx = 0;     // 0-based
    int uvIdx   = -1;   // -1 = missing
    int normIdx = -1;   // -1 = missing
};

// Parse a single face vertex string: "v/vt/vn", "v//vn", "v/vt", or "v"
static ObjFaceVertex parseFaceVertex(const char* str)
{
    ObjFaceVertex fv;
    // Replace '/' with space for easy parsing
    char buf[64];
    strncpy(buf, str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    for (char* c = buf; *c; ++c)
    {
        if (*c == '/') *c = ' ';
    }

    int count = sscanf(buf, "%d %d %d", &fv.posIdx, &fv.uvIdx, &fv.normIdx);
    (void)count;

    // OBJ indices are 1-based, convert to 0-based
    fv.posIdx   -= 1;
    if (fv.uvIdx != -1)   fv.uvIdx   -= 1;
    if (fv.normIdx != -1) fv.normIdx -= 1;

    return fv;
}

// ---- Material group for face grouping ----
struct ObjMatGroup
{
    std::string materialName;
    std::vector<ObjFaceVertex> faces; // flattened: 3 vertices per triangle
};

bool StaticMesh::build(IRenderBackend* backend, AssetFS* assets, const char* objPath)
{
    if (!backend || !assets || !objPath) return false;

    std::vector<uint8_t> fileData;
    if (!assets->readAllBytes(objPath, fileData))
    {
        Logger::instance().error("OBJ: cannot read '%s'", objPath);
        return false;
    }

    // Null-terminate
    fileData.push_back('\0');
    const char* data = reinterpret_cast<const char*>(fileData.data());
    const char* end  = data + fileData.size() - 1;

    // Resolve directory for MTL and texture paths
    std::string objDir;
    {
        std::string pathStr(objPath);
        size_t slashPos = pathStr.find_last_of("/\\");
        if (slashPos != std::string::npos)
            objDir = pathStr.substr(0, slashPos + 1);
    }

    // Parse geometry
    std::vector<Vec3> positions;
    std::vector<Vec3> texcoords; // use .x/.y
    std::vector<Vec3> normals;
    std::vector<ObjMatGroup> matGroups;

    // Start with default material group
    matGroups.emplace_back();
    matGroups.back().materialName = "";

    const char* lineStart = data;
    while (lineStart < end)
    {
        const char* lineEnd = lineStart;
        while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
            lineEnd++;

        const char* p = lineStart;
        while (p < lineEnd && (*p == ' ' || *p == '\t'))
            p++;

        if (p >= lineEnd || *p == '#')
        {
            lineStart = lineEnd + 1;
            continue;
        }

        if (strncmp(p, "v ", 2) == 0)
        {
            positions.push_back(parseVec3(p + 2));
        }
        else if (strncmp(p, "vt ", 3) == 0)
        {
            texcoords.push_back(parseVec2(p + 3));
        }
        else if (strncmp(p, "vn ", 3) == 0)
        {
            normals.push_back(parseVec3(p + 3));
        }
        else if (strncmp(p, "f ", 2) == 0)
        {
            // Parse face: can be triangles or quads (we triangulate)
            char lineBuf[512];
            size_t lineLen = (size_t)(lineEnd - p);
            if (lineLen >= sizeof(lineBuf)) lineLen = sizeof(lineBuf) - 1;
            strncpy(lineBuf, p + 2, lineLen);
            lineBuf[lineLen] = '\0';

            // Split by spaces
            std::vector<ObjFaceVertex> faceVerts;
            char* token = strtok(lineBuf, " \t");
            while (token)
            {
                faceVerts.push_back(parseFaceVertex(token));
                token = strtok(nullptr, " \t");
            }

            // Triangulate (fan for quads/polygons)
            if (faceVerts.size() >= 3)
            {
                ObjMatGroup& group = matGroups.back();
                for (size_t i = 1; i + 1 < faceVerts.size(); ++i)
                {
                    group.faces.push_back(faceVerts[0]);
                    group.faces.push_back(faceVerts[i]);
                    group.faces.push_back(faceVerts[i + 1]);
                }
            }
        }
        else if (strncmp(p, "usemtl ", 7) == 0)
        {
            std::string matName(p + 7, lineEnd - (p + 7));
            // Trim whitespace
            while (!matName.empty() && (matName.back() == ' ' || matName.back() == '\t'))
                matName.pop_back();

            // Check if last group has the same material name and is empty
            if (matGroups.back().materialName == matName && matGroups.back().faces.empty())
            {
                // Reuse existing group
            }
            else
            {
                // Start new group
                matGroups.emplace_back();
                matGroups.back().materialName = matName;
            }
        }
        else if (strncmp(p, "mtllib ", 7) == 0)
        {
            // MTL file reference - parse later
            // We'll handle this after geometry parsing
        }

        lineStart = lineEnd + 1;
    }

    Logger::instance().info("OBJ: parsed '%s' — pos=%zu uv=%zu norm=%zu groups=%zu",
                            objPath, positions.size(), texcoords.size(), normals.size(), matGroups.size());

    // Parse MTL files if referenced
    std::unordered_map<std::string, Material> mtlMap;
    // Re-scan for mtllib lines
    lineStart = data;
    while (lineStart < end)
    {
        const char* lineEnd = lineStart;
        while (lineEnd < end && *lineEnd != '\n' && *lineEnd != '\r')
            lineEnd++;

        const char* p = lineStart;
        while (p < lineEnd && (*p == ' ' || *p == '\t'))
            p++;

        if (p < lineEnd && strncmp(p, "mtllib ", 7) == 0)
        {
            std::string mtlFile(p + 7, lineEnd - (p + 7));
            while (!mtlFile.empty() && (mtlFile.back() == ' ' || mtlFile.back() == '\t' || mtlFile.back() == '\r'))
                mtlFile.pop_back();

            std::string mtlPath = objDir.empty() ? mtlFile : (objDir + mtlFile);
            auto parsed = parseMTL(assets, mtlPath);
            mtlMap.insert(parsed.begin(), parsed.end());
        }

        lineStart = lineEnd + 1;
    }

    // Build GPU-ready vertex + index data, grouped by material
    // Unique vertex cache: (posIdx, uvIdx, normIdx) -> packed vertex index
    std::unordered_map<ObjVertKey, int, ObjVertKeyHash> vertCache;
    std::vector<VertexPacked> verts;
    std::vector<uint32_t> indices;
    std::vector<SubMesh> subMeshes;
    std::vector<Material> materials;

    int subMeshStartIdx = 0;

    for (const auto& group : matGroups)
    {
        if (group.faces.empty()) continue;

        // Find or create material
        int matIdx = -1;
        for (int i = 0; i < (int)materials.size(); ++i)
        {
            if (materials[i].name == group.materialName)
            {
                matIdx = i;
                break;
            }
        }

        if (matIdx < 0)
        {
            Material mat;
            mat.name = group.materialName;

            // Look up MTL data
            if (!group.materialName.empty())
            {
                auto it = mtlMap.find(group.materialName);
                if (it != mtlMap.end())
                {
                    const Material& src = it->second;
                    mat.ambient = src.ambient;
                    mat.diffuse = src.diffuse;
                    mat.specular = src.specular;
                    mat.shininess = src.shininess;
                    mat.opacity = src.opacity;
                    mat.transparent = src.transparent;
                    mat.diffusePath = src.diffusePath;
                    mat.ambientPath = src.ambientPath;
                    mat.specularPath = src.specularPath;
                    mat.normalPath = src.normalPath;
                }
            }

            matIdx = (int)materials.size();
            materials.push_back(mat);
        }

        // Add faces to vertex/index buffers
        for (size_t i = 0; i < group.faces.size(); ++i)
        {
            const ObjFaceVertex& fv = group.faces[i];
            ObjVertKey key{fv.posIdx, fv.uvIdx, fv.normIdx};

            auto it = vertCache.find(key);
            if (it != vertCache.end())
            {
                indices.push_back((uint32_t)it->second);
            }
            else
            {
                int vertIdx = (int)verts.size();
                vertCache[key] = vertIdx;
                indices.push_back((uint32_t)vertIdx);

                VertexPacked vp{};

                // Position
                if (fv.posIdx >= 0 && fv.posIdx < (int)positions.size())
                {
                    const Vec3& p = positions[fv.posIdx];
                    vp.pos[0] = p.x; vp.pos[1] = p.y; vp.pos[2] = p.z;
                }

                // UV
                if (fv.uvIdx >= 0 && fv.uvIdx < (int)texcoords.size())
                {
                    const Vec3& t = texcoords[fv.uvIdx];
                    vp.uv[0] = t.x;
                    vp.uv[1] = 1.0f - t.y; // OBJ V is bottom-up, flip for OpenGL
                }
                else
                {
                    vp.uv[0] = 0; vp.uv[1] = 0;
                }

                // Normal
                if (fv.normIdx >= 0 && fv.normIdx < (int)normals.size())
                {
                    const Vec3& n = normals[fv.normIdx];
                    vp.normal[0] = n.x; vp.normal[1] = n.y; vp.normal[2] = n.z;
                }
                else
                {
                    vp.normal[0] = 0; vp.normal[1] = 1; vp.normal[2] = 0; // default up
                }

                // Sentinel: no lightmap, white color
                vp.lmUV[0] = -1; vp.lmUV[1] = -1;
                vp.color[0] = 255; vp.color[1] = 255; vp.color[2] = 255; vp.color[3] = 255;

                verts.push_back(vp);
            }
        }

        // Record sub-mesh
        int subIdx = (int)indices.size() - subMeshStartIdx;
        SubMesh sub;
        sub.indexOffset = subMeshStartIdx;
        sub.indexCount = subIdx;
        sub.materialIdx = matIdx;
        subMeshes.push_back(sub);
        subMeshStartIdx = (int)indices.size();
    }

    // Load textures for materials
    for (auto& mat : materials)
    {
        if (!mat.diffusePath.empty() && loadTextureWithFallback(backend, assets, mat.diffusePath, objDir, mat.diffuseMap, mat.diffuseSamp))
            Logger::instance().info("OBJ: loaded diffuse '%s' for material '%s'", mat.diffusePath.c_str(), mat.name.c_str());
        if (!mat.ambientPath.empty() && loadTextureWithFallback(backend, assets, mat.ambientPath, objDir, mat.ambientMap, mat.ambientSamp))
            Logger::instance().info("OBJ: loaded ambient '%s' for material '%s'", mat.ambientPath.c_str(), mat.name.c_str());
        if (!mat.specularPath.empty() && loadTextureWithFallback(backend, assets, mat.specularPath, objDir, mat.specularMap, mat.specularSamp))
            Logger::instance().info("OBJ: loaded specular '%s' for material '%s'", mat.specularPath.c_str(), mat.name.c_str());
        if (!mat.normalPath.empty() && loadTextureWithFallback(backend, assets, mat.normalPath, objDir, mat.normalMap, mat.normalSamp))
            Logger::instance().info("OBJ: loaded normal '%s' for material '%s'", mat.normalPath.c_str(), mat.name.c_str());
    }

    // Build GPU resources
    if (!build(backend, verts.data(), (int)verts.size(),
               indices.data(), (int)indices.size(),
               subMeshes.data(), (int)subMeshes.size(),
               materials.data(), (int)materials.size()))
    {
        return false;
    }

    Logger::instance().info("OBJ: loaded '%s' (%d verts, %d tris, %d materials, %d subMeshes)",
                            objPath, m_numVerts, m_numTris,
                            (int)materials.size(), (int)subMeshes.size());

    return true;
}

} // namespace nova
