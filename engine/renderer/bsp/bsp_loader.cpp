// ============================================================
// FILE:    engine/renderer/bsp/bsp_loader.cpp
// MODULE:  Renderer > BSP
// PHASE:   1
// PURPOSE: Load and render Quake 2 .bsp maps (v38 vanilla + v46 KEX).
// DEPENDS: renderer/bsp.h
//
// Memory limits to prevent std::bad_alloc on large maps:
//   - kMaxVerts: max 4M vertices (~352MB vertex data)
//   - kMaxIndices: max 12M indices (~48MB index data)
// ============================================================

#include "engine/renderer/bsp/bsp.h"
#include "engine/renderer/irender_backend.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <algorithm>

namespace nova
{

// Per-chunk GPU upload limits (controls VRAM per draw call, not total geometry)
constexpr size_t kChunkMaxVerts   =  500'000;
constexpr size_t kChunkMaxIndices = 1'500'000;

// ---------------------------------------------------------------------------
void BSPMap::freeLumps()
{
    m_vertices.clear();
    m_planes.clear();
    m_edges.clear();
    m_surfEdges.clear();
    m_texInfos.clear();
    m_faces.clear();
    m_lightmapData.clear();
    m_models.clear();
    m_nodes.clear();
    m_leaves.clear();
    m_leafFaces.clear();
    m_brushes.clear();
    m_brushSides.clear();
    m_leafBrushes.clear();
    m_entities.clear();
    m_surfaces.clear();
    m_verticesPacked.clear();
    m_indicesRaw.clear();
}

// ---------------------------------------------------------------------------
bool BSPMap::loadLumps(const uint8_t *data, size_t size)
{
    if (size < 8)  // need at least magic(4) + version(4)
    {
        fprintf(stderr, "BSPMap: file too small\n");
        return false;
    }

    const BSPRawHeader *hdr = reinterpret_cast<const BSPRawHeader *>(data);

    // Accept "IBSP" (vanilla Q2/KEX) or "QBSP" (some community tools)
    if (memcmp(hdr->magic, "IBSP", 4) != 0 &&
        memcmp(hdr->magic, "QBSP", 4) != 0)
    {
        fprintf(stderr, "BSPMap: wrong magic '%.4s'\n", hdr->magic);
        return false;
    }

    if (hdr->version != 38 && hdr->version != 46)
    {
        fprintf(stderr, "BSPMap: unsupported version %d\n", hdr->version);
        return false;
    }

    fprintf(stdout, "BSPMap: loading version %d (%s)\n",
            hdr->version,
            hdr->version == 46 ? "KEX Enhanced" : "Vanilla Q2");

    // FIX 15: Only read the correct number of lump table entries.
    // v38 has 19 lumps; the lump table occupies bytes 8..159 (19*8=152 bytes).
    // v46 has 31 lumps; the lump table occupies bytes 8..255 (31*8=248 bytes).
    // The BSPRawHeader struct has space for 31, but if the file is v38, entries
    // 19-30 overlap real lump data and must not be used.
    const int numLumps = (hdr->version == 46) ? BSP_LUMPS_V46 : BSP_LUMPS_V38;

    // Minimum file size: header prefix + lump table
    const size_t minHeaderBytes = 8 + (size_t)numLumps * 8;
    if (size < minHeaderBytes)
    {
        fprintf(stderr, "BSPMap: file too small for %d-lump header (%zu < %zu)\n",
                numLumps, size, minHeaderBytes);
        return false;
    }

    // Build a local safe lump table clamped to actual file bounds.
    // lumps[i] = {byteOffset, byteLength}
    struct LumpEntry { int off, len; };
    LumpEntry lumps[BSP_MAX_LUMPS] = {};

    for (int i = 0; i < numLumps; ++i)
    {
        int off = hdr->lumps[i][0];
        int len = hdr->lumps[i][1];
        if (off < 0 || off > (int)size)  { lumps[i] = {0, 0}; continue; }
        if (len < 0)                      { lumps[i] = {off, 0}; continue; }
        if ((size_t)(off + len) > size)   len = (int)size - off;
        lumps[i] = {off, len};
    }

    auto lump = [&](BSPLump l) -> LumpEntry
    {
        int idx = (int)l;
        if (idx < 0 || idx >= numLumps) return {0, 0};
        return lumps[idx];
    };

    // ---- Planes ----
    // CRITICAL: q2ToGL swaps Y and Z and negates new-Z (old-Y). The plane
    // equation is n·p = dist. After the coordinate transform the dot product
    // is recomputed in GL space, so dist must stay the same IF the transform
    // is a pure rotation/reflection (which it is — det = ±1, preserves
    // distances). The normal direction IS changed by the transform so we
    // apply q2ToGL to the normal, but dist is invariant and must NOT be
    // negated or swapped.
    {
        auto [off, len] = lump(BSPLump::Planes);
        int count = len / (int)sizeof(BSPRawPlane);
        m_planes.resize(count);
        for (int i = 0; i < count; ++i)
        {
            const BSPRawPlane *r = reinterpret_cast<const BSPRawPlane *>(data + off) + i;
            m_planes[i].normal = q2ToGL(r->normal[0], r->normal[1], r->normal[2]);
            m_planes[i].dist   = r->dist;  // invariant under orthogonal transform
        }
    }

    // ---- Vertices (FIX 1: 12 bytes each) ----
    {
        auto [off, len] = lump(BSPLump::Vertices);
        int count = len / (int)sizeof(BSPRawVertex);
        m_vertices.resize(count);
        for (int i = 0; i < count; ++i)
        {
            const BSPRawVertex *r = reinterpret_cast<const BSPRawVertex *>(data + off) + i;
            m_vertices[i] = q2ToGL(r->position[0], r->position[1], r->position[2]);
        }
    }

    // ---- Edges ----
    {
        auto [off, len] = lump(BSPLump::Edges);
        int count = len / (int)sizeof(BSPRawEdge);
        m_edges.resize(count);
        if (count > 0)
            memcpy(m_edges.data(), data + off, count * sizeof(BSPRawEdge));
    }

    // ---- SurfEdges ----
    {
        auto [off, len] = lump(BSPLump::SurfEdges);
        int count = len / (int)sizeof(int32_t);
        m_surfEdges.resize(count);
        if (count > 0)
            memcpy(m_surfEdges.data(), data + off, count * sizeof(int32_t));
    }

    // ---- TexInfo (FIX 3: 76-byte layout) ----
    {
        auto [off, len] = lump(BSPLump::TexInfo);
        int count = len / (int)sizeof(BSPRawTexInfo);
        m_texInfos.resize(count);
        for (int i = 0; i < count; ++i)
        {
            const BSPRawTexInfo *r = reinterpret_cast<const BSPRawTexInfo *>(data + off) + i;
            BSPTexInfo &ti = m_texInfos[i];
            ti.uAxis    = q2ToGL(r->vecs[0][0], r->vecs[0][1], r->vecs[0][2]);
            ti.uOffset  = r->vecs[0][3];
            ti.vAxis    = q2ToGL(r->vecs[1][0], r->vecs[1][1], r->vecs[1][2]);
            ti.vOffset  = r->vecs[1][3];
            ti.flags    = r->flags;
            ti.value    = r->value;
            memcpy(ti.textureName, r->texture, 32);
            ti.textureName[31] = '\0';
            ti.nextTexinfo = r->nexttexinfo;
        }
    }

    // ---- Faces (FIX 2: styles uint8_t) ----
    {
        auto [off, len] = lump(BSPLump::Faces);
        int count = len / (int)sizeof(BSPRawFace);
        m_faces.resize(count);
        for (int i = 0; i < count; ++i)
        {
            const BSPRawFace *r = reinterpret_cast<const BSPRawFace *>(data + off) + i;
            BSPFace &f = m_faces[i];
            f.plane      = r->plane;
            f.sideFacing = (r->side != 0);
            f.firstEdge  = r->firstEdge;
            f.numEdges   = r->numEdges;
            f.texinfo    = r->texinfo;
            f.lightofs   = r->lightofs;
            for (int j = 0; j < 4; ++j)
                f.styles[j] = r->styles[j];
        }
    }

    // ---- Lightmap lump (raw bytes) ----
    {
        auto [off, len] = lump(BSPLump::Lightmaps);
        m_lightmapData.resize((size_t)len);
        if (len > 0)
            memcpy(m_lightmapData.data(), data + off, (size_t)len);

        // Compute per-face extents now that verts + texinfos are loaded.
        for (int i = 0; i < (int)m_faces.size(); ++i)
            computeFaceExtents(i);
    }

    // ---- Models ----
    {
        auto [off, len] = lump(BSPLump::Models);
        int count = len / (int)sizeof(BSPRawModel);
        m_models.resize(count);
        for (int i = 0; i < count; ++i)
        {
            const BSPRawModel *r = reinterpret_cast<const BSPRawModel *>(data + off) + i;
            BSPModel &m = m_models[i];
            for (int j = 0; j < 3; ++j)
            {
                m.mins[j]   = r->mins[j];
                m.maxs[j]   = r->maxs[j];
                m.origin[j] = r->origin[j];
            }
            m.headNode  = r->headNode;
            m.firstFace = r->firstFace;
            m.numFaces  = r->numFaces;
        }
    }

    // ---- Nodes (FIX 4) ----
    {
        auto [off, len] = lump(BSPLump::Nodes);
        int count = len / (int)sizeof(BSPRawNode);
        m_nodes.resize(count);
        for (int i = 0; i < count; ++i)
        {
            const BSPRawNode *r = reinterpret_cast<const BSPRawNode *>(data + off) + i;
            m_nodes[i].plane       = r->plane;
            m_nodes[i].children[0] = r->children[0];
            m_nodes[i].children[1] = r->children[1];
            for (int j = 0; j < 3; ++j)
            {
                m_nodes[i].mins[j] = r->mins[j];
                m_nodes[i].maxs[j] = r->maxs[j];
            }
            m_nodes[i].firstFace = r->firstFace;
            m_nodes[i].numFaces  = r->numFaces;
        }
    }

    // ---- Leaves (FIX 5) ----
    {
        auto [off, len] = lump(BSPLump::Leaves);
        int count = len / (int)sizeof(BSPRawLeaf);
        m_leaves.resize(count);
        for (int i = 0; i < count; ++i)
        {
            const BSPRawLeaf *r = reinterpret_cast<const BSPRawLeaf *>(data + off) + i;
            m_leaves[i].brushOr    = r->brushOr;
            m_leaves[i].cluster    = r->cluster;
            m_leaves[i].area       = r->area;
            m_leaves[i].firstFace  = r->firstFace;
            m_leaves[i].numFaces   = r->numFaces;
            m_leaves[i].firstBrush = r->firstBrush;
            m_leaves[i].numBrushes = r->numBrushes;
            for (int j = 0; j < 3; ++j)
            {
                m_leaves[i].mins[j] = r->mins[j];
                m_leaves[i].maxs[j] = r->maxs[j];
            }
        }
    }

    // ---- LeafFaces (FIX 6: uint16_t entries) ----
    {
        auto [off, len] = lump(BSPLump::LeafFaces);
        int count = len / (int)sizeof(uint16_t);
        m_leafFaces.resize(count);
        const uint16_t *raw = reinterpret_cast<const uint16_t *>(data + off);
        for (int i = 0; i < count; ++i)
            m_leafFaces[i] = raw[i];
    }

    // ---- Brushes (collision geometry) ----
    {
        auto [off, len] = lump(BSPLump::Brushes);
        int count = len / (int)sizeof(BSPRawBrush);
        m_brushes.resize(count);
        for (int i = 0; i < count; ++i)
        {
            const BSPRawBrush *r = reinterpret_cast<const BSPRawBrush *>(data + off) + i;
            m_brushes[i].firstBrushSide = r->firstBrushSide;
            m_brushes[i].numBrushSides = r->numBrushSides;
            m_brushes[i].contents = r->contents;
        }
    }

    // ---- BrushSides ----
    {
        auto [off, len] = lump(BSPLump::BrushSides);
        int count = len / (int)sizeof(BSPRawBrushSide);
        m_brushSides.resize(count);
        for (int i = 0; i < count; ++i)
        {
            const BSPRawBrushSide *r = reinterpret_cast<const BSPRawBrushSide *>(data + off) + i;
            m_brushSides[i].plane = r->plane;
            m_brushSides[i].texinfo = r->texinfo;
        }
    }

    // ---- LeafBrushes (index into Brushes lump) ----
    {
        auto [off, len] = lump(BSPLump::LeafBrushes);
        int count = len / (int)sizeof(uint16_t);
        m_leafBrushes.resize(count);
        const uint16_t *raw = reinterpret_cast<const uint16_t *>(data + off);
        for (int i = 0; i < count; ++i)
            m_leafBrushes[i] = raw[i];
    }

    // ---- Entities ----
    {
        auto [off, len] = lump(BSPLump::Entities);
        if (len > 0)
            m_entities.assign(reinterpret_cast<const char *>(data + off), (size_t)len);
    }

    parseSpawnFromEntities();
    return true;
}

// ---------------------------------------------------------------------------
// Lightmap extent calculation.
//
// IMPORTANT: UV projection must use CONSISTENT coordinate spaces.
// m_vertices[] are already in GL space (q2ToGL applied during loadLumps).
// ti.uAxis/vAxis are also already in GL space (q2ToGL applied in loadLumps).
// Because q2ToGL is an orthogonal transform (det = -1, a reflection), dot
// products are preserved: dot(q2ToGL(p), q2ToGL(axis)) == dot(p, axis).
// So both GL-space and Q2-space give identical UV values — consistent.
//
// The SAME formula is used in buildGeometry() for per-vertex lmUV, so
// lmMins and per-vertex (worldU, worldV) are always in the same space.
void BSPMap::computeFaceExtents(int faceIdx)
{
    BSPFace &face = m_faces[faceIdx];
    if (face.texinfo < 0 || face.texinfo >= (int)m_texInfos.size() ||
        face.numEdges <= 0)
        return;

    const BSPTexInfo &ti = m_texInfos[face.texinfo];

    float minU =  FLT_MAX, minV =  FLT_MAX;
    float maxU = -FLT_MAX, maxV = -FLT_MAX;

    for (int e = 0; e < face.numEdges; ++e)
    {
        int seIdx = face.firstEdge + e;
        if (seIdx < 0 || seIdx >= (int)m_surfEdges.size()) continue;
        int edgeIdx = m_surfEdges[seIdx];
        uint16_t vIdx = (edgeIdx >= 0) ? m_edges[edgeIdx].v0 : m_edges[-edgeIdx].v1;
        if (vIdx >= (uint16_t)m_vertices.size()) continue;

        // m_vertices and ti axes are both GL-space — dot product preserved by q2ToGL
        const Vec3 &p = m_vertices[vIdx];
        float u = p.x*ti.uAxis.x + p.y*ti.uAxis.y + p.z*ti.uAxis.z + ti.uOffset;
        float v = p.x*ti.vAxis.x + p.y*ti.vAxis.y + p.z*ti.vAxis.z + ti.vOffset;

        minU = std::min(minU, u);  maxU = std::max(maxU, u);
        minV = std::min(minV, v);  maxV = std::max(maxV, v);
    }

    if (minU == FLT_MAX) return;

    face.lmMins[0] = std::floor(minU / 16.f) * 16.f;
    face.lmMins[1] = std::floor(minV / 16.f) * 16.f;
    face.lmWidth   = (int)(std::floor(maxU / 16.f) - std::floor(minU / 16.f)) + 1;
    face.lmHeight  = (int)(std::floor(maxV / 16.f) - std::floor(minV / 16.f)) + 1;

    // Guard against degenerate faces
    face.lmWidth  = std::max(1, std::min(face.lmWidth,  512));
    face.lmHeight = std::max(1, std::min(face.lmHeight, 512));
}

// ---------------------------------------------------------------------------
void BSPMap::parseSpawnFromEntities()
{
    // Q2 entity lump format: one or more blocks of the form
    //   { "key" "value" "key" "value" ... }
    // We must scope all key lookups to a single entity block to avoid
    // accidentally reading "origin" or "angle" from an adjacent entity.
    // The old code used bare strstr() on the full entity string, which
    // could bleed across block boundaries on maps with many entities.

    const char* src = m_entities.c_str();

    // Find the first entity block whose body contains the given classname.
    // Returns {blockBegin, blockEnd} pointers into src, or {nullptr, nullptr}.
    auto findBlock = [&](const char* classname) -> std::pair<const char*, const char*>
    {
        const char* p = src;
        while (*p)
        {
            const char* open = strchr(p, '{');
            if (!open) break;
            const char* close = strchr(open + 1, '}');
            if (!close) break;

            // Search for classname only within this block
            const char* c = open + 1;
            while (c < close)
            {
                c = (const char*)memchr(c, classname[0], close - c);
                if (!c) break;
                if (strncmp(c, classname, strlen(classname)) == 0)
                    return {open, close};
                ++c;
            }
            p = close + 1;
        }
        return {nullptr, nullptr};
    };

    auto [blockStart, blockEnd] = findBlock("info_player_deathmatch");
    if (!blockStart)
        std::tie(blockStart, blockEnd) = findBlock("info_player_start");

    if (blockStart && blockEnd)
    {
        // Copy the block into a null-terminated string so sscanf is safe
        std::string block(blockStart, (size_t)(blockEnd - blockStart + 1));
        const char* b = block.c_str();

        const char* originKey = strstr(b, "\"origin\"");
        if (originKey)
        {
            float x = 0.f, y = 0.f, z = 0.f;
            if (sscanf(originKey, "\"origin\" \"%f %f %f\"", &x, &y, &z) == 3)
                m_spawnOrigin = q2ToGL(x, y, z);
        }

        const char* angleKey = strstr(b, "\"angle\"");
        if (!angleKey) angleKey = strstr(b, "\"angles\"");
        if (angleKey)
        {
            float yaw = 0.f;
            if (sscanf(angleKey, "\"angle\" \"%f\"", &yaw) == 1)
                m_spawnAngles = {0.f, yaw, 0.f};
        }
    }

    if (m_spawnOrigin.x == 0.f && m_spawnOrigin.y == 0.f && m_spawnOrigin.z == 0.f)
    {
        if (!m_models.empty())
            m_spawnOrigin = q2ToGL(m_models[0].origin[0],
                                   m_models[0].origin[1],
                                   m_models[0].origin[2]);
    }
}

// ---------------------------------------------------------------------------
bool BSPMap::load(IPlatform *platform, const char *path)
{
    freeLumps();
    m_platform = platform;
    m_path = path;

    FileHandle fh = m_platform->openFile(path, "rb");
    if (!fh.handle)
    {
        fprintf(stderr, "BSPMap: could not open '%s'\n", path);
        return false;
    }

    size_t fileSize = m_platform->getFileSize(fh);
    if (fileSize == 0)
    {
        m_platform->closeFile(fh);
        fprintf(stderr, "BSPMap: file is empty '%s'\n", path);
        return false;
    }

    std::vector<uint8_t> fileData(fileSize);
    size_t bytesRead = m_platform->readFile(fh, fileData.data(), fileSize);
    m_platform->closeFile(fh);

    if (bytesRead < fileSize)
    {
        fprintf(stderr, "BSPMap: short read (%zu / %zu bytes)\n", bytesRead, fileSize);
    }

    if (!loadLumps(fileData.data(), bytesRead))
    {
        fprintf(stderr, "BSPMap: failed to parse lumps from '%s'\n", path);
        return false;
    }

    fprintf(stdout,
            "BSPMap: verts=%zu faces=%zu planes=%zu edges=%zu models=%zu leafFaces=%zu\n"
            "BSPMap: brushes=%zu brushSides=%zu leafBrushes=%zu\n",
            m_vertices.size(), m_faces.size(), m_planes.size(),
            m_edges.size(), m_models.size(), m_leafFaces.size(),
            m_brushes.size(), m_brushSides.size(), m_leafBrushes.size());

    return true;
}
// buildGeometry — triangle-fan tessellation of all BSP faces.
// Lightmap UVs reference the atlas (computed after uploadLightmapAtlas).
// They are filled in during buildGeometry IF atlas data is already packed,
// otherwise deferred to a second pass in uploadToGPU.
void BSPMap::buildGeometry()
{
    m_verticesPacked.clear();
    m_indicesRaw.clear();
    m_surfaces.clear();

    // No limit in buildGeometry - build all geometry
    m_verticesPacked.reserve(m_faces.size() * 6);
    m_indicesRaw.reserve(m_faces.size() * 12);
    m_surfaces.reserve(m_faces.size());

    std::vector<BSPVertexPacked> faceVerts;

    for (int fi = 0; fi < (int)m_faces.size(); ++fi)
    {
        const BSPFace &face = m_faces[fi];
        if (face.numEdges < 3 || face.numEdges > 1024) continue;  // skip degenerate / corrupted faces

        // Skip sky / special surfaces (SURF_SKY = 0x04, SURF_NODRAW = 0x80)
        if (face.texinfo >= 0 && face.texinfo < (int)m_texInfos.size())
        {
            int flags = m_texInfos[face.texinfo].flags;
            if (flags & 0x84) continue;  // SKY | NODRAW
        }

        Vec3 faceNormal = {0.f, 1.f, 0.f};
        if (face.plane < (uint16_t)m_planes.size())
        {
            faceNormal = m_planes[face.plane].normal;
            if (face.sideFacing)
                faceNormal = -faceNormal;
        }

        // Get texinfo for UV computation
        const BSPTexInfo *ti = nullptr;
        if (face.texinfo >= 0 && face.texinfo < (int)m_texInfos.size())
            ti = &m_texInfos[face.texinfo];

        faceVerts.clear();
        faceVerts.reserve(face.numEdges);

        for (int e = 0; e < face.numEdges; ++e)
        {
            int seIdx = face.firstEdge + e;
            if (seIdx < 0 || seIdx >= (int)m_surfEdges.size()) continue;

            int edgeIdx = m_surfEdges[seIdx];
            uint16_t vIdx;
            if (edgeIdx >= 0)
                vIdx = m_edges[edgeIdx].v0;
            else
                vIdx = m_edges[-edgeIdx].v1;

            if (vIdx >= (uint16_t)m_vertices.size()) continue;

            const Vec3 &pos = m_vertices[vIdx];

            // ---- World-space UV from texinfo axes ----
            float worldU = 0.f, worldV = 0.f;
            if (ti)
            {
                worldU = pos.x*ti->uAxis.x + pos.y*ti->uAxis.y + pos.z*ti->uAxis.z + ti->uOffset;
                worldV = pos.x*ti->vAxis.x + pos.y*ti->vAxis.y + pos.z*ti->vAxis.z + ti->vOffset;
            }

            // ---- Lightmap UV into atlas ----
            // Faces without a packed lightmap get lmUV = (-1, -1) as a sentinel.
            // The shader detects x < 0 and uses the unlit fallback colour instead
            // of sampling the atlas corner (which would give wrong lighting).
            float lmU = -1.f, lmV = -1.f;
            if (face.hasAtlas && face.lmWidth > 0 && face.lmHeight > 0)
            {
                float faceU = (worldU - face.lmMins[0]) / 16.f;
                float faceV = (worldV - face.lmMins[1]) / 16.f;
                lmU = ((float)face.atlasX + faceU + 0.5f) / (float)kAtlasSize;
                lmV = ((float)face.atlasY + faceV + 0.5f) / (float)kAtlasSize;
                // Clamp to face region to prevent bilinear bleeding into adjacent entries
                float u0 = ((float)face.atlasX + 0.5f) / (float)kAtlasSize;
                float u1 = ((float)(face.atlasX + face.lmWidth)  - 0.5f) / (float)kAtlasSize;
                float v0 = ((float)face.atlasY + 0.5f) / (float)kAtlasSize;
                float v1 = ((float)(face.atlasY + face.lmHeight) - 0.5f) / (float)kAtlasSize;
                lmU = lmU < u0 ? u0 : (lmU > u1 ? u1 : lmU);
                lmV = lmV < v0 ? v0 : (lmV > v1 ? v1 : lmV);
            }

            // Vertex colour: white for lit faces, dim grey for no-lightmap faces.
            // The position-based colour gradient is too dark for small maps
            // (world coords near 0 → near-black after multiply with lightmap).
            // White lets the lightmap display at full intensity as intended.
            uint8_t cr = 255, cg = 255, cb = 255;
            if (!face.hasAtlas)
            {
                // No lightmap — use a muted tint so these faces are visually distinct
                cr = 80; cg = 80; cb = 100;
            }

            BSPVertexPacked vtx;
            vtx.pos[0] = pos.x; vtx.pos[1] = pos.y; vtx.pos[2] = pos.z;
            vtx.uv[0]  = worldU; vtx.uv[1] = worldV;
            vtx.lmUV[0] = lmU;  vtx.lmUV[1] = lmV;
            vtx.normal[0] = faceNormal.x;
            vtx.normal[1] = faceNormal.y;
            vtx.normal[2] = faceNormal.z;
            vtx.color[0] = cr; vtx.color[1] = cg;
            vtx.color[2] = cb; vtx.color[3] = 255;

            faceVerts.push_back(vtx);
        }

        if ((int)faceVerts.size() < 3) continue;

        // Memory protection removed: geometry is now fully built and split
        // into chunks during uploadToGPU().  No truncation here.

        uint32_t baseVertex = (uint32_t)m_verticesPacked.size();

        Surface surf;
        surf.firstVertex = baseVertex;
        surf.vertCount   = (uint32_t)faceVerts.size();
        surf.indexOffset = (uint32_t)m_indicesRaw.size();
        surf.indexCount  = (int)(faceVerts.size() - 2) * 3;
        surf.faceIndex   = fi;

        // Check winding: compute cross product of first edge vectors
        const auto &v0 = faceVerts[0].pos;
        const auto &v1 = faceVerts[1].pos;
        const auto &v2 = faceVerts[2].pos;
        Vec3 e1{ v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2] };
        Vec3 e2{ v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2] };
        Vec3 cross{ e1.y*e2.z - e1.z*e2.y,
                   e1.z*e2.x - e1.x*e2.z,
                   e1.x*e2.y - e1.y*e2.x };
        float dot = cross.x*faceNormal.x + cross.y*faceNormal.y + cross.z*faceNormal.z;
        bool reverse = (dot < 0.0f);  // winding opposite to normal

