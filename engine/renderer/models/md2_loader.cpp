// ============================================================
// FILE:    engine/renderer/models/md2_loader.cpp
// MODULE:  Renderer > Models
// PHASE:   2
// PURPOSE: MD2 model loading, animation, and rendering.
//          Parses Quake 2 .md2 files (IDP2 v8), loads PCX/image skins,
//          interpolates frames on CPU, uploads to GPU via IRenderBackend.
// DEPENDS: models/md2.h, core/image_load.h, core/asset_fs.h
// ============================================================

#include "engine/renderer/models/md2.h"
#include "engine/renderer/bsp/bsp.h"
#include "engine/core/image_load.h"
#include "engine/core/log.h"
#include <glad/glad.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <memory>

namespace nova
{

// =====================================================================
//  Standard MD2 normal table (162 entries from Quake 2 anorms.h)
//  These are pre-computed unit vectors for compressed vertex normals.
//  Converted from Q2 Z-up to GL Y-up: (x, y, z) -> (x, z, -y)
// =====================================================================
static const float s_anormsData[MD2_NUM_ANORMS][3] = {
    { -0.525731f,  0.000000f,  0.850651f }, { -0.442863f,  0.235114f,  0.865323f },
    { -0.295242f,  0.000000f,  0.955423f }, { -0.309017f,  0.500000f,  0.809017f },
    { -0.162460f,  0.262866f,  0.951056f }, {  0.000000f,  0.000000f,  1.000000f },
    {  0.000000f,  0.850651f,  0.525731f }, { -0.147621f,  0.716567f,  0.681718f },
    {  0.147621f,  0.716567f,  0.681718f }, {  0.000000f,  0.525731f,  0.850651f },
    {  0.309017f,  0.500000f,  0.809017f }, {  0.525731f,  0.000000f,  0.850651f },
    {  0.295242f,  0.000000f,  0.955423f }, {  0.442863f,  0.235114f,  0.865323f },
    {  0.162460f,  0.262866f,  0.951056f }, { -0.681718f,  0.147621f,  0.716567f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.587785f,  0.425325f,  0.688191f },
    { -0.850651f,  0.525731f,  0.000000f }, { -0.865323f,  0.442863f,  0.235114f },
    { -0.688191f,  0.587785f,  0.425325f }, { -0.587785f,  0.951057f,  0.000000f },
    { -0.716567f,  0.681718f,  0.147621f }, { -0.500000f,  0.809017f,  0.309017f },
    { -0.235114f,  0.865323f,  0.442863f }, { -0.425325f,  0.688191f,  0.587785f },
    { -0.716567f,  0.147621f,  0.681718f }, { -0.500000f,  0.309017f,  0.809017f },
    { -0.235114f,  0.442863f,  0.865323f }, { -0.865323f,  0.235114f,  0.442863f },
    { -0.688191f,  0.425325f,  0.587785f }, { -0.809017f,  0.309017f,  0.500000f },
    { -0.681718f,  0.147621f,  0.716567f }, { -0.681718f,  0.147621f,  0.716567f },
    { -0.850651f,  0.525731f,  0.000000f }, { -0.865323f,  0.442863f,  0.235114f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.688191f,  0.587785f,  0.425325f },
    { -0.587785f,  0.951057f,  0.000000f }, { -0.716567f,  0.681718f,  0.147621f },
    { -0.500000f,  0.809017f,  0.309017f }, { -0.235114f,  0.865323f,  0.442863f },
    { -0.425325f,  0.688191f,  0.587785f }, { -0.716567f,  0.147621f,  0.681718f },
    { -0.500000f,  0.309017f,  0.809017f }, { -0.235114f,  0.442863f,  0.865323f },
    { -0.865323f,  0.235114f,  0.442863f }, { -0.688191f,  0.425325f,  0.587785f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.681718f,  0.147621f,  0.716567f },
    { -0.850651f,  0.525731f,  0.000000f }, { -0.865323f,  0.442863f,  0.235114f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.688191f,  0.587785f,  0.425325f },
    { -0.587785f,  0.951057f,  0.000000f }, { -0.716567f,  0.681718f,  0.147621f },
    { -0.500000f,  0.809017f,  0.309017f }, { -0.235114f,  0.865323f,  0.442863f },
    { -0.425325f,  0.688191f,  0.587785f }, { -0.716567f,  0.147621f,  0.681718f },
    { -0.500000f,  0.309017f,  0.809017f }, { -0.235114f,  0.442863f,  0.865323f },
    { -0.865323f,  0.235114f,  0.442863f }, { -0.688191f,  0.425325f,  0.587785f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.681718f,  0.147621f,  0.716567f },
    { -0.850651f,  0.525731f,  0.000000f }, { -0.865323f,  0.442863f,  0.235114f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.688191f,  0.587785f,  0.425325f },
    { -0.587785f,  0.951057f,  0.000000f }, { -0.716567f,  0.681718f,  0.147621f },
    { -0.500000f,  0.809017f,  0.309017f }, { -0.235114f,  0.865323f,  0.442863f },
    { -0.425325f,  0.688191f,  0.587785f }, { -0.716567f,  0.147621f,  0.681718f },
    { -0.500000f,  0.309017f,  0.809017f }, { -0.235114f,  0.442863f,  0.865323f },
    { -0.865323f,  0.235114f,  0.442863f }, { -0.688191f,  0.425325f,  0.587785f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.681718f,  0.147621f,  0.716567f },
    { -0.850651f,  0.525731f,  0.000000f }, { -0.865323f,  0.442863f,  0.235114f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.688191f,  0.587785f,  0.425325f },
    { -0.587785f,  0.951057f,  0.000000f }, { -0.716567f,  0.681718f,  0.147621f },
    { -0.500000f,  0.809017f,  0.309017f }, { -0.235114f,  0.865323f,  0.442863f },
    { -0.425325f,  0.688191f,  0.587785f }, { -0.716567f,  0.147621f,  0.681718f },
    { -0.500000f,  0.309017f,  0.809017f }, { -0.235114f,  0.442863f,  0.865323f },
    { -0.865323f,  0.235114f,  0.442863f }, { -0.688191f,  0.425325f,  0.587785f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.681718f,  0.147621f,  0.716567f },
    { -0.850651f,  0.525731f,  0.000000f }, { -0.865323f,  0.442863f,  0.235114f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.688191f,  0.587785f,  0.425325f },
    { -0.587785f,  0.951057f,  0.000000f }, { -0.716567f,  0.681718f,  0.147621f },
    { -0.500000f,  0.809017f,  0.309017f }, { -0.235114f,  0.865323f,  0.442863f },
    { -0.425325f,  0.688191f,  0.587785f }, { -0.716567f,  0.147621f,  0.681718f },
    { -0.500000f,  0.309017f,  0.809017f }, { -0.235114f,  0.442863f,  0.865323f },
    { -0.865323f,  0.235114f,  0.442863f }, { -0.688191f,  0.425325f,  0.587785f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.681718f,  0.147621f,  0.716567f },
    { -0.850651f,  0.525731f,  0.000000f }, { -0.865323f,  0.442863f,  0.235114f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.688191f,  0.587785f,  0.425325f },
    { -0.587785f,  0.951057f,  0.000000f }, { -0.716567f,  0.681718f,  0.147621f },
    { -0.500000f,  0.809017f,  0.309017f }, { -0.235114f,  0.865323f,  0.442863f },
    { -0.425325f,  0.688191f,  0.587785f }, { -0.716567f,  0.147621f,  0.681718f },
    { -0.500000f,  0.309017f,  0.809017f }, { -0.235114f,  0.442863f,  0.865323f },
    { -0.865323f,  0.235114f,  0.442863f }, { -0.688191f,  0.425325f,  0.587785f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.681718f,  0.147621f,  0.716567f },
    { -0.850651f,  0.525731f,  0.000000f }, { -0.865323f,  0.442863f,  0.235114f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.688191f,  0.587785f,  0.425325f },
    { -0.587785f,  0.951057f,  0.000000f }, { -0.716567f,  0.681718f,  0.147621f },
    { -0.500000f,  0.809017f,  0.309017f }, { -0.235114f,  0.865323f,  0.442863f },
    { -0.425325f,  0.688191f,  0.587785f }, { -0.716567f,  0.147621f,  0.681718f },
    { -0.500000f,  0.309017f,  0.809017f }, { -0.235114f,  0.442863f,  0.865323f },
    { -0.865323f,  0.235114f,  0.442863f }, { -0.688191f,  0.425325f,  0.587785f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.681718f,  0.147621f,  0.716567f },
    { -0.850651f,  0.525731f,  0.000000f }, { -0.865323f,  0.442863f,  0.235114f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.688191f,  0.587785f,  0.425325f },
    { -0.587785f,  0.951057f,  0.000000f }, { -0.716567f,  0.681718f,  0.147621f },
    { -0.500000f,  0.809017f,  0.309017f }, { -0.235114f,  0.865323f,  0.442863f },
    { -0.425325f,  0.688191f,  0.587785f }, { -0.716567f,  0.147621f,  0.681718f },
    { -0.500000f,  0.309017f,  0.809017f }, { -0.235114f,  0.442863f,  0.865323f },
    { -0.865323f,  0.235114f,  0.442863f }, { -0.688191f,  0.425325f,  0.587785f },
    { -0.809017f,  0.309017f,  0.500000f }, { -0.681718f,  0.147621f,  0.716567f },
};

// Q2-to-GL coordinate conversion for normals: (x, y, z) -> (x, z, -y)
const Vec3 g_md2Normals[MD2_NUM_ANORMS] = {
    Vec3{s_anormsData[0][0],  s_anormsData[0][2],  -s_anormsData[0][1]},
    Vec3{s_anormsData[1][0],  s_anormsData[1][2],  -s_anormsData[1][1]},
    Vec3{s_anormsData[2][0],  s_anormsData[2][2],  -s_anormsData[2][1]},
    Vec3{s_anormsData[3][0],  s_anormsData[3][2],  -s_anormsData[3][1]},
    Vec3{s_anormsData[4][0],  s_anormsData[4][2],  -s_anormsData[4][1]},
    Vec3{s_anormsData[5][0],  s_anormsData[5][2],  -s_anormsData[5][1]},
    Vec3{s_anormsData[6][0],  s_anormsData[6][2],  -s_anormsData[6][1]},
    Vec3{s_anormsData[7][0],  s_anormsData[7][2],  -s_anormsData[7][1]},
    Vec3{s_anormsData[8][0],  s_anormsData[8][2],  -s_anormsData[8][1]},
    Vec3{s_anormsData[9][0],  s_anormsData[9][2],  -s_anormsData[9][1]},
    Vec3{s_anormsData[10][0], s_anormsData[10][2], -s_anormsData[10][1]},
    Vec3{s_anormsData[11][0], s_anormsData[11][2], -s_anormsData[11][1]},
    Vec3{s_anormsData[12][0], s_anormsData[12][2], -s_anormsData[12][1]},
    Vec3{s_anormsData[13][0], s_anormsData[13][2], -s_anormsData[13][1]},
    Vec3{s_anormsData[14][0], s_anormsData[14][2], -s_anormsData[14][1]},
    Vec3{s_anormsData[15][0], s_anormsData[15][2], -s_anormsData[15][1]},
    Vec3{s_anormsData[16][0], s_anormsData[16][2], -s_anormsData[16][1]},
    Vec3{s_anormsData[17][0], s_anormsData[17][2], -s_anormsData[17][1]},
    Vec3{s_anormsData[18][0], s_anormsData[18][2], -s_anormsData[18][1]},
    Vec3{s_anormsData[19][0], s_anormsData[19][2], -s_anormsData[19][1]},
    Vec3{s_anormsData[20][0], s_anormsData[20][2], -s_anormsData[20][1]},
    Vec3{s_anormsData[21][0], s_anormsData[21][2], -s_anormsData[21][1]},
    Vec3{s_anormsData[22][0], s_anormsData[22][2], -s_anormsData[22][1]},
    Vec3{s_anormsData[23][0], s_anormsData[23][2], -s_anormsData[23][1]},
    Vec3{s_anormsData[24][0], s_anormsData[24][2], -s_anormsData[24][1]},
    Vec3{s_anormsData[25][0], s_anormsData[25][2], -s_anormsData[25][1]},
    Vec3{s_anormsData[26][0], s_anormsData[26][2], -s_anormsData[26][1]},
    Vec3{s_anormsData[27][0], s_anormsData[27][2], -s_anormsData[27][1]},
    Vec3{s_anormsData[28][0], s_anormsData[28][2], -s_anormsData[28][1]},
    Vec3{s_anormsData[29][0], s_anormsData[29][2], -s_anormsData[29][1]},
    Vec3{s_anormsData[30][0], s_anormsData[30][2], -s_anormsData[30][1]},
    Vec3{s_anormsData[31][0], s_anormsData[31][2], -s_anormsData[31][1]},
    Vec3{s_anormsData[32][0], s_anormsData[32][2], -s_anormsData[32][1]},
    Vec3{s_anormsData[33][0], s_anormsData[33][2], -s_anormsData[33][1]},
    Vec3{s_anormsData[34][0], s_anormsData[34][2], -s_anormsData[34][1]},
    Vec3{s_anormsData[35][0], s_anormsData[35][2], -s_anormsData[35][1]},
    Vec3{s_anormsData[36][0], s_anormsData[36][2], -s_anormsData[36][1]},
    Vec3{s_anormsData[37][0], s_anormsData[37][2], -s_anormsData[37][1]},
    Vec3{s_anormsData[38][0], s_anormsData[38][2], -s_anormsData[38][1]},
    Vec3{s_anormsData[39][0], s_anormsData[39][2], -s_anormsData[39][1]},
    Vec3{s_anormsData[40][0], s_anormsData[40][2], -s_anormsData[40][1]},
    Vec3{s_anormsData[41][0], s_anormsData[41][2], -s_anormsData[41][1]},
    Vec3{s_anormsData[42][0], s_anormsData[42][2], -s_anormsData[42][1]},
    Vec3{s_anormsData[43][0], s_anormsData[43][2], -s_anormsData[43][1]},
    Vec3{s_anormsData[44][0], s_anormsData[44][2], -s_anormsData[44][1]},
    Vec3{s_anormsData[45][0], s_anormsData[45][2], -s_anormsData[45][1]},
    Vec3{s_anormsData[46][0], s_anormsData[46][2], -s_anormsData[46][1]},
    Vec3{s_anormsData[47][0], s_anormsData[47][2], -s_anormsData[47][1]},
    Vec3{s_anormsData[48][0], s_anormsData[48][2], -s_anormsData[48][1]},
    Vec3{s_anormsData[49][0], s_anormsData[49][2], -s_anormsData[49][1]},
    Vec3{s_anormsData[50][0], s_anormsData[50][2], -s_anormsData[50][1]},
    Vec3{s_anormsData[51][0], s_anormsData[51][2], -s_anormsData[51][1]},
    Vec3{s_anormsData[52][0], s_anormsData[52][2], -s_anormsData[52][1]},
    Vec3{s_anormsData[53][0], s_anormsData[53][2], -s_anormsData[53][1]},
    Vec3{s_anormsData[54][0], s_anormsData[54][2], -s_anormsData[54][1]},
    Vec3{s_anormsData[55][0], s_anormsData[55][2], -s_anormsData[55][1]},
    Vec3{s_anormsData[56][0], s_anormsData[56][2], -s_anormsData[56][1]},
    Vec3{s_anormsData[57][0], s_anormsData[57][2], -s_anormsData[57][1]},
    Vec3{s_anormsData[58][0], s_anormsData[58][2], -s_anormsData[58][1]},
    Vec3{s_anormsData[59][0], s_anormsData[59][2], -s_anormsData[59][1]},
    Vec3{s_anormsData[60][0], s_anormsData[60][2], -s_anormsData[60][1]},
    Vec3{s_anormsData[61][0], s_anormsData[61][2], -s_anormsData[61][1]},
    Vec3{s_anormsData[62][0], s_anormsData[62][2], -s_anormsData[62][1]},
    Vec3{s_anormsData[63][0], s_anormsData[63][2], -s_anormsData[63][1]},
    Vec3{s_anormsData[64][0], s_anormsData[64][2], -s_anormsData[64][1]},
    Vec3{s_anormsData[65][0], s_anormsData[65][2], -s_anormsData[65][1]},
    Vec3{s_anormsData[66][0], s_anormsData[66][2], -s_anormsData[66][1]},
    Vec3{s_anormsData[67][0], s_anormsData[67][2], -s_anormsData[67][1]},
    Vec3{s_anormsData[68][0], s_anormsData[68][2], -s_anormsData[68][1]},
    Vec3{s_anormsData[69][0], s_anormsData[69][2], -s_anormsData[69][1]},
    Vec3{s_anormsData[70][0], s_anormsData[70][2], -s_anormsData[70][1]},
    Vec3{s_anormsData[71][0], s_anormsData[71][2], -s_anormsData[71][1]},
    Vec3{s_anormsData[72][0], s_anormsData[72][2], -s_anormsData[72][1]},
    Vec3{s_anormsData[73][0], s_anormsData[73][2], -s_anormsData[73][1]},
    Vec3{s_anormsData[74][0], s_anormsData[74][2], -s_anormsData[74][1]},
    Vec3{s_anormsData[75][0], s_anormsData[75][2], -s_anormsData[75][1]},
    Vec3{s_anormsData[76][0], s_anormsData[76][2], -s_anormsData[76][1]},
    Vec3{s_anormsData[77][0], s_anormsData[77][2], -s_anormsData[77][1]},
    Vec3{s_anormsData[78][0], s_anormsData[78][2], -s_anormsData[78][1]},
    Vec3{s_anormsData[79][0], s_anormsData[79][2], -s_anormsData[79][1]},
    Vec3{s_anormsData[80][0], s_anormsData[80][2], -s_anormsData[80][1]},
    Vec3{s_anormsData[81][0], s_anormsData[81][2], -s_anormsData[81][1]},
    Vec3{s_anormsData[82][0], s_anormsData[82][2], -s_anormsData[82][1]},
    Vec3{s_anormsData[83][0], s_anormsData[83][2], -s_anormsData[83][1]},
    Vec3{s_anormsData[84][0], s_anormsData[84][2], -s_anormsData[84][1]},
    Vec3{s_anormsData[85][0], s_anormsData[85][2], -s_anormsData[85][1]},
    Vec3{s_anormsData[86][0], s_anormsData[86][2], -s_anormsData[86][1]},
    Vec3{s_anormsData[87][0], s_anormsData[87][2], -s_anormsData[87][1]},
    Vec3{s_anormsData[88][0], s_anormsData[88][2], -s_anormsData[88][1]},
    Vec3{s_anormsData[89][0], s_anormsData[89][2], -s_anormsData[89][1]},
    Vec3{s_anormsData[90][0], s_anormsData[90][2], -s_anormsData[90][1]},
    Vec3{s_anormsData[91][0], s_anormsData[91][2], -s_anormsData[91][1]},
    Vec3{s_anormsData[92][0], s_anormsData[92][2], -s_anormsData[92][1]},
    Vec3{s_anormsData[93][0], s_anormsData[93][2], -s_anormsData[93][1]},
    Vec3{s_anormsData[94][0], s_anormsData[94][2], -s_anormsData[94][1]},
    Vec3{s_anormsData[95][0], s_anormsData[95][2], -s_anormsData[95][1]},
    Vec3{s_anormsData[96][0], s_anormsData[96][2], -s_anormsData[96][1]},
    Vec3{s_anormsData[97][0], s_anormsData[97][2], -s_anormsData[97][1]},
    Vec3{s_anormsData[98][0], s_anormsData[98][2], -s_anormsData[98][1]},
    Vec3{s_anormsData[99][0], s_anormsData[99][2], -s_anormsData[99][1]},
    Vec3{s_anormsData[100][0],s_anormsData[100][2],-s_anormsData[100][1]},
    Vec3{s_anormsData[101][0],s_anormsData[101][2],-s_anormsData[101][1]},
    Vec3{s_anormsData[102][0],s_anormsData[102][2],-s_anormsData[102][1]},
    Vec3{s_anormsData[103][0],s_anormsData[103][2],-s_anormsData[103][1]},
    Vec3{s_anormsData[104][0],s_anormsData[104][2],-s_anormsData[104][1]},
    Vec3{s_anormsData[105][0],s_anormsData[105][2],-s_anormsData[105][1]},
    Vec3{s_anormsData[106][0],s_anormsData[106][2],-s_anormsData[106][1]},
    Vec3{s_anormsData[107][0],s_anormsData[107][2],-s_anormsData[107][1]},
    Vec3{s_anormsData[108][0],s_anormsData[108][2],-s_anormsData[108][1]},
    Vec3{s_anormsData[109][0],s_anormsData[109][2],-s_anormsData[109][1]},
    Vec3{s_anormsData[110][0],s_anormsData[110][2],-s_anormsData[110][1]},
    Vec3{s_anormsData[111][0],s_anormsData[111][2],-s_anormsData[111][1]},
    Vec3{s_anormsData[112][0],s_anormsData[112][2],-s_anormsData[112][1]},
    Vec3{s_anormsData[113][0],s_anormsData[113][2],-s_anormsData[113][1]},
    Vec3{s_anormsData[114][0],s_anormsData[114][2],-s_anormsData[114][1]},
    Vec3{s_anormsData[115][0],s_anormsData[115][2],-s_anormsData[115][1]},
    Vec3{s_anormsData[116][0],s_anormsData[116][2],-s_anormsData[116][1]},
    Vec3{s_anormsData[117][0],s_anormsData[117][2],-s_anormsData[117][1]},
    Vec3{s_anormsData[118][0],s_anormsData[118][2],-s_anormsData[118][1]},
    Vec3{s_anormsData[119][0],s_anormsData[119][2],-s_anormsData[119][1]},
    Vec3{s_anormsData[120][0],s_anormsData[120][2],-s_anormsData[120][1]},
    Vec3{s_anormsData[121][0],s_anormsData[121][2],-s_anormsData[121][1]},
    Vec3{s_anormsData[122][0],s_anormsData[122][2],-s_anormsData[122][1]},
    Vec3{s_anormsData[123][0],s_anormsData[123][2],-s_anormsData[123][1]},
    Vec3{s_anormsData[124][0],s_anormsData[124][2],-s_anormsData[124][1]},
    Vec3{s_anormsData[125][0],s_anormsData[125][2],-s_anormsData[125][1]},
    Vec3{s_anormsData[126][0],s_anormsData[126][2],-s_anormsData[126][1]},
    Vec3{s_anormsData[127][0],s_anormsData[127][2],-s_anormsData[127][1]},
    Vec3{s_anormsData[128][0],s_anormsData[128][2],-s_anormsData[128][1]},
    Vec3{s_anormsData[129][0],s_anormsData[129][2],-s_anormsData[129][1]},
    Vec3{s_anormsData[130][0],s_anormsData[130][2],-s_anormsData[130][1]},
    Vec3{s_anormsData[131][0],s_anormsData[131][2],-s_anormsData[131][1]},
    Vec3{s_anormsData[132][0],s_anormsData[132][2],-s_anormsData[132][1]},
    Vec3{s_anormsData[133][0],s_anormsData[133][2],-s_anormsData[133][1]},
    Vec3{s_anormsData[134][0],s_anormsData[134][2],-s_anormsData[134][1]},
    Vec3{s_anormsData[135][0],s_anormsData[135][2],-s_anormsData[135][1]},
    Vec3{s_anormsData[136][0],s_anormsData[136][2],-s_anormsData[136][1]},
    Vec3{s_anormsData[137][0],s_anormsData[137][2],-s_anormsData[137][1]},
    Vec3{s_anormsData[138][0],s_anormsData[138][2],-s_anormsData[138][1]},
    Vec3{s_anormsData[139][0],s_anormsData[139][2],-s_anormsData[139][1]},
    Vec3{s_anormsData[140][0],s_anormsData[140][2],-s_anormsData[140][1]},
    Vec3{s_anormsData[141][0],s_anormsData[141][2],-s_anormsData[141][1]},
    Vec3{s_anormsData[142][0],s_anormsData[142][2],-s_anormsData[142][1]},
    Vec3{s_anormsData[143][0],s_anormsData[143][2],-s_anormsData[143][1]},
    Vec3{s_anormsData[144][0],s_anormsData[144][2],-s_anormsData[144][1]},
    Vec3{s_anormsData[145][0],s_anormsData[145][2],-s_anormsData[145][1]},
    Vec3{s_anormsData[146][0],s_anormsData[146][2],-s_anormsData[146][1]},
    Vec3{s_anormsData[147][0],s_anormsData[147][2],-s_anormsData[147][1]},
    Vec3{s_anormsData[148][0],s_anormsData[148][2],-s_anormsData[148][1]},
    Vec3{s_anormsData[149][0],s_anormsData[149][2],-s_anormsData[149][1]},
    Vec3{s_anormsData[150][0],s_anormsData[150][2],-s_anormsData[150][1]},
    Vec3{s_anormsData[151][0],s_anormsData[151][2],-s_anormsData[151][1]},
    Vec3{s_anormsData[152][0],s_anormsData[152][2],-s_anormsData[152][1]},
    Vec3{s_anormsData[153][0],s_anormsData[153][2],-s_anormsData[153][1]},
    Vec3{s_anormsData[154][0],s_anormsData[154][2],-s_anormsData[154][1]},
    Vec3{s_anormsData[155][0],s_anormsData[155][2],-s_anormsData[155][1]},
    Vec3{s_anormsData[156][0],s_anormsData[156][2],-s_anormsData[156][1]},
    Vec3{s_anormsData[157][0],s_anormsData[157][2],-s_anormsData[157][1]},
    Vec3{s_anormsData[158][0],s_anormsData[158][2],-s_anormsData[158][1]},
    Vec3{s_anormsData[159][0],s_anormsData[159][2],-s_anormsData[159][1]},
    Vec3{s_anormsData[160][0],s_anormsData[160][2],-s_anormsData[160][1]},
    Vec3{s_anormsData[161][0],s_anormsData[161][2],-s_anormsData[161][1]},
};

// =====================================================================
//  MD2Mesh Implementation
// =====================================================================

static Vec3 q2ToGL(float x, float y, float z) { return {x, z, -y}; }

bool MD2Mesh::load(IRenderBackend* backend, AssetFS* assets, const char* md2Path)
{
    if (!backend || !md2Path) return false;

    std::vector<uint8_t> fileData;
    if (assets && !assets->readAllBytes(md2Path, fileData))
    {
        Logger::instance().error("MD2: cannot read '%s'", md2Path);
        return false;
    }
    if (!assets)
    {
        Logger::instance().error("MD2: AssetFS not provided");
        return false;
    }

    if (fileData.size() < sizeof(MD2Header))
    {
        Logger::instance().error("MD2: file too small '%s'", md2Path);
        return false;
    }

    const MD2Header* hdr = reinterpret_cast<const MD2Header*>(fileData.data());
    if (hdr->magic != (int32_t)MD2_MAGIC)
    {
        Logger::instance().error("MD2: bad magic 0x%08X in '%s'", hdr->magic, md2Path);
        return false;
    }
    if (hdr->version != MD2_VERSION)
    {
        Logger::instance().error("MD2: bad version %d (expected %d) in '%s'", hdr->version, MD2_VERSION, md2Path);
        return false;
    }

    m_numVerts  = hdr->numVertices;
    m_numTris   = hdr->numTriangles;
    m_numFrames = hdr->numFrames;

    if (m_numVerts <= 0 || m_numTris <= 0 || m_numFrames <= 0)
    {
        Logger::instance().error("MD2: invalid counts v=%d t=%d f=%d", m_numVerts, m_numTris, m_numFrames);
        return false;
    }

    // Scan triangles for actual max vertex/ST indices.
    // Some exported MD2 files (Blender, etc.) report incorrect numVertices.
    const uint8_t* base = fileData.data();
    const MD2Triangle* triangles = reinterpret_cast<const MD2Triangle*>(base + hdr->offsetTriangles);
    const MD2TexCoord* stCoords = reinterpret_cast<const MD2TexCoord*>(base + hdr->offsetST);
    const char* skinNames = reinterpret_cast<const char*>(base + hdr->offsetSkins);
    const uint8_t* frameBase = base + hdr->offsetFrames;

    int maxVertIdx = 0;
    int maxSTIdx = 0;
    for (int i = 0; i < m_numTris; ++i)
    {
        for (int v = 0; v < 3; ++v)
        {
            if (triangles[i].vertexIndex[v] > maxVertIdx) maxVertIdx = triangles[i].vertexIndex[v];
            if (triangles[i].stIndex[v] > maxSTIdx) maxSTIdx = triangles[i].stIndex[v];
        }
    }

    Logger::instance().info("MD2: triangle scan complete — maxVertIdx=%d, maxSTIdx=%d", maxVertIdx, maxSTIdx);

    // Use original numVerts for frame data (the actual per-frame vertex count),
    // but expand the buffer/UV arrays if indices go beyond it.
    m_frameVertCount = m_numVerts; // from header, used for frame parsing
    const int frameVertexCount = m_frameVertCount;
    if (maxVertIdx >= m_numVerts)
    {
        Logger::instance().warn("MD2: vertex index out of range (%d >= %d), expanding",
                                maxVertIdx, m_numVerts);
        m_numVerts = maxVertIdx + 1;
    }

    Logger::instance().info("MD2: parsing '%s' verts=%d (frameVerts=%d) tris=%d frames=%d st=%d skins=%d",
                            md2Path, m_numVerts, frameVertexCount, m_numTris, m_numFrames,
                            hdr->numST, hdr->numSkins);

    // MD2 UVs are per-triangle-vertex: each triangle corner has its own (vertexIndex, stIndex).
    // A single mesh vertex can be shared by multiple triangles with DIFFERENT UVs.
    // We must split vertices so each unique (vertexIdx, stIdx) pair becomes a unique GPU vertex.
    struct SplitVertKey
    {
        int vertIdx;
        int stIdx;
        bool operator==(const SplitVertKey& o) const { return vertIdx == o.vertIdx && stIdx == o.stIdx; }
    };
    struct SplitVertKeyHash
    {
        size_t operator()(const SplitVertKey& k) const
        {
            return ((size_t)k.vertIdx * 73856093) ^ ((size_t)k.stIdx * 19349663);
        }
    };

    std::unordered_map<SplitVertKey, int, SplitVertKeyHash> vertMap;
    int uniqueCount = 0;

    // Pre-build the split vertex mapping
    for (int t = 0; t < m_numTris; ++t)
    {
        for (int v = 0; v < 3; ++v)
        {
            int vi = triangles[t].vertexIndex[v];
            int si = triangles[t].stIndex[v];
            SplitVertKey key = {vi, si};
            if (vertMap.find(key) == vertMap.end())
            {
                vertMap[key] = uniqueCount++;
            }
        }
    }

    m_numVerts = uniqueCount;
    m_uvs.resize(m_numVerts * 2);
    for (const auto& kv : vertMap)
    {
        int gpuIdx = kv.second;
        float u = (float)stCoords[kv.first.stIdx].s / (float)hdr->skinWidth;
        float vt = (float)stCoords[kv.first.stIdx].t / (float)hdr->skinHeight;
        m_uvs[gpuIdx * 2 + 0] = u;
        m_uvs[gpuIdx * 2 + 1] = vt;
    }

    // Build remapped index buffer (points into split vertex array)
    std::vector<uint32_t> indices(m_numTris * 3);
    for (int i = 0; i < m_numTris; ++i)
    {
        for (int v = 0; v < 3; ++v)
        {
            int vi = triangles[i].vertexIndex[v];
            int si = triangles[i].stIndex[v];
            SplitVertKey key = {vi, si};
            indices[i * 3 + v] = (uint32_t)vertMap[key];
        }
    }

    BufferDesc ibDesc{};
    ibDesc.type = BufferType::Index;
    ibDesc.usage = BufferUsage::Static;
    ibDesc.size = indices.size() * sizeof(uint32_t);
    ibDesc.initialData = indices.data();
    m_indexBuffer = backend->createBuffer(ibDesc);

    // Allocate vertex buffer (static allocation, updated each frame)
    BufferDesc vbDesc{};
    vbDesc.type = BufferType::Vertex;
    vbDesc.usage = BufferUsage::Dynamic;
    vbDesc.size = m_numVerts * sizeof(MD2VertexPacked);
    m_vertexBuffer = backend->createBuffer(vbDesc);

    // Build the inverse mapping: original vertex index → list of (splitIdx, stIdx)
    // This is needed to remap frame vertex data to the split vertex array.
    // We need to store this for frame interpolation.
    m_splitInfo.resize(frameVertexCount);
    for (const auto& kv : vertMap)
    {
        if (kv.first.vertIdx >= 0 && kv.first.vertIdx < frameVertexCount)
            m_splitInfo[kv.first.vertIdx].push_back({kv.second, kv.first.stIdx});
    }

    // Upload initial frame (frame 0)
    std::vector<MD2VertexPacked> initialVerts(m_numVerts);
    buildFrameDataSplit(frameBase, 0, initialVerts.data(), frameVertexCount);

    // Apply UVs to initial vertex buffer
    for (int i = 0; i < m_numVerts; ++i)
    {
        initialVerts[i].uv[0] = m_uvs[i * 2 + 0];
        initialVerts[i].uv[1] = m_uvs[i * 2 + 1];
    }

    backend->setBufferData(m_vertexBuffer, initialVerts.data(), m_numVerts * sizeof(MD2VertexPacked));

    // Store compressed frame data for CPU interpolation
    // Use frameVertexCount for frame data (actual per-frame vertex count from header).
    const int frameByteSize = hdr->frameSize;
    m_frameVerts.resize(m_numFrames * frameVertexCount * 4);

    m_frameData.resize(m_numFrames * 6);
    m_frameNames.resize(m_numFrames * MD2_MAX_FRAME_NAME);

    for (int f = 0; f < m_numFrames; ++f)
    {
        const MD2AliasFrame* aliasFrame = reinterpret_cast<const MD2AliasFrame*>(frameBase + f * frameByteSize);
        const MD2AliasVertex* aliasVerts = reinterpret_cast<const MD2AliasVertex*>(aliasFrame + 1);

        m_frameData[f * 6 + 0] = aliasFrame->scale[0];
        m_frameData[f * 6 + 1] = aliasFrame->scale[1];
        m_frameData[f * 6 + 2] = aliasFrame->scale[2];
        m_frameData[f * 6 + 3] = aliasFrame->translate[0];
        m_frameData[f * 6 + 4] = aliasFrame->translate[1];
        m_frameData[f * 6 + 5] = aliasFrame->translate[2];

        std::memcpy(m_frameNames.data() + f * MD2_MAX_FRAME_NAME, aliasFrame->name, MD2_MAX_FRAME_NAME);

        const size_t vertOffset = (size_t)f * frameVertexCount * 4;
        for (int v = 0; v < frameVertexCount; ++v)
        {
            m_frameVerts[vertOffset + v * 4 + 0] = aliasVerts[v].v[0];
            m_frameVerts[vertOffset + v * 4 + 1] = aliasVerts[v].v[1];
            m_frameVerts[vertOffset + v * 4 + 2] = aliasVerts[v].v[2];
            m_frameVerts[vertOffset + v * 4 + 3] = aliasVerts[v].normalIndex;
        }
    }

    // Load skin textures
    const int numSkins = hdr->numSkins;
    if (numSkins > 0)
    {
        m_skinTextures.resize(numSkins);
        m_skinSamplers.resize(numSkins);

        // Resolve model directory from md2Path for relative skin paths.
        // E.g., "models/player/tris.md2" -> base = "models/player/"
        std::string modelDir;
        {
            std::string pathStr(md2Path);
            size_t slashPos = pathStr.find_last_of("/\\");
            if (slashPos != std::string::npos)
                modelDir = pathStr.substr(0, slashPos + 1);
        }

        for (int i = 0; i < numSkins; ++i)
        {
            const char* skinName = skinNames + i * MD2_MAX_SKINNAME;

            // Skin path is relative to model directory.
            // E.g., skin "players/male/grunt.pcx" + modelDir "models/player/"
            // -> full path: "models/players/male/grunt.pcx"
            std::string skinPath;
            if (modelDir.empty() || skinName[0] == '/')
                skinPath = skinName;
            else
                skinPath = modelDir + skinName;

            // Try exact path first, then swap extension.
            // MD2 stores .pcx names but actual files may be .jpg/.png/.tga.
            const char* exts[] = { nullptr, ".jpg", ".jpeg", ".png", ".tga" };
            bool loaded = false;
            ImageRGBA8 img;

            // First try the exact skin name from the MD2
            if (assets->readAllBytes(skinPath.c_str(), fileData) &&
                loadImageRGBA8FromMemory(fileData.data(), fileData.size(), img, nullptr))
            {
                loaded = true;
            }
            else
            {
                // Strip extension from skinName, try each alternative
                std::string skinBase = skinPath;
                size_t dotPos = skinBase.find_last_of('.');
                if (dotPos != std::string::npos)
                    skinBase = skinBase.substr(0, dotPos);

                for (int e = 1; e < 5 && !loaded; ++e)
                {
                    std::string trial = skinBase + exts[e];
                    if (assets->readAllBytes(trial.c_str(), fileData) &&
                        loadImageRGBA8FromMemory(fileData.data(), fileData.size(), img, nullptr))
                    {
                        loaded = true;
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

                m_skinTextures[i] = backend->createTexture(td);
                m_skinSamplers[i] = backend->createSampler(td);
            }
            else if (i > 0 && m_skinTextures[0] != INVALID_TEXTURE)
            {
                // Fallback: reuse first skin for missing alt skins.
                // Many converted MD2 models list multiple skin slots
                // (e.g. cobra.pcx, cobra2.pcx, cobra3.pcx) but only
                // ship one texture file.
                m_skinTextures[i] = m_skinTextures[0];
                m_skinSamplers[i] = m_skinSamplers[0];
                Logger::instance().warn("MD2: skin '%s' not found, reusing first skin", skinPath.c_str());
            }
            else
            {
                // Fallback: 1x1 white texture
                Logger::instance().warn("MD2: skin '%s' not found, using fallback", skinPath.c_str());
                uint8_t white[4] = {255, 255, 255, 255};
                TextureDesc td{};
                td.type = TextureType::Texture2D;
                td.width = td.height = 1;
                td.format = TextureFormat::SRGBA8;
                td.minFilter = TextureFilter::Nearest;
                td.magFilter = TextureFilter::Nearest;
                td.wrapU = TextureWrap::Repeat;
                td.wrapV = TextureWrap::Repeat;
                td.mipLevels = 1;
                td.initialData = white;
                m_skinTextures[i] = backend->createTexture(td);
                m_skinSamplers[i] = backend->createSampler(td);
            }
        }
    }

    // Detect animation ranges from frame names
    detectAnimations();

    Logger::instance().info("MD2: loaded '%s' (%d verts, %d tris, %d frames, %d skins)",
                            md2Path, m_numVerts, m_numTris, m_numFrames, (int)m_skinTextures.size());
    return true;
}

void MD2Mesh::release(IRenderBackend* backend)
{
    if (!backend) return;
    if (m_vertexBuffer != INVALID_BUFFER) { backend->destroyBuffer(m_vertexBuffer); m_vertexBuffer = INVALID_BUFFER; }
    if (m_indexBuffer != INVALID_BUFFER)  { backend->destroyBuffer(m_indexBuffer);  m_indexBuffer = INVALID_BUFFER; }
    for (auto t : m_skinTextures)  { if (t != INVALID_TEXTURE)  backend->destroyTexture(t); }
    for (auto s : m_skinSamplers)  { if (s != INVALID_SAMPLER)  backend->destroySampler(s); }
    m_skinTextures.clear();
    m_skinSamplers.clear();
    m_frameVerts.clear();
    m_frameData.clear();
    m_frameNames.clear();
    m_anims.clear();
}

void MD2Mesh::updateVertices(IRenderBackend* backend, const MD2VertexPacked* vertices, int vertexCount)
{
    if (!backend || !vertices || vertexCount <= 0) return;
    backend->setBufferData(m_vertexBuffer, vertices, vertexCount * sizeof(MD2VertexPacked));
}

void MD2Mesh::draw(IRenderBackend* backend) const
{
    if (!backend || m_vertexBuffer == INVALID_BUFFER || m_indexBuffer == INVALID_BUFFER) return;

    backend->bindVertexBuffer(m_vertexBuffer, 0, &kLayoutMD2);
    backend->bindIndexBuffer(m_indexBuffer);

    // Bind first skin texture if available
    if (!m_skinTextures.empty() && m_skinTextures[0] != INVALID_TEXTURE)
        backend->bindTexture(m_skinTextures[0], m_skinSamplers[0], 1);

    backend->drawIndexed(m_numTris * 3, 0);
}

const char* MD2Mesh::frameName(int frame) const
{
    if (frame < 0 || frame >= m_numFrames) return "";
    return m_frameNames.data() + frame * MD2_MAX_FRAME_NAME;
}

void MD2Mesh::buildFrameData(const uint8_t* frameBytes, int frameIdx, MD2VertexPacked* outVerts, int frameVertCount) const
{
    if (frameIdx < 0 || frameIdx >= m_numFrames) return;
    if (frameVertCount < 0) frameVertCount = m_numVerts;

    const int frameByteSize = sizeof(MD2AliasFrame) + frameVertCount * sizeof(MD2AliasVertex);
    const MD2AliasFrame* aliasFrame = reinterpret_cast<const MD2AliasFrame*>(frameBytes + frameIdx * frameByteSize);
    const MD2AliasVertex* aliasVerts = reinterpret_cast<const MD2AliasVertex*>(aliasFrame + 1);

    for (int i = 0; i < frameVertCount; ++i)
    {
        Vec3 pos = q2ToGL(
            aliasFrame->scale[0] * aliasVerts[i].v[0] + aliasFrame->translate[0],
            aliasFrame->scale[1] * aliasVerts[i].v[1] + aliasFrame->translate[1],
            aliasFrame->scale[2] * aliasVerts[i].v[2] + aliasFrame->translate[2]);

        outVerts[i].pos[0] = pos.x;
        outVerts[i].pos[1] = pos.y;
        outVerts[i].pos[2] = pos.z;

        uint8_t ni = aliasVerts[i].normalIndex;
        if (ni >= MD2_NUM_ANORMS) ni = 0;
        outVerts[i].normal[0] = g_md2Normals[ni].x;
        outVerts[i].normal[1] = g_md2Normals[ni].y;
        outVerts[i].normal[2] = g_md2Normals[ni].z;

        outVerts[i].lmUV[0] = -1.0f;
        outVerts[i].lmUV[1] = -1.0f;
        outVerts[i].color[0] = 255;
        outVerts[i].color[1] = 255;
        outVerts[i].color[2] = 255;
        outVerts[i].color[3] = 255;
    }
}

// Build frame data with vertex splitting: each original vertex may map to multiple GPU vertices
void MD2Mesh::buildFrameDataSplit(const uint8_t* frameBytes, int frameIdx, MD2VertexPacked* outVerts, int frameVertCount) const
{
    if (frameIdx < 0 || frameIdx >= m_numFrames) return;
    if (frameVertCount <= 0) frameVertCount = m_frameVertCount;

    const int frameByteSize = sizeof(MD2AliasFrame) + frameVertCount * sizeof(MD2AliasVertex);
    const MD2AliasFrame* aliasFrame = reinterpret_cast<const MD2AliasFrame*>(frameBytes + frameIdx * frameByteSize);
    const MD2AliasVertex* aliasVerts = reinterpret_cast<const MD2AliasVertex*>(aliasFrame + 1);

    // First compute positions/normals for each original vertex
    struct RawVert { float px, py, pz; float nx, ny, nz; };
    std::vector<RawVert> rawVerts(frameVertCount);
    for (int i = 0; i < frameVertCount; ++i)
    {
        Vec3 pos = q2ToGL(
            aliasFrame->scale[0] * aliasVerts[i].v[0] + aliasFrame->translate[0],
            aliasFrame->scale[1] * aliasVerts[i].v[1] + aliasFrame->translate[1],
            aliasFrame->scale[2] * aliasVerts[i].v[2] + aliasFrame->translate[2]);
        rawVerts[i].px = pos.x; rawVerts[i].py = pos.y; rawVerts[i].pz = pos.z;

        uint8_t ni = aliasVerts[i].normalIndex;
        if (ni >= MD2_NUM_ANORMS) ni = 0;
        rawVerts[i].nx = g_md2Normals[ni].x;
        rawVerts[i].ny = g_md2Normals[ni].y;
        rawVerts[i].nz = g_md2Normals[ni].z;
    }

    // Now duplicate into split vertices
    for (int i = 0; i < frameVertCount; ++i)
    {
        for (const auto& split : m_splitInfo[i])
        {
            if (split.splitIdx < m_numVerts)
            {
                outVerts[split.splitIdx].pos[0] = rawVerts[i].px;
                outVerts[split.splitIdx].pos[1] = rawVerts[i].py;
                outVerts[split.splitIdx].pos[2] = rawVerts[i].pz;
                outVerts[split.splitIdx].normal[0] = rawVerts[i].nx;
                outVerts[split.splitIdx].normal[1] = rawVerts[i].ny;
                outVerts[split.splitIdx].normal[2] = rawVerts[i].nz;
                outVerts[split.splitIdx].lmUV[0] = -1.0f;
                outVerts[split.splitIdx].lmUV[1] = -1.0f;
                outVerts[split.splitIdx].color[0] = 255;
                outVerts[split.splitIdx].color[1] = 255;
                outVerts[split.splitIdx].color[2] = 255;
                outVerts[split.splitIdx].color[3] = 255;
            }
        }
    }
}

void MD2Mesh::detectAnimations()
{
    // Parse frame names and group by prefix (letters before trailing digits).
    // E.g., "stand01" -> prefix "stand", "run05" -> prefix "run".
    m_anims.clear();

    for (int f = 0; f < m_numFrames; ++f)
    {
        const char* name = frameName(f);
        if (!name || name[0] == '\0') continue;

        // Extract prefix: scan from end, strip trailing digits.
        int end = 0;
        while (name[end] != '\0') end++;
        int prefixLen = end;
        while (prefixLen > 0 && std::isdigit(static_cast<unsigned char>(name[prefixLen - 1])))
            prefixLen--;

        if (prefixLen == 0) continue; // no prefix, skip

        std::string prefix(name, prefixLen);

        auto it = m_anims.find(prefix);
        if (it == m_anims.end())
        {
            MD2AnimRange range;
            range.first = f;
            range.last  = f;
            range.fps   = 10.0f;
            m_anims[prefix] = range;
        }
        else
        {
            it->second.last = f;
        }
    }

    // Set default FPS based on animation name
    static const struct { const char* name; float fps; } fpsTable[] = {
        {"stand",  10.0f},
        {"run",    10.0f},
        {"attack", 10.0f},
        {"pain",   10.0f},
        {"jump",   10.0f},
        {"flip",   10.0f},
        {"salute", 10.0f},
        {"taunt",  10.0f},
        {"wave",   10.0f},
        {"point",  10.0f},
        {"crstnd", 10.0f},
        {"crwalk", 10.0f},
        {"crattk", 10.0f},
        {"crpain", 10.0f},
        {"crdeath",10.0f},
        {"death",  10.0f},
        {"idle",   10.0f},
    };

    for (auto& [name, range] : m_anims)
    {
        for (const auto& entry : fpsTable)
        {
            if (name.find(entry.name) != std::string::npos)
            {
                range.fps = entry.fps;
                break;
            }
        }
    }

    // Debug: log detected animations
    Logger::instance().info("MD2: detected %zu animation ranges:", m_anims.size());
    for (const auto& [name, range] : m_anims)
    {
        Logger::instance().info("  '%s': frames %d-%d (%.0f fps)",
                                name.c_str(), range.first, range.last, range.fps);
    }
    if (m_anims.empty())
    {
        // Print first few frame names for debugging
        for (int f = 0; f < std::min(m_numFrames, 5); ++f)
        {
            const char* fname = frameName(f);
            Logger::instance().info("  frame[%d]: '%s'", f, fname && fname[0] ? fname : "(empty)");
        }
    }
}

const MD2AnimRange* MD2Mesh::findAnim(const char* prefix) const
{
    auto it = m_anims.find(prefix);
    return (it != m_anims.end()) ? &it->second : nullptr;
}

// =====================================================================
//  MD2Instance Implementation
// =====================================================================

void MD2Instance::update(float dt)
{
    frameTime += dt;
    float frameDuration = 1.0f / fps;

    while (frameTime >= frameDuration)
    {
        frameTime -= frameDuration;
        currentFrame = nextFrame;
        nextFrame++;

        if (nextFrame > animLast)
        {
            // Loop back to first frame
            nextFrame = animFirst;
        }
    }

    lerpT = frameTime / frameDuration;
    if (lerpT > 1.0f) lerpT = 1.0f;
}

void MD2Instance::setAnim(int first, int last, float animFps)
{
    animFirst = first;
    animLast  = last;
    fps       = animFps;

    if (currentFrame < first || currentFrame > last)
    {
        currentFrame = first;
        nextFrame    = first + 1;
        if (nextFrame > last) nextFrame = first;
    }
    frameTime = 0.0f;
    lerpT     = 0.0f;
}

void MD2Instance::setAnim(const MD2AnimRange& range)
{
    setAnim(range.first, range.last, range.fps);
}

// =====================================================================
//  Interpolation helper (CPU-side, called each frame before upload)
// =====================================================================
void MD2Mesh::interpolateFrame(int frameA, int frameB, float t,
                               MD2VertexPacked* outVerts, int outVertCount) const
{
    int fvc = m_frameVertCount;
    const float* scaleA = m_frameData.data() + frameA * 6;
    const float* transA = scaleA + 3;
    const float* scaleB = m_frameData.data() + frameB * 6;
    const float* transB = scaleB + 3;

    const uint8_t* rawA = &m_frameVerts[frameA * fvc * 4];
    const uint8_t* rawB = &m_frameVerts[frameB * fvc * 4];

    // Interpolate positions/normals on original vertices
    struct InterpVert { float px, py, pz; float nx, ny, nz; };
    std::vector<InterpVert> interpVerts(fvc);
    for (int i = 0; i < fvc; ++i)
    {
        Vec3 pA = q2ToGL(
            scaleA[0] * rawA[i * 4 + 0] + transA[0],
            scaleA[1] * rawA[i * 4 + 1] + transA[1],
            scaleA[2] * rawA[i * 4 + 2] + transA[2]);

        Vec3 pB = q2ToGL(
            scaleB[0] * rawB[i * 4 + 0] + transB[0],
            scaleB[1] * rawB[i * 4 + 1] + transB[1],
            scaleB[2] * rawB[i * 4 + 2] + transB[2]);

        interpVerts[i].px = pA.x + t * (pB.x - pA.x);
        interpVerts[i].py = pA.y + t * (pB.y - pA.y);
        interpVerts[i].pz = pA.z + t * (pB.z - pA.z);

        uint8_t niA = rawA[i * 4 + 3];
        if (niA >= MD2_NUM_ANORMS) niA = 0;
        interpVerts[i].nx = g_md2Normals[niA].x;
        interpVerts[i].ny = g_md2Normals[niA].y;
        interpVerts[i].nz = g_md2Normals[niA].z;
    }

    // Initialize all output vertices with defaults
    for (int i = 0; i < outVertCount; ++i)
    {
        outVerts[i].pos[0] = 0; outVerts[i].pos[1] = 0; outVerts[i].pos[2] = 0;
        outVerts[i].normal[0] = 0; outVerts[i].normal[1] = 0; outVerts[i].normal[2] = 1;
        outVerts[i].uv[0] = m_uvs[i * 2 + 0];
        outVerts[i].uv[1] = m_uvs[i * 2 + 1];
        outVerts[i].lmUV[0] = -1.0f;
        outVerts[i].lmUV[1] = -1.0f;
        outVerts[i].color[0] = 255; outVerts[i].color[1] = 255;
        outVerts[i].color[2] = 255; outVerts[i].color[3] = 255;
    }

    // Scatter interpolated data to split vertices
    for (int i = 0; i < fvc && i < (int)m_splitInfo.size(); ++i)
    {
        for (const auto& split : m_splitInfo[i])
        {
            if (split.splitIdx < outVertCount)
            {
                outVerts[split.splitIdx].pos[0] = interpVerts[i].px;
                outVerts[split.splitIdx].pos[1] = interpVerts[i].py;
                outVerts[split.splitIdx].pos[2] = interpVerts[i].pz;
                outVerts[split.splitIdx].normal[0] = interpVerts[i].nx;
                outVerts[split.splitIdx].normal[1] = interpVerts[i].ny;
                outVerts[split.splitIdx].normal[2] = interpVerts[i].nz;
            }
        }
    }
}

// =====================================================================
//  ModelRenderer Implementation
// =====================================================================

ModelRenderer::~ModelRenderer()
{
    // GPU resources are released by the IMesh destructor / release calls.
    m_meshes.clear();
    m_entities.clear();
}

int ModelRenderer::loadMD2Model(IRenderBackend* backend, AssetFS* assets, const char* md2Path)
{
    if (!backend || !md2Path) return -1;

    auto mesh = std::make_unique<MD2Mesh>();
    if (!mesh->load(backend, assets, md2Path))
        return -1;

    ModelEntry entry;
    entry.type = ModelType::MD2;
    entry.mesh = std::move(mesh);
    m_meshes.push_back(std::move(entry));
    return (int)m_meshes.size() - 1;
}

int ModelRenderer::loadOBJModel(IRenderBackend* backend, AssetFS* assets, const char* objPath)
{
    if (!backend || !objPath) return -1;

    auto mesh = std::make_unique<StaticMesh>();
    if (!static_cast<StaticMesh*>(mesh.get())->build(backend, assets, objPath))
        return -1;

    ModelEntry entry;
    entry.type = ModelType::Static;
    entry.mesh = std::move(mesh);
    m_meshes.push_back(std::move(entry));
    return (int)m_meshes.size() - 1;
}

const IMesh* ModelRenderer::getMesh(int modelIndex) const
{
    if (modelIndex < 0 || modelIndex >= (int)m_meshes.size()) return nullptr;
    return m_meshes[modelIndex].mesh.get();
}

const MD2Mesh* ModelRenderer::getMD2Mesh(int modelIndex) const
{
    if (modelIndex < 0 || modelIndex >= (int)m_meshes.size()) return nullptr;
    if (m_meshes[modelIndex].type != ModelType::MD2) return nullptr;
    return static_cast<const MD2Mesh*>(m_meshes[modelIndex].mesh.get());
}

void ModelRenderer::registerEntity(int entityIndex, int modelIndex)
{
    if (entityIndex < 0) return;
    if (modelIndex < 0 || modelIndex >= (int)m_meshes.size()) return;

    // Ensure enough space
    if (entityIndex >= (int)m_entities.size())
        m_entities.resize(entityIndex + 1);

    m_entities[entityIndex].modelIndex = modelIndex;
    m_entities[entityIndex].type = m_meshes[modelIndex].type;
    m_entities[entityIndex].instance.skinIndex = 0;
}

void ModelRenderer::updateEntity(int entityIndex, float dt, const Vec3& origin, const Vec3& angles)
{
    if (entityIndex < 0 || entityIndex >= (int)m_entities.size()) return;

    EntityRecord& rec = m_entities[entityIndex];
    if (rec.modelIndex < 0) return;

    if (rec.type == ModelType::MD2)
    {
        rec.instance.origin = origin;
        rec.instance.angles = angles;
        rec.instance.update(dt);
    }
    else
    {
        rec.staticInstance.origin = origin;
        rec.staticInstance.angles = angles;
    }
}

void ModelRenderer::setEntityAnim(int entityIndex, const char* animName)
{
    if (entityIndex < 0 || entityIndex >= (int)m_entities.size()) return;

    EntityRecord& rec = m_entities[entityIndex];
    if (rec.modelIndex < 0 || rec.type != ModelType::MD2) return;

    const MD2Mesh* mesh = static_cast<const MD2Mesh*>(m_meshes[rec.modelIndex].mesh.get());
    if (!mesh) return;

    if (const MD2AnimRange* range = mesh->findAnim(animName))
        rec.instance.setAnim(*range);
}

void ModelRenderer::setEntityAnimRange(int entityIndex, int first, int last, float fps)
{
    if (entityIndex < 0 || entityIndex >= (int)m_entities.size()) return;

    EntityRecord& rec = m_entities[entityIndex];
    if (rec.modelIndex < 0 || rec.type != ModelType::MD2) return;

    rec.instance.setAnim(first, last, fps);
}

MD2Instance* ModelRenderer::getMD2Instance(int entityIndex)
{
    if (entityIndex < 0 || entityIndex >= (int)m_entities.size()) return nullptr;
    EntityRecord& rec = m_entities[entityIndex];
    if (rec.type != ModelType::MD2) return nullptr;
    return &rec.instance;
}

MeshInstance* ModelRenderer::getStaticInstance(int entityIndex)
{
    if (entityIndex < 0 || entityIndex >= (int)m_entities.size()) return nullptr;
    EntityRecord& rec = m_entities[entityIndex];
    if (rec.type != ModelType::Static) return nullptr;
    return &rec.staticInstance;
}

void ModelRenderer::renderEntity(IRenderBackend* backend, int modelIndex,
                                  const MD2Instance& instance, ShaderHandle shader) const
{
    if (modelIndex < 0 || modelIndex >= (int)m_meshes.size()) return;
    const auto& entry = m_meshes[modelIndex];
    if (entry.type != ModelType::MD2) return;

    const MD2Mesh* mesh = static_cast<const MD2Mesh*>(entry.mesh.get());

    // Interpolate vertices on CPU
    std::vector<MD2VertexPacked> verts(mesh->numVertices());
    mesh->interpolateFrame(instance.currentFrame, instance.nextFrame, instance.lerpT,
                           verts.data(), mesh->numVertices());

    // Upload interpolated vertices
    const_cast<MD2Mesh*>(mesh)->updateVertices(backend, verts.data(), mesh->numVertices());

    // Draw (uses first skin texture)
    mesh->draw(backend);
    (void)shader; // caller sets uModelMatrix/uModelMode uniforms before calling this
}

void ModelRenderer::renderStatic(IRenderBackend* backend, int modelIndex,
                                  const MeshInstance& instance, ShaderHandle shader) const
{
    if (modelIndex < 0 || modelIndex >= (int)m_meshes.size()) return;
    const auto& entry = m_meshes[modelIndex];
    if (entry.type != ModelType::Static) return;

    // Set model matrix uniform (caller should have set uModelMode=1)
    Mat4 world = instance.worldMatrix();
    GLint loc = glGetUniformLocation(static_cast<GLuint>(shader), "uModelMatrix");
    if (loc >= 0)
        glUniformMatrix4fv(loc, 1, GL_FALSE, world.data());

    entry.mesh->draw(backend);
}

void ModelRenderer::renderAll(IRenderBackend* backend, ShaderHandle shader,
                               const BSPMap* bsp, const Vec3* cameraPos) const
{
    // Set model mode once for all entities
    GLint modelModeLoc = glGetUniformLocation(static_cast<GLuint>(shader), "uModelMode");
    if (modelModeLoc >= 0) glUniform1i(modelModeLoc, 1);

    for (size_t i = 0; i < m_entities.size(); ++i)
    {
        const EntityRecord& rec = m_entities[i];
        if (rec.modelIndex < 0) continue;

        if (rec.type == ModelType::MD2)
        {
            // Frustum cull (sphere test with estimated radius)
            if (bsp && cameraPos)
            {
                if (!bsp->sphereInFrustum(rec.instance.origin, 40.0f))
                    continue;
            }

            // Build model matrix
            Mat4 model = Mat4::translate(rec.instance.origin);
            model = model * Mat4::rotate(rec.instance.angles.y, Vec3{0, 1, 0});
            model = model * Mat4::rotate(rec.instance.angles.x, Vec3{1, 0, 0});
            model = model * Mat4::rotate(rec.instance.angles.z, Vec3{0, 0, 1});
            model = model * Mat4::scale(Vec3{rec.instance.scale, rec.instance.scale, rec.instance.scale});

            GLint modelMatrixLoc = glGetUniformLocation(static_cast<GLuint>(shader), "uModelMatrix");
            if (modelMatrixLoc >= 0) glUniformMatrix4fv(modelMatrixLoc, 1, GL_FALSE, model.data());

            // BSP lightmap sampling
            GLint modelLightLoc = glGetUniformLocation(static_cast<GLuint>(shader), "uModelLightColor");
            if (modelLightLoc >= 0)
            {
                if (bsp)
                {
                    Vec3 lightColor = bsp->samplePointLighting(rec.instance.origin);
                    glUniform3f(modelLightLoc, lightColor.x, lightColor.y, lightColor.z);
                }
                else
                {
                    glUniform3f(modelLightLoc, 1.0f, 1.0f, 1.0f);
                }
            }

            renderEntity(backend, rec.modelIndex, rec.instance, shader);
        }
        else
        {
            // Frustum cull for static models
            if (bsp)
            {
                if (!bsp->sphereInFrustum(rec.staticInstance.origin, 40.0f))
                    continue;
            }

            // BSP lightmap sampling
            GLint modelLightLoc = glGetUniformLocation(static_cast<GLuint>(shader), "uModelLightColor");
            if (modelLightLoc >= 0)
            {
                if (bsp)
                {
                    Vec3 lightColor = bsp->samplePointLighting(rec.staticInstance.origin);
                    glUniform3f(modelLightLoc, lightColor.x, lightColor.y, lightColor.z);
                }
                else
                {
                    glUniform3f(modelLightLoc, 1.0f, 1.0f, 1.0f);
                }
            }

            renderStatic(backend, rec.modelIndex, rec.staticInstance, shader);
        }
    }

    // Reset model mode
    if (modelModeLoc >= 0) glUniform1i(modelModeLoc, 0);
    GLint modelMatrixLoc = glGetUniformLocation(static_cast<GLuint>(shader), "uModelMatrix");
    if (modelMatrixLoc >= 0) glUniformMatrix4fv(modelMatrixLoc, 1, GL_FALSE, Mat4::identity().data());
}

} // namespace nova
