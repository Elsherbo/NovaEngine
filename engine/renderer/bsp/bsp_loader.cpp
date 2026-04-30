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
#include "engine/core/image_load.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <algorithm>
#include <unordered_map>
#include <array>
#include <unordered_set>

namespace nova
{

namespace
{
    struct FrustumPlanes
    {
        // plane equation: n.x*x + n.y*y + n.z*z + d >= 0 means inside
        float p[6][4]{};
    };

    static FrustumPlanes extractFrustum(const Mat4& m)
    {
        FrustumPlanes f{};
        auto M = [&](int r, int c) { return m.col[c][r]; };

        // Left:  row3 + row0
        f.p[0][0] = M(3,0) + M(0,0); f.p[0][1] = M(3,1) + M(0,1); f.p[0][2] = M(3,2) + M(0,2); f.p[0][3] = M(3,3) + M(0,3);
        // Right: row3 - row0
        f.p[1][0] = M(3,0) - M(0,0); f.p[1][1] = M(3,1) - M(0,1); f.p[1][2] = M(3,2) - M(0,2); f.p[1][3] = M(3,3) - M(0,3);
        // Bottom:row3 + row1
        f.p[2][0] = M(3,0) + M(1,0); f.p[2][1] = M(3,1) + M(1,1); f.p[2][2] = M(3,2) + M(1,2); f.p[2][3] = M(3,3) + M(1,3);
        // Top:   row3 - row1
        f.p[3][0] = M(3,0) - M(1,0); f.p[3][1] = M(3,1) - M(1,1); f.p[3][2] = M(3,2) - M(1,2); f.p[3][3] = M(3,3) - M(1,3);
        // Near:  row3 + row2
        f.p[4][0] = M(3,0) + M(2,0); f.p[4][1] = M(3,1) + M(2,1); f.p[4][2] = M(3,2) + M(2,2); f.p[4][3] = M(3,3) + M(2,3);
        // Far:   row3 - row2
        f.p[5][0] = M(3,0) - M(2,0); f.p[5][1] = M(3,1) - M(2,1); f.p[5][2] = M(3,2) - M(2,2); f.p[5][3] = M(3,3) - M(2,3);

        // normalize planes
        for (int i = 0; i < 6; ++i)
        {
            float nx = f.p[i][0], ny = f.p[i][1], nz = f.p[i][2];
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            if (len > 1e-6f)
            {
                f.p[i][0] /= len; f.p[i][1] /= len; f.p[i][2] /= len; f.p[i][3] /= len;
            }
        }
        return f;
    }

    static bool aabbInFrustum(const FrustumPlanes& f, const Vec3& bmin, const Vec3& bmax)
    {
        for (int i = 0; i < 6; ++i)
        {
            const float nx = f.p[i][0], ny = f.p[i][1], nz = f.p[i][2], d = f.p[i][3];
            // positive vertex for this normal
            const float px = (nx >= 0.f) ? bmax.x : bmin.x;
            const float py = (ny >= 0.f) ? bmax.y : bmin.y;
            const float pz = (nz >= 0.f) ? bmax.z : bmin.z;
            if (nx * px + ny * py + nz * pz + d < 0.f)
                return false;
        }
        return true;
    }
}

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
    m_visData.clear();
    m_numClusters  = 0;
    m_clusterBytes = 0;
    m_lastPVSCluster = -2;
    m_cachedPVS.clear();
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