        for (size_t tri = 0; tri < faceVerts.size() - 2; ++tri)
        {
            if (reverse)
            {
                m_indicesRaw.push_back(baseVertex);
                m_indicesRaw.push_back(baseVertex + (uint32_t)tri + 2);
                m_indicesRaw.push_back(baseVertex + (uint32_t)tri + 1);
            }
            else
            {
                m_indicesRaw.push_back(baseVertex);
                m_indicesRaw.push_back(baseVertex + (uint32_t)tri + 1);
                m_indicesRaw.push_back(baseVertex + (uint32_t)tri + 2);
            }
        }

        m_surfaces.push_back(surf);
        m_verticesPacked.insert(m_verticesPacked.end(),
                                faceVerts.begin(), faceVerts.end());
    }

    fprintf(stdout, "BSPMap: %zu surfaces, %zu verts, %zu indices\n",
            m_surfaces.size(), m_verticesPacked.size(), m_indicesRaw.size());

    // Sanity-check: no index should exceed vertex count
    if (!m_indicesRaw.empty() && !m_verticesPacked.empty())
    {
        size_t badCount = 0;
        uint32_t maxVert = (uint32_t)m_verticesPacked.size();
        for (uint32_t idx : m_indicesRaw)
        {
            if (idx >= maxVert) ++badCount;
        }
        if (badCount > 0)
            fprintf(stderr, "BSPMap: WARNING %zu out-of-range indices!\n", badCount);
        else
            fprintf(stdout, "BSPMap: all %zu indices valid\n", m_indicesRaw.size());
    }
}