    // Accept "IBSP" (Quake II / KEX) or "QBSP" (some community tools).
    // If this fails, try to recognize other common BSP formats so the user
    // gets a useful error message.
    if (memcmp(hdr->magic, "IBSP", 4) != 0 &&
        memcmp(hdr->magic, "QBSP", 4) != 0)
    {
        // Quake 1 BSP starts with a 32-bit version integer (no magic).
        // The most common version is 29 (0x1D).
        const uint32_t v = *reinterpret_cast<const uint32_t*>(data);
        if (v == 29u)
        {
            fprintf(stderr,
                    "BSPMap: unsupported BSP format (Quake 1 BSP v29). "
                    "This loader currently supports Quake II BSP v38/v46 only.\n");
            return false;
        }

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

    // ---- Visibility lump (PVS / PHS) ----
    // Layout after the 8-byte header:
    //   int32 numClusters
    //   int32 clusterSize  (sizeof each offset-table entry, always 8 for Q2)
    //   then numClusters * 8 bytes of offset pairs {pvsByteOffset, phasByteOffset}
    //   then variable-length RLE-compressed PVS data
    // We store the entire lump raw so decompressPVS can seek into it.
    {
        auto [off, len] = lump(BSPLump::Visibility);
        if (len >= 8)
        {
            m_visData.resize((size_t)len);
            memcpy(m_visData.data(), data + off, (size_t)len);

            int32_t nc, cs;
            memcpy(&nc, m_visData.data() + 0, 4);
            memcpy(&cs, m_visData.data() + 4, 4);
            if (nc > 0 && nc <= 65536)
            {
                m_numClusters  = nc;
                m_clusterBytes = (nc + 7) / 8;
                fprintf(stdout, "BSPMap: vis lump: %d clusters, %d bytes/row\n",
                        m_numClusters, m_clusterBytes);
            }
        }
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
// findLeaf — walk BSP tree from root to find which leaf contains `pos`.
// m_planes are already in GL space; cameraPos from getPosition() is also GL space.
// Children encoding: child >= 0 is a node index; child < 0 is ~leafIndex.
int BSPMap::findLeaf(const Vec3& pos) const
{
    int nodeIdx = 0; // root
    while (nodeIdx >= 0)
    {
        if (nodeIdx >= (int)m_nodes.size()) return -1; // guard
        const BSPNode& node = m_nodes[nodeIdx];
        if (node.plane < 0 || node.plane >= (int)m_planes.size()) return -1;
        const BSPPlane& pl = m_planes[node.plane];
        float d = pos.x*pl.normal.x + pos.y*pl.normal.y + pos.z*pl.normal.z - pl.dist;
        nodeIdx = node.children[d < 0.f ? 1 : 0];
    }
    // Negative child: ~nodeIdx encodes the leaf index.
    int leafIdx = ~nodeIdx;
    if (leafIdx < 0 || leafIdx >= (int)m_leaves.size()) return -1;
    return leafIdx;
}

// ---------------------------------------------------------------------------
// decompressPVS — RLE-decode the PVS bitset for `cluster` into `out`.
// Q2 vis lump layout (offsets are relative to start of m_visData):
//   [0]  int32  numClusters
//   [4]  int32  clusterSize  (always 8 — size of each offset-table entry)
//   [8]  numClusters * 8 bytes: {int32 pvsByteOffset, int32 phasByteOffset}
//        offsets are relative to the start of m_visData.
//   [...] variable-length RLE bitstream data
//
// RLE rule: non-zero byte = copy directly; 0x00 followed by byte N = N zero bytes.
void BSPMap::decompressPVS(int cluster, std::vector<uint8_t>& out) const
{
    out.assign((size_t)m_clusterBytes, 0);
    if (m_visData.empty() || cluster < 0 || cluster >= m_numClusters) return;

    // Offset table entry for this cluster starts at byte 8 + cluster*8.
    const size_t entryOff = 8 + (size_t)cluster * 8;
    if (entryOff + 4 > m_visData.size()) return;

    int32_t pvsByteOffset;
    memcpy(&pvsByteOffset, m_visData.data() + entryOff, 4);
    if (pvsByteOffset <= 0 || (size_t)pvsByteOffset >= m_visData.size()) return;

    const uint8_t* src = m_visData.data() + pvsByteOffset;
    const uint8_t* end = m_visData.data() + m_visData.size();
    int outIdx = 0;

    while (outIdx < m_clusterBytes && src < end)
    {
        uint8_t b = *src++;
        if (b != 0)
        {
            out[outIdx++] = b;
        }
        else
        {
            if (src >= end) break;
            int run = (int)(*src++);
            // Guard: don't write past end of out
            run = std::min(run, m_clusterBytes - outIdx);
            for (int i = 0; i < run; ++i)
                out[outIdx++] = 0;
        }
    }
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
            // Normalize to texture space so sampling works with real images.
            // Quake-style mapping uses worldU/V in texels; dividing by texture size gives UVs.
            if (ti && ti->texWidth > 0 && ti->texHeight > 0)
            {
                vtx.uv[0] = worldU / (float)ti->texWidth;
                vtx.uv[1] = worldV / (float)ti->texHeight;
            }
            else
            {
                vtx.uv[0] = worldU / 128.0f;
                vtx.uv[1] = worldV / 128.0f;
            }
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

        // Assign the PVS cluster for this surface by scanning leaves.
        // O(leaves) per face — acceptable at load time.
        for (int li = 0; li < (int)m_leaves.size(); ++li)
        {
            const BSPLeaf& lf = m_leaves[li];
            for (int lfi = 0; lfi < (int)lf.numFaces; ++lfi)
            {
                int lfIdx = (int)lf.firstFace + lfi;
                if (lfIdx < (int)m_leafFaces.size() &&
                    (int)m_leafFaces[lfIdx] == fi)
                {
                    surf.clusterIndex = (int)lf.cluster;
                    goto clusterFound;
                }
            }
        }
        clusterFound:

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

    // Upload the atlas as a single texture.
    // FIX 5: Lightmap atlas now gets anisotropic filtering automatically from
    // createSampler (Bug #2 fix). mipLevels=4 caps the mip chain at level 4
    // (4096 -> 256 px) to prevent deep-level cross-face colour bleed. Levels
    // 5+ collapse many packed face lightmaps into single texels; the 1-texel
    // gutter can't protect against bleed at that resolution.
    //
    // FIX 6 (mipLevels semantic): the convention across this codebase is:
    //   mipLevels == 1  -> single level, no mip chain (solid/fallback textures)
    //   mipLevels >= 2  -> generate full mip chain via glGenerateMipmap
    // The value is also used in createSampler to select the mipmap filter mode
    // (mipLevels > 1 -> GL_LINEAR_MIPMAP_LINEAR, aniso enabled).
    // A future refactor should replace this with an explicit `generateMips` bool
    // and a separate `maxMipLevel` field for immutable storage (glTexStorage2D).
    TextureDesc td{};
    td.type        = TextureType::Texture2D;
    td.width       = atlasW;
    td.height      = atlasH;
    td.format      = TextureFormat::RGB8;
    td.minFilter   = TextureFilter::Trilinear;
    td.magFilter   = TextureFilter::Linear;
    td.wrapU       = TextureWrap::Clamp;
    td.wrapV       = TextureWrap::Clamp;
    td.mipLevels   = 4;        // generate chain, cap at 4 levels (4096->256px)
    td.initialData = atlas.data();

    m_lmAtlasHandle  = backend->createTexture(td);
    m_lmAtlasSampler = backend->createSampler(td);

    // White fallback texture for surfaces without textures
    uint8_t whitePx[3] = {255, 255, 255};
    TextureDesc wt{};
    wt.type = TextureType::Texture2D;
    wt.width = wt.height = 1;
    wt.format = TextureFormat::RGB8;
    wt.minFilter = TextureFilter::Nearest;
    wt.magFilter = TextureFilter::Nearest;
    wt.wrapU = TextureWrap::Repeat;
    wt.wrapV = TextureWrap::Repeat;
    wt.mipLevels = 1;
    wt.initialData = whitePx;
    m_whiteFallback = backend->createTexture(wt);
    m_whiteFallbackSampler = backend->createSampler(wt);
}

// ---------------------------------------------------------------------------
void BSPMap::uploadToGPU(IRenderBackend *backend)
{
    // Re-upload path: release old GPU resources first.
    releaseGPU(backend);
    m_gpuBackend = backend;

    // Step 1: Pack lightmap atlas (sets face.atlasX/Y/hasAtlas).
    uploadLightmapAtlas(backend);

    // Step 1.5: Load diffuse textures referenced by texinfo (best-effort).
    // This is intentionally simple: look for TGA files under a nearby "textures/" folder.
    std::unordered_map<std::string, TextureHandle> texCache;
    std::unordered_map<std::string, std::pair<int,int>> sizeCache;

    // Quake2 WAL textures are indexed color and rely on the global palette
    // stored in pics/colormap.pcx (last 769 bytes: 0x0C + 256*RGB).
    std::array<uint8_t, 768> q2Palette{};
    bool hasQ2Palette = false;
    if (m_assets)
    {
        std::vector<uint8_t> colormap;
        if (m_assets->readAllBytes("pics/colormap.pcx", colormap) &&
            colormap.size() >= 769 &&
            colormap[colormap.size() - 769] == 12)
        {
            std::memcpy(q2Palette.data(), colormap.data() + (colormap.size() - 768), 768);
            hasQ2Palette = true;
        }
    }
    if (!hasQ2Palette)
    {
        // Fallback grayscale palette (keeps WALs visible if colormap missing).
        for (int i = 0; i < 256; ++i)
        {
            q2Palette[i * 3 + 0] = (uint8_t)i;
            q2Palette[i * 3 + 1] = (uint8_t)i;
            q2Palette[i * 3 + 2] = (uint8_t)i;
        }
    }

    auto makeSolidTexture = [&](uint8_t r, uint8_t g, uint8_t b) -> TextureHandle
    {
        uint8_t px[3] = {r, g, b};
        TextureDesc td{};
        td.type      = TextureType::Texture2D;
        td.width     = td.height = 1;
        td.format    = TextureFormat::RGB8;
        td.minFilter = TextureFilter::Nearest;
        td.magFilter = TextureFilter::Nearest;
        td.wrapU     = TextureWrap::Repeat;
        td.wrapV     = TextureWrap::Repeat;
        // FIX 4: 1x1 solid-colour fallbacks have exactly one texel — mipLevels=1
        // means "single level, no mip chain". The old mipLevels=2 was triggering
        // glGenerateMipmap on a 1-pixel texture (harmless but wasteful and
        // semantically wrong). mipLevels=1 also skips the aniso path in createSampler,
        // which is correct: anisotropic filtering has no effect at a single texel.
        td.mipLevels  = 1;
        td.initialData = px;
        return backend->createTexture(td);
    };

    auto hashColor = [&](const char* s) -> std::array<uint8_t,3>
    {
        uint32_t h = 2166136261u;
        for (const char* p = s; p && *p; ++p)
            h = (h ^ (uint8_t)*p) * 16777619u;
        uint8_t r = (uint8_t)(50 + (h & 0x7Fu));
        uint8_t g = (uint8_t)(50 + ((h >> 8) & 0x7Fu));
        uint8_t b = (uint8_t)(50 + ((h >> 16) & 0x7Fu));
        return {r,g,b};
    };

    SamplerHandle defaultSampler = INVALID_SAMPLER;
    {
        TextureDesc sd{};
        // FIX: mipLevels must be > 1 so createSampler uses GL_LINEAR_MIPMAP_LINEAR
        // (trilinear) instead of GL_NEAREST (no mipmap at all).
        // Without this the sampler object overrides the texture's own filter and
        // samples from full-resolution mip-0 at every distance → shimmering.
        // magFilter stays Nearest for the classic Quake pixel-art look up close.
        sd.minFilter  = TextureFilter::Linear;   // → GL_LINEAR_MIPMAP_LINEAR
        sd.magFilter  = TextureFilter::Nearest;  // sharp up close
        sd.wrapU      = TextureWrap::Repeat;
        sd.wrapV      = TextureWrap::Repeat;
        sd.mipLevels  = 2;                       // flag: mipmaps exist, use mip filter
        defaultSampler = backend->createSampler(sd);
    }

    auto tryLoadImage = [&](const std::string& logicalPath, TextureHandle& outTex, int& outW, int& outH) -> bool
    {
        ImageRGBA8 img;
        std::string err;

        if (m_assets)
        {
            std::vector<uint8_t> bytes;
            if (!m_assets->readAllBytes(logicalPath, bytes))
                return false;
            if (!loadImageRGBA8FromMemory(bytes.data(), bytes.size(), img, &err))
                return false;
        }
        else
        {
            // Fallback to direct filesystem reads (dev-only path).
            if (!loadImageRGBA8FromFile(logicalPath.c_str(), img, &err))
                return false;
        }

        TextureDesc td{};
        td.type      = TextureType::Texture2D;
        td.width     = img.width;
        td.height    = img.height;
        // SRGBA8: tells OpenGL this data is gamma-encoded (which .tga/.png always is).
        // The hardware auto-decodes to linear on every texture read so all
        // lighting math in the shader happens in correct linear space.
        // GL_FRAMEBUFFER_SRGB then re-encodes to sRGB on framebuffer write.
        td.format    = TextureFormat::SRGBA8;
        td.minFilter = TextureFilter::Linear;
        td.magFilter = TextureFilter::Nearest;
        td.wrapU     = TextureWrap::Repeat;
        td.wrapV     = TextureWrap::Repeat;
        td.mipLevels = 2;
        td.initialData = img.rgba.data();
        outTex = backend->createTexture(td);
        outW = img.width;
        outH = img.height;
        return outTex != INVALID_TEXTURE;
    };

    auto tryLoadWal = [&](const std::string& logicalPath, TextureHandle& outTex, int& outW, int& outH) -> bool
    {
        std::vector<uint8_t> bytes;
        if (m_assets)
        {
            if (!m_assets->readAllBytes(logicalPath, bytes))
                return false;
        }
        else
        {
            // Filesystem fallback path.
            FILE* f = fopen(logicalPath.c_str(), "rb");
            if (!f) return false;
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz <= 0) { fclose(f); return false; }
            bytes.resize((size_t)sz);
            const size_t rd = fread(bytes.data(), 1, (size_t)sz, f);
            fclose(f);
            if (rd != (size_t)sz) return false;
        }

        auto readI32 = [&](size_t off) -> int32_t
        {
            if (off + 4 > bytes.size()) return 0;
            const uint8_t* p = bytes.data() + off;
            return (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
        };

        // Quake2 miptex wal header:
        // name[32], width(4), height(4), offsets[4](16), animname[32], flags, contents, value
        if (bytes.size() < 100) return false;
        const int w = readI32(32);
        const int h = readI32(36);
        const int ofs0 = readI32(40);
        if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
        if (ofs0 <= 0) return false;
        const size_t mip0Size = (size_t)w * (size_t)h;
        if ((size_t)ofs0 + mip0Size > bytes.size()) return false;

        std::vector<uint8_t> rgba(mip0Size * 4);
        const uint8_t* src = bytes.data() + (size_t)ofs0;
        for (size_t i = 0; i < mip0Size; ++i)
        {
            const uint8_t idx = src[i];
            rgba[i * 4 + 0] = q2Palette[idx * 3 + 0];
            rgba[i * 4 + 1] = q2Palette[idx * 3 + 1];
            rgba[i * 4 + 2] = q2Palette[idx * 3 + 2];
            rgba[i * 4 + 3] = 255;
        }

        TextureDesc td{};
        td.type      = TextureType::Texture2D;
        td.width     = w;
        td.height    = h;
        // SRGBA8: WAL palette entries are sRGB values (the Q2 palette is gamma-encoded).
        // Decoding palette index -> RGB gives sRGB bytes, so we must declare SRGBA8
        // so the hardware linearises them on read, matching the .tga path.
        td.format    = TextureFormat::SRGBA8;
        td.minFilter = TextureFilter::Linear;
        td.magFilter = TextureFilter::Nearest;
        td.wrapU     = TextureWrap::Repeat;
        td.wrapV     = TextureWrap::Repeat;
        td.mipLevels = 2;
        td.initialData = rgba.data();
        outTex = backend->createTexture(td);
        outW = w;
        outH = h;
        return outTex != INVALID_TEXTURE;
    };

    auto getTextureForName = [&](const char* texName, int& outW, int& outH) -> TextureHandle
    {
        if (!texName || texName[0] == '\0')
        {
            outW = outH = 128;
            return makeSolidTexture(180, 180, 180);
        }

        const std::string key(texName);
        if (auto it = texCache.find(key); it != texCache.end())
        {
            auto sz = sizeCache[key];
            outW = sz.first; outH = sz.second;
            return it->second;
        }

        // Typical Quake2 mapping: "textures/<name>.(wal|tga|png|jpg)"
        TextureHandle tex = INVALID_TEXTURE;
        int w = 128, h = 128;

        {
            const char* exts[] = { ".wal", ".tga", ".png", ".jpg", ".jpeg" };
            for (const char* ext : exts)
            {
                const std::string p = std::string("textures/") + key + ext;
                printf("BSPLoader: trying texture '%s' (m_assets=%p)\n", p.c_str(), (void*)m_assets);
                if ((std::strcmp(ext, ".wal") == 0 && tryLoadWal(p, tex, w, h)) ||
                    (std::strcmp(ext, ".wal") != 0 && tryLoadImage(p, tex, w, h)))
                {
                    printf("BSPLoader: loaded '%s'\n", p.c_str());
                    break;
                }
            }
        }

        if (tex == INVALID_TEXTURE)
        {
            auto c = hashColor(texName);
            tex = makeSolidTexture(c[0], c[1], c[2]);
            w = h = 128;
            fprintf(stdout, "BSPLoader: solid color fallback for '%s': (%d,%d,%d)\n", 
                texName, c[0], c[1], c[2]);
        }

        texCache[key] = tex;
        sizeCache[key] = {w, h};
        outW = w; outH = h;
        return tex;
    };

    for (auto& ti : m_texInfos)
    {
        int w = 128, h = 128;
        ti.diffuse = getTextureForName(ti.textureName, w, h);
        ti.sampler = defaultSampler;
        ti.texWidth = w;
        ti.texHeight = h;
    }

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

    // We also build draw batches so we can bind diffuse textures per group.
    std::vector<RenderChunk::DrawBatch> batches;

    auto flushChunkWithBatches = [&]()
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
        rc.batches = batches;

        // Compute chunk AABB for frustum culling.
        Vec3 bmin{ FLT_MAX, FLT_MAX, FLT_MAX };
        Vec3 bmax{ -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (const auto& v : chunkVerts)
        {
            bmin.x = std::min(bmin.x, v.pos[0]); bmin.y = std::min(bmin.y, v.pos[1]); bmin.z = std::min(bmin.z, v.pos[2]);
            bmax.x = std::max(bmax.x, v.pos[0]); bmax.y = std::max(bmax.y, v.pos[1]); bmax.z = std::max(bmax.z, v.pos[2]);
        }
        rc.boundsMin = bmin;
        rc.boundsMax = bmax;
        m_chunks.push_back(rc);

        chunkVerts.clear();
        chunkIndices.clear();
        batches.clear();
    };

    for (const Surface &surf : m_surfaces)
    {
        if (surf.indexCount <= 0) continue;

        const BSPFace& face = (surf.faceIndex >= 0 && surf.faceIndex < (int)m_faces.size())
            ? m_faces[surf.faceIndex]
            : m_faces[0];
        const BSPTexInfo* ti = (face.texinfo >= 0 && face.texinfo < (int)m_texInfos.size())
            ? &m_texInfos[face.texinfo]
            : nullptr;
        const TextureHandle tex = ti ? ti->diffuse : INVALID_TEXTURE;
        const SamplerHandle samp = ti ? ti->sampler : INVALID_SAMPLER;

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
            flushChunkWithBatches();
        }

        // Remap: subtract global base, add local (chunk) base.
        uint32_t localBase = (uint32_t)chunkVerts.size();

        for (uint32_t v = 0; v < vertCount; ++v)
            chunkVerts.push_back(m_verticesPacked[globalBase + v]);

        const int batchStart = (int)chunkIndices.size();
        for (int i = 0; i < surf.indexCount; ++i)
        {
            uint32_t remapped = m_indicesRaw[surf.indexOffset + i] - globalBase + localBase;
            chunkIndices.push_back(remapped);
        }

        // Merge into batches (contiguous indices with same texture).
        if (!batches.empty() && batches.back().tex == tex && batches.back().samp == samp
            && batches.back().clusterIndex == surf.clusterIndex)
        {
            batches.back().indexCount += surf.indexCount;
        }
        else
        {
            RenderChunk::DrawBatch b{};
            b.tex          = tex;
            b.samp         = samp;
            b.firstIndex   = batchStart;
            b.indexCount   = surf.indexCount;
            b.clusterIndex = surf.clusterIndex;
            batches.push_back(b);
        }
    }

    flushChunkWithBatches();

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
    // Best-effort cleanup if engine didn't explicitly release.
    if (m_gpuBackend)
        releaseGPU(m_gpuBackend);
    freeLumps();
}

// ---------------------------------------------------------------------------
void BSPMap::releaseGPU(IRenderBackend *backend)
{
    if (!backend) return;

    // Destroy chunk buffers.
    for (auto& c : m_chunks)
    {
        if (c.vertexBuffer != INVALID_BUFFER) backend->destroyBuffer(c.vertexBuffer);
        if (c.indexBuffer != INVALID_BUFFER) backend->destroyBuffer(c.indexBuffer);
        c.vertexBuffer = INVALID_BUFFER;
        c.indexBuffer = INVALID_BUFFER;
        c.batches.clear();
    }
    m_chunks.clear();

    // Destroy lightmap atlas resources.
    if (m_lmAtlasHandle != INVALID_TEXTURE) backend->destroyTexture(m_lmAtlasHandle);
    if (m_lmAtlasSampler != INVALID_SAMPLER) backend->destroySampler(m_lmAtlasSampler);
    m_lmAtlasHandle = INVALID_TEXTURE;
    m_lmAtlasSampler = INVALID_SAMPLER;

    // Destroy white fallback texture.
    if (m_whiteFallback != INVALID_TEXTURE) backend->destroyTexture(m_whiteFallback);
    if (m_whiteFallbackSampler != INVALID_SAMPLER) backend->destroySampler(m_whiteFallbackSampler);
    m_whiteFallback = INVALID_TEXTURE;
    m_whiteFallbackSampler = INVALID_SAMPLER;

    // Destroy unique diffuse textures/samplers.
    std::unordered_set<uint64_t> texSeen;
    std::unordered_set<uint64_t> sampSeen;
    for (auto& ti : m_texInfos)
    {
        if (ti.diffuse != INVALID_TEXTURE && texSeen.insert((uint64_t)ti.diffuse).second)
            backend->destroyTexture(ti.diffuse);
        if (ti.sampler != INVALID_SAMPLER && sampSeen.insert((uint64_t)ti.sampler).second)
            backend->destroySampler(ti.sampler);
        ti.diffuse = INVALID_TEXTURE;
        ti.sampler = INVALID_SAMPLER;
    }

    m_gpuBackend = nullptr;
}

// ---------------------------------------------------------------------------
void BSPMap::render(IRenderBackend *backend, const Vec3& cameraPos)
{
    if (m_chunks.empty())
        return;

    // Debug removed

    // ---- Step 1: Determine camera cluster and decompress PVS ----
    const int camLeaf    = findLeaf(cameraPos);
    const int camCluster = (camLeaf >= 0 && camLeaf < (int)m_leaves.size())
                           ? (int)m_leaves[camLeaf].cluster : -1;

    // Use cached PVS if the cluster hasn't changed since last frame.
    bool hasPVS = false;
    if (camCluster >= 0 && !m_visData.empty())
    {
        if (camCluster != m_lastPVSCluster)
        {
            decompressPVS(camCluster, m_cachedPVS);
            m_lastPVSCluster = camCluster;
        }
        hasPVS = !m_cachedPVS.empty();
    }

    // ---- Step 2: Bind the lightmap atlas (slot 0 = uLightmap) ----
    if (m_lmAtlasHandle != INVALID_TEXTURE)
        backend->bindTexture(m_lmAtlasHandle, m_lmAtlasSampler, 0);

    const bool doCull = m_hasViewProj;
    FrustumPlanes fr{};
    if (doCull)
        fr = extractFrustum(m_viewProj);

    // ---- Step 3: Draw visible chunks ----
    for (const auto &chunk : m_chunks)
    {
        if (chunk.vertexBuffer == INVALID_BUFFER || chunk.indexBuffer == INVALID_BUFFER)
            continue;

        // Frustum-AABB cull the whole chunk first (fast rejection).
        if (doCull && !aabbInFrustum(fr, chunk.boundsMin, chunk.boundsMax))
            continue;

        backend->bindVertexBuffer(chunk.vertexBuffer, 0, &kLayoutBSP);
        backend->bindIndexBuffer(chunk.indexBuffer);

        for (const auto& b : chunk.batches)
        {
            // PVS cluster gate:
            //   clusterIndex == -1  -> conservative: always draw (no leaf claimed it)
            //   hasPVS == false     -> camera in solid/void: draw everything
            //   otherwise: check the bit for b.clusterIndex in the decompressed row
            if (hasPVS && b.clusterIndex >= 0)
            {
                if (b.clusterIndex >= m_numClusters) continue;
                if (!(m_cachedPVS[b.clusterIndex >> 3] & (1 << (b.clusterIndex & 7))))
                    continue;
            }

            if (b.tex != INVALID_TEXTURE)
            {
                backend->bindTexture(b.tex, b.samp, 1);
            }
            else
            {
                backend->bindTexture(m_whiteFallback, m_whiteFallbackSampler, 1);
            }
            backend->drawIndexed(b.indexCount, b.firstIndex);
        }
    }
}

} // namespace nova