// ---------------------------------------------------------------------------
// FIX 16: Lightmap atlas packer.
//
// Algorithm: simple shelf (row) packer.
//   - Walk faces in order; for each face with a valid lightmap, try to fit
//     its (lmWidth+1) x (lmHeight+1) lightmap into the current shelf row.
//     (+1 texel gutter to prevent bilinear bleeding across faces.)
//   - If it doesn't fit horizontally, start a new shelf.
//   - If the atlas fills up vertically, remaining faces get lmU=lmV=0
//     (they'll sample the corner of the atlas — not ideal but won't crash).
//
// Produces one 4096x4096 RGB8 atlas texture.
// Per-face atlasX/atlasY are written during packing so buildGeometry()
// can compute the correct normalised atlas UVs.
//
void BSPMap::uploadLightmapAtlas(IRenderBackend *backend)
{
    if (m_lightmapData.empty()) return;

    // Allocate CPU-side atlas: RGB8
    const int atlasW = kAtlasSize;
    const int atlasH = kAtlasSize;
    std::vector<uint8_t> atlas((size_t)atlasW * atlasH * 3, 0);

    constexpr int kGutter = 1;   // 1-texel gutter between packed lightmaps

    int curX = 0;   // current shelf X cursor
    int curY = 0;   // current shelf Y origin
    int shelfH = 0; // height of the current shelf

    int packed = 0, skipped = 0;
    int noLightofs = 0, zeroDims = 0, outOfBounds = 0;

    for (int fi = 0; fi < (int)m_faces.size(); ++fi)
    {
        BSPFace &face = m_faces[fi];
        if (face.lightofs < 0)            { ++noLightofs; continue; }
        if (face.lmWidth <= 0 || face.lmHeight <= 0) { ++zeroDims;  continue; }

        int faceBytes = face.lmWidth * face.lmHeight * 3;
        if (face.lightofs + faceBytes > (int)m_lightmapData.size())
        {
            ++outOfBounds;
            continue;   // corrupt / truncated data
        }

        int padW = face.lmWidth  + kGutter;
        int padH = face.lmHeight + kGutter;

        // Does this face fit on the current shelf horizontally?
        if (curX + padW > atlasW)
        {
            // Advance to new shelf
            curY  += shelfH + kGutter;
            curX   = 0;
            shelfH = 0;
        }

        // Does it fit vertically?
        if (curY + padH > atlasH)
        {
            // Atlas full — skip remaining lightmaps
            ++skipped;
            continue;
        }

        // Copy the face's lightmap into the atlas
        const uint8_t *src = m_lightmapData.data() + face.lightofs;
        for (int row = 0; row < face.lmHeight; ++row)
        {
            int atlasRow = curY + row;
            int atlasCol = curX;
            uint8_t *dst = atlas.data() + (atlasRow * atlasW + atlasCol) * 3;
            memcpy(dst, src + row * face.lmWidth * 3, (size_t)face.lmWidth * 3);
        }

        face.atlasX   = curX;
        face.atlasY   = curY;
        face.hasAtlas = true;

        curX   += padW;
        shelfH  = std::max(shelfH, padH);
        ++packed;
    }

    fprintf(stdout, "BSPMap: lightmap atlas %dx%d: %d packed, %d skipped, "
            "%d no-lightofs, %d zero-dims, %d out-of-bounds\n",
            atlasW, atlasH, packed, skipped, noLightofs, zeroDims, outOfBounds);

    // Dump first face stats for debugging
    if (!m_faces.empty())
    {
        const BSPFace &f0 = m_faces[0];
        fprintf(stdout, "BSPMap: face[0] lightofs=%d lmW=%d lmH=%d hasAtlas=%d atlasX=%d atlasY=%d\n",
                f0.lightofs, f0.lmWidth, f0.lmHeight, (int)f0.hasAtlas, f0.atlasX, f0.atlasY);
        fprintf(stdout, "BSPMap: lightmapData size=%zu bytes\n", m_lightmapData.size());
    }

    // Upload the atlas as a single texture
    TextureDesc td{};
    td.type       = TextureType::Texture2D;
    td.width      = atlasW;
    td.height     = atlasH;
    td.format     = TextureFormat::RGB8;
    td.minFilter  = TextureFilter::Linear;
    td.magFilter  = TextureFilter::Linear;
    td.wrapU      = TextureWrap::Clamp;
    td.wrapV      = TextureWrap::Clamp;
    td.initialData = atlas.data();

    m_lmAtlasHandle  = backend->createTexture(td);
    m_lmAtlasSampler = backend->createSampler(td);
}

// ---------------------------------------------------------------------------
void BSPMap::uploadToGPU(IRenderBackend *backend)
{
    // Step 1: Pack lightmap atlas (sets face.atlasX/Y/hasAtlas).
    uploadLightmapAtlas(backend);

    // Step 2: Build geometry — full, no truncation.
    buildGeometry();

    if (m_verticesPacked.empty())
    {
        fprintf(stderr, "BSPMap: no geometry to upload\n");
        return;
    }

    // Step 3: Split into GPU chunks so any size map fits in VRAM.
    // Each surface's vertices form a contiguous block in m_verticesPacked;
    // its indices reference only that block.  We re-pack surfaces into
    // chunks and remap indices to chunk-local vertex offsets.
    m_chunks.clear();

    std::vector<BSPVertexPacked> chunkVerts;
    std::vector<uint32_t>        chunkIndices;
    chunkVerts.reserve(kChunkMaxVerts);
    chunkIndices.reserve(kChunkMaxIndices);

    auto flushChunk = [&]()
    {
        if (chunkVerts.empty()) return;

        RenderChunk rc;

        BufferDesc vbDesc{};
        vbDesc.type        = BufferType::Vertex;
        vbDesc.usage       = BufferUsage::Static;
        vbDesc.size        = chunkVerts.size() * sizeof(BSPVertexPacked);
        vbDesc.initialData = chunkVerts.data();
        rc.vertexBuffer    = backend->createBuffer(vbDesc);

        BufferDesc ibDesc{};
        ibDesc.type        = BufferType::Index;
        ibDesc.usage       = BufferUsage::Static;
        ibDesc.size        = chunkIndices.size() * sizeof(uint32_t);
        ibDesc.initialData = chunkIndices.data();
        rc.indexBuffer     = backend->createBuffer(ibDesc);

        rc.indexCount = (int)chunkIndices.size();
        m_chunks.push_back(rc);

        chunkVerts.clear();
        chunkIndices.clear();
    };

    for (const Surface &surf : m_surfaces)
    {
        if (surf.indexCount <= 0) continue;

        // Use explicit firstVertex and vertCount from Surface (not derived from index array)
        uint32_t globalBase = surf.firstVertex;
        uint32_t vertCount  = surf.vertCount;

        // Guard against corrupted surface data
        if (globalBase + vertCount > (uint32_t)m_verticesPacked.size()) continue;

        // Would this surface overflow the current chunk?  Flush first.
        if (!chunkVerts.empty() &&
            (chunkVerts.size()   + vertCount            > kChunkMaxVerts ||
             chunkIndices.size() + (size_t)surf.indexCount > kChunkMaxIndices))
        {
            flushChunk();
        }

        // Remap: subtract global base, add local (chunk) base.
        uint32_t localBase = (uint32_t)chunkVerts.size();

        for (uint32_t v = 0; v < vertCount; ++v)
            chunkVerts.push_back(m_verticesPacked[globalBase + v]);

        for (int i = 0; i < surf.indexCount; ++i)
        {
            uint32_t remapped = m_indicesRaw[surf.indexOffset + i] - globalBase + localBase;
            chunkIndices.push_back(remapped);
        }
    }

    flushChunk();

    // Tally totals for logging
    m_totalVertexCount = (int)m_verticesPacked.size();
    m_totalIndexCount  = 0;
    for (const RenderChunk &c : m_chunks)
        m_totalIndexCount += c.indexCount;

    fprintf(stdout,
            "BSPMap: uploaded %zu chunk(s), %d vertices, %d indices, %zu surfaces\n",
            m_chunks.size(),
            m_totalVertexCount,
            m_totalIndexCount,
            m_surfaces.size());
}

// ---------------------------------------------------------------------------
BSPMap::~BSPMap()
{
    freeLumps();
}

// ---------------------------------------------------------------------------
void BSPMap::render(IRenderBackend *backend)
{
    if (m_chunks.empty())
        return;

    // Bind the single lightmap atlas (slot 0 = uLightmap in shader)
    if (m_lmAtlasHandle != INVALID_TEXTURE)
        backend->bindTexture(m_lmAtlasHandle, m_lmAtlasSampler, 0);

    // Draw all chunks
    for (const auto &chunk : m_chunks)
    {
        if (chunk.vertexBuffer == INVALID_BUFFER || chunk.indexBuffer == INVALID_BUFFER)
            continue;
        backend->bindVertexBuffer(chunk.vertexBuffer, 0, &kLayoutBSP);
        backend->bindIndexBuffer(chunk.indexBuffer);
        backend->drawIndexed(chunk.indexCount, 0);
    }
}

} // namespace nova
