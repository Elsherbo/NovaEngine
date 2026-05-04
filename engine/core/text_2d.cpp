// ============================================================
// FILE:    engine/core/text_2d.cpp
// MODULE:  Core > 2D Text Overlay
// VERSION: v5 — Full Q2 conchars support (normal + alternate set)
//
// FONT LOADING:
//   Q2 conchars: any square image whose width is a multiple of 16.
//     Top half    (rows 0-7)  → normal atlas    (s_fontTex)
//     Bottom half (rows 8-15) → alternate atlas  (s_fontTexAlt)
//   Generic bitmap: 128×128 single-set image.
//   Built-in: IBM CP437 8×8 hardcoded (always available as fallback).
//
// ATLAS FORMAT (internal):
//   Always 128×128 RGBA8.
//   16×16 grid, 8×8 pixels per glyph.
//   Alpha channel = glyph mask (0=transparent, 255=opaque).
//
// SAMPLER BUG FIX (carried from v4):
//   glBindSampler(0, 0) before drawing text detaches any sampler
//   object left by BSP lightmap rendering. Without this, a mipmap
//   sampler makes the font atlas return (0,0,0,0) → invisible text.
//
// ALTERNATE SET:
//   drawCharAlt / drawStringAlt use s_fontTexAlt if loaded.
//   Falls back to s_fontTex (normal set) if no alternate is available.
//   In console.cpp: use drawStringAlt for header, warnings, accents.
// ============================================================

#include "engine/core/text_2d.h"
#include "engine/core/image_load.h"

#include <glad/glad.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

namespace nova
{

// ---------------------------------------------------------------------------
// Static storage
// ---------------------------------------------------------------------------
uint32_t Text2D::s_fontTex         = 0;
uint32_t Text2D::s_fontTexAlt      = 0;
uint32_t Text2D::s_textProg        = 0;
uint32_t Text2D::s_fillProg        = 0;
uint32_t Text2D::s_vbo             = 0;
uint32_t Text2D::s_vao             = 0;
uint32_t Text2D::s_fillVao         = 0;
bool     Text2D::s_initialized     = false;
bool     Text2D::s_inFrame         = false;
int      Text2D::s_screenW         = 0;
int      Text2D::s_screenH         = 0;
int      Text2D::s_scale           = 2;
bool     Text2D::s_usingExternalFont = false;
Text2D::FontSet Text2D::s_currentFontSet = Text2D::FontSet::Normal;
float    Text2D::s_vbuf[kMaxQuads * kFloatsPerQuad];
int      Text2D::s_vertexCount     = 0;
bool     Text2D::s_inFillMode      = false;

// ---------------------------------------------------------------------------
// Built-in IBM CP437 8×8 font bitmap (ASCII 32-127, 96 chars × 8 rows)
// ---------------------------------------------------------------------------
static const uint8_t kFont8x8[96 * 8] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // ' '  32
    0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00, // '!'
    0x6C,0x6C,0x00,0x00,0x00,0x00,0x00,0x00, // '"'
    0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00, // '#'
    0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00, // '$'
    0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00, // '%'
    0x38,0x6C,0x38,0x76,0xCC,0xCC,0x76,0x00, // '&'
    0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00, // '\''
    0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00, // '('
    0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00, // ')'
    0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00, // '*'
    0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00, // '+'
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30, // ','
    0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00, // '-'
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00, // '.'
    0x00,0x06,0x0C,0x18,0x30,0x60,0xC0,0x00, // '/'
    0x38,0x6C,0xC6,0xC6,0xD6,0x6C,0x38,0x00, // '0'
    0x18,0x38,0x78,0x18,0x18,0x18,0x7E,0x00, // '1'
    0x7C,0xC6,0x06,0x3C,0x60,0xC6,0xFE,0x00, // '2'
    0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00, // '3'
    0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00, // '4'
    0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00, // '5'
    0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00, // '6'
    0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00, // '7'
    0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00, // '8'
    0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00, // '9'
    0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00, // ':'
    0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00, // ';'
    0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00, // '<'
    0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00, // '='
    0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00, // '>'
    0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00, // '?'
    0x38,0x6C,0xDE,0xDE,0xC0,0x78,0x00,0x00, // '@'
    0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00, // 'A'
    0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00, // 'B'
    0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00, // 'C'
    0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00, // 'D'
    0xFE,0x62,0x60,0x78,0x60,0x62,0xFE,0x00, // 'E'
    0xFE,0x62,0x60,0x78,0x60,0x60,0xF0,0x00, // 'F'
    0x7C,0xC6,0xC0,0xCE,0xC6,0xC6,0x7E,0x00, // 'G'
    0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00, // 'H'
    0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00, // 'I'
    0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00, // 'J'
    0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00, // 'K'
    0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00, // 'L'
    0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00, // 'M'
    0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00, // 'N'
    0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00, // 'O'
    0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00, // 'P'
    0x7C,0xC6,0xC6,0xC6,0xD6,0x6C,0x76,0x00, // 'Q'
    0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00, // 'R'
    0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00, // 'S'
    0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00, // 'T'
    0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00, // 'U'
    0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00, // 'V'
    0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00, // 'W'
    0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00, // 'X'
    0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00, // 'Y'
    0xFE,0xC6,0x86,0x0C,0x18,0x30,0xFE,0x00, // 'Z'
    0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00, // '['
    0x00,0xC0,0x60,0x30,0x18,0x0C,0x06,0x00, // '\\'
    0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00, // ']'
    0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00, // '^'
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF, // '_'
    0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00, // '`'
    0x00,0x00,0x7C,0x0C,0x7C,0xCC,0x76,0x00, // 'a'
    0xE0,0x60,0x60,0x7C,0x66,0x66,0xDC,0x00, // 'b'
    0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00, // 'c'
    0x1C,0x0C,0x0C,0x7C,0xCC,0xCC,0x76,0x00, // 'd'
    0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00, // 'e'
    0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00, // 'f'
    0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x78, // 'g'
    0xE0,0x60,0x60,0x6C,0x76,0x66,0xE6,0x00, // 'h'
    0x18,0x18,0x00,0x38,0x18,0x18,0x3C,0x00, // 'i'
    0x0C,0x0C,0x00,0x1C,0x0C,0xCC,0xCC,0x78, // 'j'
    0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00, // 'k'
    0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00, // 'l'
    0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xC6,0x00, // 'm'
    0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00, // 'n'
    0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00, // 'o'
    0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0, // 'p'
    0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E, // 'q'
    0x00,0x00,0xDE,0x76,0x60,0x60,0xF0,0x00, // 'r'
    0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00, // 's'
    0x18,0x18,0x7E,0x18,0x18,0x18,0x0E,0x00, // 't'
    0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00, // 'u'
    0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00, // 'v'
    0x00,0x00,0xC6,0xD6,0xFE,0xEE,0xC6,0x00, // 'w'
    0x00,0x00,0xCC,0x6C,0x38,0x6C,0xCC,0x00, // 'x'
    0x00,0x00,0xCC,0xCC,0xCC,0x7C,0x0C,0xF8, // 'y'
    0x00,0x00,0xFE,0xCC,0x18,0x30,0xFE,0x00, // 'z'
    0x0C,0x18,0x18,0x70,0x18,0x18,0x0C,0x00, // '{'
    0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00, // '|'
    0x30,0x18,0x18,0x0E,0x18,0x18,0x30,0x00, // '}'
    0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00, // '~'
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // DEL -> solid block
};

// ---------------------------------------------------------------------------
// GLSL shaders
// ---------------------------------------------------------------------------
static const char* kVSText = R"GLSL(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreenSize;
out vec2 vUV;
out vec4 vColor;
void main()
{
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV    = aUV;
    vColor = aColor;
}
)GLSL";

static const char* kFSText = R"GLSL(
#version 450 core
in vec2  vUV;
in vec4  vColor;
uniform sampler2D uFontTex;
out vec4 fragColor;
void main()
{
    float mask = texture(uFontTex, vUV).a;
    if (mask < 0.05) discard;
    fragColor = vec4(vColor.rgb, vColor.a * mask);
}
)GLSL";

static const char* kVSFill = R"GLSL(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreenSize;
out vec4 vColor;
void main()
{
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vColor = aColor;
}
)GLSL";

static const char* kFSFill = R"GLSL(
#version 450 core
in vec4 vColor;
out vec4 fragColor;
void main() { fragColor = vColor; }
)GLSL";

// ---------------------------------------------------------------------------
// GL helpers
// ---------------------------------------------------------------------------
static uint32_t compileProg(const char* vs, const char* fs)
{
    GLuint prog = glCreateProgram();
    char log[512]; GLint ok;
    auto stage = [&](GLenum type, const char* src)
    {
        if (!src) return;
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(sh, 512, nullptr, log);
                   fprintf(stderr, "[Text2D] shader compile: %s\n", log); }
        glAttachShader(prog, sh);
        glDeleteShader(sh);
    };
    stage(GL_VERTEX_SHADER, vs);
    stage(GL_FRAGMENT_SHADER, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(prog, 512, nullptr, log);
               fprintf(stderr, "[Text2D] shader link: %s\n", log); }
    return prog;
}

struct GL2DState
{
    GLint  prog, vao, vbo, texUnit, tex2D_unit0, sampler_unit0;
    GLboolean depth, cull, blend, depthMask;
    GLint  blendSrcRGB, blendDstRGB, blendSrcA, blendDstA, depthFunc;
    GLint  clipOrigin, clipDepth;
};

static void saveGL(GL2DState& s)
{
    glGetIntegerv(GL_CURRENT_PROGRAM,      &s.prog);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.vbo);
    glGetIntegerv(GL_ACTIVE_TEXTURE,       &s.texUnit);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D,   &s.tex2D_unit0);
    glGetIntegerv(GL_SAMPLER_BINDING,      &s.sampler_unit0);
    s.depth    = glIsEnabled(GL_DEPTH_TEST);
    s.cull     = glIsEnabled(GL_CULL_FACE);
    s.blend    = glIsEnabled(GL_BLEND);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &s.depthMask);
    glGetIntegerv(GL_BLEND_SRC_RGB,   &s.blendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB,   &s.blendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.blendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &s.blendDstA);
    glGetIntegerv(GL_DEPTH_FUNC,      &s.depthFunc);
    glGetIntegerv(GL_CLIP_ORIGIN,     &s.clipOrigin);
    glGetIntegerv(GL_CLIP_DEPTH_MODE, &s.clipDepth);
}

static void set2DState()
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_ALWAYS);
}

static void restoreGL(const GL2DState& s)
{
    glBindVertexArray(s.vao);
    glBindBuffer(GL_ARRAY_BUFFER, s.vbo);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s.tex2D_unit0);
    glBindSampler(0, (GLuint)s.sampler_unit0);  // restore BSP sampler
    glActiveTexture((GLenum)s.texUnit);
    glUseProgram((GLuint)s.prog);
    glDepthMask(s.depthMask);
    glDepthFunc((GLenum)s.depthFunc);
    glClipControl((GLenum)s.clipOrigin, (GLenum)s.clipDepth);
    if (s.depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (s.cull)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
    if (s.blend) glEnable(GL_BLEND);      else glDisable(GL_BLEND);
    glBlendFuncSeparate((GLenum)s.blendSrcRGB, (GLenum)s.blendDstRGB,
                        (GLenum)s.blendSrcA,   (GLenum)s.blendDstA);
}

static void setupVAOAttribs(GLuint vbo)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    const GLsizei stride = Text2D::kFloatsPerVertex * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2*sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4*sizeof(float)));
}

// ---------------------------------------------------------------------------
// buildBuiltinAtlasBuffer — unpack kFont8x8 into a 128×128 RGBA8 buffer
// ---------------------------------------------------------------------------
void Text2D::buildBuiltinAtlasBuffer(uint8_t* out)
{
    memset(out, 0, 128 * 128 * 4);
    for (int code = 0; code < 128; ++code)
    {
        int gx = code % 16, gy = code / 16;
        int ax = gx * 8,    ay = gy * 8;
        if (code >= 32 && code <= 127)
        {
            int fi = (code - 32) * 8;
            for (int row = 0; row < 8; ++row)
            {
                uint8_t bits = kFont8x8[fi + row];
                for (int col = 0; col < 8; ++col)
                {
                    uint8_t v = ((bits >> (7 - col)) & 1) ? 255 : 0;
                    int idx = ((ay + row) * 128 + (ax + col)) * 4;
                    out[idx+0] = out[idx+1] = out[idx+2] = v;
                    out[idx+3] = v;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// uploadAtlasToGPU — create or replace a GL texture from a 128×128 RGBA8 buf
// ---------------------------------------------------------------------------
void Text2D::uploadAtlasToGPU(uint32_t& texId, const uint8_t* rgba8, int w, int h)
{
    if (texId) { glDeleteTextures(1, &texId); texId = 0; }

    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, rgba8);
    // NEAREST filter — pixel-perfect at exact scale, no mip chain needed
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Explicitly no mipmaps — prevents mipmap sampler bug from BSP renderer
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,  0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// ---------------------------------------------------------------------------
// applyColorKey — extract one half of a conchars source into a 128×128 atlas.
//
// src       : raw RGBA8 source image pixels
// srcSize   : source image width/height (square)
// glyphSrc  : source pixels per glyph (srcSize/16)
// atlas     : output 128×128 RGBA8 buffer (must be zeroed by caller)
// glyphDst  : destination pixels per glyph (always 8)
// keyR/G/B  : background color to treat as transparent
// halfOffset: 0 for top half (rows 0-7), 8 for bottom half (rows 8-15)
// ---------------------------------------------------------------------------
void Text2D::applyColorKey(const uint8_t* src, int srcSize, int glyphSrc,
                            uint8_t* atlas, int atlasDst, int glyphDst,
                            uint8_t keyR, uint8_t keyG, uint8_t keyB,
                            int halfOffset)
{
    const int scale = glyphSrc / glyphDst;  // 1, 2, 4, 8...
    constexpr int kKeyThresh = 400;         // color distance squared threshold

    for (int asciiCode = 0; asciiCode < 128; ++asciiCode)
    {
        // Source glyph: column from ASCII code, row shifted by halfOffset
        int srcCol = asciiCode % 16;
        int srcRow = (asciiCode / 16) + halfOffset;
        int srcX   = srcCol * glyphSrc;
        int srcY   = srcRow * glyphSrc;

        // Destination glyph in our 128×128 atlas (always ASCII-ordered)
        int dstCol = asciiCode % 16;
        int dstRow = asciiCode / 16;
        int dstX   = dstCol * glyphDst;
        int dstY   = dstRow * glyphDst;

        if (scale <= 1)
        {
            // 1:1 direct copy
            for (int py = 0; py < glyphDst; ++py)
            for (int px = 0; px < glyphDst; ++px)
            {
                int si = ((srcY + py) * srcSize + (srcX + px)) * 4;
                int di = ((dstY + py) * atlasDst + (dstX + px)) * 4;

                uint8_t r = src[si+0], g = src[si+1], b = src[si+2];
                int dr = r-keyR, dg = g-keyG, db = b-keyB;
                bool isKey = (dr*dr + dg*dg + db*db) < kKeyThresh;

                atlas[di+0] = isKey ? 0 : r;
                atlas[di+1] = isKey ? 0 : g;
                atlas[di+2] = isKey ? 0 : b;
                atlas[di+3] = isKey ? 0 : 255;
            }
        }
        else
        {
            // N:1 box filter downsample
            // For each dst pixel, average a scale×scale block of src pixels.
            // Only non-key pixels contribute to the average, so background
            // color does not bleed into glyph edges.
            // Alpha is proportional to foreground pixel count, giving natural
            // antialiasing at glyph boundaries after downsampling.
            for (int py = 0; py < glyphDst; ++py)
            for (int px = 0; px < glyphDst; ++px)
            {
                int rSum=0, gSum=0, bSum=0, count=0;

                for (int sy = 0; sy < scale; ++sy)
                for (int sx = 0; sx < scale; ++sx)
                {
                    int si = ((srcY + py*scale + sy) * srcSize +
                               (srcX + px*scale + sx)) * 4;

                    uint8_t sr = src[si+0], sg = src[si+1], sb = src[si+2];
                    int dr = sr-keyR, dg = sg-keyG, db = sb-keyB;
                    bool isKey = (dr*dr + dg*dg + db*db) < kKeyThresh;

                    if (!isKey) { rSum+=sr; gSum+=sg; bSum+=sb; ++count; }
                }

                int di = ((dstY + py) * atlasDst + (dstX + px)) * 4;

                if (count == 0)
                {
                    atlas[di+0] = atlas[di+1] = atlas[di+2] = atlas[di+3] = 0;
                }
                else
                {
                    atlas[di+0] = (uint8_t)(rSum / count);
                    atlas[di+1] = (uint8_t)(gSum / count);
                    atlas[di+2] = (uint8_t)(bSum / count);
                    // Proportional alpha = natural antialiasing for free
                    atlas[di+3] = (uint8_t)(count * 255 / (scale * scale));
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// init
// ---------------------------------------------------------------------------
void Text2D::init()
{
    if (s_initialized) return;
    s_initialized = true;

    GLint savedVAO = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &savedVAO);

    // Build and upload built-in font as normal atlas
    uint8_t atlas[128 * 128 * 4];
    buildBuiltinAtlasBuffer(atlas);
    uploadAtlasToGPU(s_fontTex, atlas, 128, 128);
    s_fontTexAlt       = 0;  // no alternate set for built-in font
    s_usingExternalFont = false;

    s_textProg = compileProg(kVSText, kFSText);
    s_fillProg = compileProg(kVSFill, kFSFill);

    // Shared VBO
    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(kMaxQuads * kFloatsPerQuad * sizeof(float)),
                 nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Text VAO
    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    setupVAOAttribs(s_vbo);
    glBindVertexArray(0);

    // Fill VAO
    glGenVertexArrays(1, &s_fillVao);
    glBindVertexArray(s_fillVao);
    setupVAOAttribs(s_vbo);
    glBindVertexArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (savedVAO) glBindVertexArray(savedVAO);

    fprintf(stderr, "[Text2D] init OK — fontTex=%u (builtin IBM CP437)\n", s_fontTex);
}

// ---------------------------------------------------------------------------
// tryLoadQ2Conchars
// Loads BOTH the normal set (top half) and alternate set (bottom half).
// Accepts any square image whose width is a multiple of 16.
// ---------------------------------------------------------------------------
bool Text2D::tryLoadQ2Conchars(AssetFS* assets, const char* logicalPath)
{
    if (!s_initialized) init();
    if (!assets || !logicalPath) return false;

    std::vector<uint8_t> bytes;
    if (!assets->readAllBytes(logicalPath, bytes))
    {
        fprintf(stdout, "[Text2D] conchars not found: '%s'\n", logicalPath);
        return false;
    }

    ImageRGBA8 img;
    std::string err;
    if (!loadImageRGBA8FromMemory(bytes.data(), bytes.size(), img, &err))
    {
        fprintf(stderr, "[Text2D] failed to decode conchars '%s': %s\n",
                logicalPath, err.c_str());
        return false;
    }

    // Must be square and width divisible by 16
    if (img.width != img.height || (img.width % 16) != 0)
    {
        fprintf(stderr, "[Text2D] conchars '%s': must be square and divisible "
                "by 16, got %d×%d\n", logicalPath, img.width, img.height);
        return false;
    }

    const int srcSize  = img.width;
    const int glyphSrc = srcSize / 16;   // pixels per glyph in source
    const int glyphDst = 8;
    const int atlasDst = 128;

    // Color key: pixel (0,0) = background color → transparent
    const uint8_t keyR = img.rgba[0];
    const uint8_t keyG = img.rgba[1];
    const uint8_t keyB = img.rgba[2];

    fprintf(stdout, "[Text2D] loading conchars '%s' (%d×%d, %dpx glyphs, "
            "colorKey=%d,%d,%d)\n",
            logicalPath, srcSize, srcSize, glyphSrc, keyR, keyG, keyB);

    // --- Build normal atlas (top half: rows 0-7 = halfOffset 0) ---
    std::vector<uint8_t> normalAtlas(atlasDst * atlasDst * 4, 0);
    applyColorKey(img.rgba.data(), srcSize, glyphSrc,
                  normalAtlas.data(), atlasDst, glyphDst,
                  keyR, keyG, keyB,
                  0);  // halfOffset=0 → source rows 0-7
    uploadAtlasToGPU(s_fontTex, normalAtlas.data(), atlasDst, atlasDst);

    // --- Build alternate atlas (bottom half: rows 8-15 = halfOffset 8) ---
    // Only if the image is tall enough to have a bottom half
    if (srcSize >= glyphSrc * 16)  // 16 rows exist
    {
        std::vector<uint8_t> altAtlas(atlasDst * atlasDst * 4, 0);
        applyColorKey(img.rgba.data(), srcSize, glyphSrc,
                      altAtlas.data(), atlasDst, glyphDst,
                      keyR, keyG, keyB,
                      8);  // halfOffset=8 → source rows 8-15
        uploadAtlasToGPU(s_fontTexAlt, altAtlas.data(), atlasDst, atlasDst);
        fprintf(stdout, "[Text2D] alternate (gold) set loaded: texId=%u\n",
                s_fontTexAlt);
    }
    else
    {
        // Single-half image — no alternate set
        if (s_fontTexAlt) { glDeleteTextures(1, &s_fontTexAlt); s_fontTexAlt = 0; }
        fprintf(stdout, "[Text2D] no alternate set (image only has top half)\n");
    }

    s_usingExternalFont = true;
    fprintf(stdout, "[Text2D] conchars loaded OK. normalTex=%u altTex=%u\n",
            s_fontTex, s_fontTexAlt);
    return true;
}

// ---------------------------------------------------------------------------
// tryLoadFont — generic 128×128 single-set bitmap font
// ---------------------------------------------------------------------------
bool Text2D::tryLoadFont(AssetFS* assets, const char* logicalPath)
{
    if (!s_initialized) init();
    if (!assets || !logicalPath) return false;

    std::vector<uint8_t> bytes;
    if (!assets->readAllBytes(logicalPath, bytes))
    {
        fprintf(stdout, "[Text2D] font not found: '%s'\n", logicalPath);
        return false;
    }

    ImageRGBA8 img;
    std::string err;
    if (!loadImageRGBA8FromMemory(bytes.data(), bytes.size(), img, &err))
    {
        fprintf(stderr, "[Text2D] failed to decode font '%s': %s\n",
                logicalPath, err.c_str());
        return false;
    }

    if (img.width != 128 || img.height != 128)
    {
        fprintf(stderr, "[Text2D] font '%s' must be 128×128, got %d×%d\n",
                logicalPath, img.width, img.height);
        return false;
    }

    return loadFontFromPixels(img.rgba.data(), img.width, img.height);
}

// ---------------------------------------------------------------------------
// loadFontFromPixels
// ---------------------------------------------------------------------------
bool Text2D::loadFontFromPixels(const uint8_t* rgba, int width, int height,
                                 const uint8_t* altRgba)
{
    if (!rgba || width != 128 || height != 128) return false;

    // Color key from top-left pixel
    const uint8_t keyR = rgba[0], keyG = rgba[1], keyB = rgba[2];
    constexpr int kKeyThresh = 400;

    std::vector<uint8_t> fixed(128 * 128 * 4);
    for (int i = 0; i < 128 * 128; ++i)
    {
        uint8_t r = rgba[i*4+0], g = rgba[i*4+1], b = rgba[i*4+2], a = rgba[i*4+3];
        bool hasRealAlpha = (a < 250 && a > 5);
        if (hasRealAlpha)
        {
            fixed[i*4+0]=r; fixed[i*4+1]=g; fixed[i*4+2]=b; fixed[i*4+3]=a;
        }
        else
        {
            int dr=r-keyR, dg=g-keyG, db=b-keyB;
            bool isKey = (dr*dr+dg*dg+db*db) < kKeyThresh;
            fixed[i*4+0] = isKey?0:r;
            fixed[i*4+1] = isKey?0:g;
            fixed[i*4+2] = isKey?0:b;
            fixed[i*4+3] = isKey?0:255;
        }
    }

    uploadAtlasToGPU(s_fontTex, fixed.data(), 128, 128);

    if (altRgba)
        loadFontFromPixels(altRgba, 128, 128);  // recursively loads alt set
    else
    {
        if (s_fontTexAlt) { glDeleteTextures(1, &s_fontTexAlt); s_fontTexAlt = 0; }
    }

    s_usingExternalFont = true;
    return true;
}

// ---------------------------------------------------------------------------
// resetToBuiltinFont
// ---------------------------------------------------------------------------
void Text2D::resetToBuiltinFont()
{
    uint8_t atlas[128 * 128 * 4];
    buildBuiltinAtlasBuffer(atlas);
    uploadAtlasToGPU(s_fontTex, atlas, 128, 128);

    if (s_fontTexAlt) { glDeleteTextures(1, &s_fontTexAlt); s_fontTexAlt = 0; }

    s_usingExternalFont = false;
    fprintf(stdout, "[Text2D] reset to built-in IBM CP437 font\n");
}

// ---------------------------------------------------------------------------
// setScale
// ---------------------------------------------------------------------------
void Text2D::setScale(int scale)
{
    s_scale = (scale < 1) ? 1 : (scale > 4 ? 4 : scale);
}

// ---------------------------------------------------------------------------
// shutdown
// ---------------------------------------------------------------------------
void Text2D::shutdown()
{
    if (s_fontTex)    { glDeleteTextures(1, &s_fontTex);    s_fontTex    = 0; }
    if (s_fontTexAlt) { glDeleteTextures(1, &s_fontTexAlt); s_fontTexAlt = 0; }
    if (s_textProg)   { glDeleteProgram(s_textProg);         s_textProg   = 0; }
    if (s_fillProg)   { glDeleteProgram(s_fillProg);         s_fillProg   = 0; }
    if (s_vbo)        { glDeleteBuffers(1, &s_vbo);          s_vbo        = 0; }
    if (s_vao)        { glDeleteVertexArrays(1, &s_vao);     s_vao        = 0; }
    if (s_fillVao)    { glDeleteVertexArrays(1, &s_fillVao); s_fillVao    = 0; }
    s_initialized = false;
}

// ---------------------------------------------------------------------------
// begin / end
// ---------------------------------------------------------------------------
void Text2D::begin(int screenW, int screenH)
{
    s_inFrame     = true;
    s_screenW     = screenW;
    s_screenH     = screenH;
    s_vertexCount = 0;
    s_inFillMode  = false;
    s_currentFontSet = FontSet::Normal;
}

void Text2D::end()
{
    if (!s_inFrame) return;
    if (s_vertexCount > 0)
    {
        if (s_inFillMode) flushFill();
        else              flush();
    }
    s_inFrame = false;
}

// ---------------------------------------------------------------------------
// emitQuad / emitFillQuad
// ---------------------------------------------------------------------------
void Text2D::emitQuad(float x, float y, float w, float h,
                       float u0, float v0, float u1, float v1,
                       float r, float g, float b, float a)
{
    if (s_inFillMode && s_vertexCount > 0) { flushFill(); }
    s_inFillMode = false;
    if (s_vertexCount + kVertsPerQuad > kMaxQuads * kVertsPerQuad) flush();

    float x1 = x+w, y1 = y+h;
    float* p = s_vbuf + s_vertexCount * kFloatsPerVertex;
    *p++=x;  *p++=y;  *p++=u0; *p++=v0; *p++=r; *p++=g; *p++=b; *p++=a;
    *p++=x1; *p++=y;  *p++=u1; *p++=v0; *p++=r; *p++=g; *p++=b; *p++=a;
    *p++=x;  *p++=y1; *p++=u0; *p++=v1; *p++=r; *p++=g; *p++=b; *p++=a;
    *p++=x1; *p++=y;  *p++=u1; *p++=v0; *p++=r; *p++=g; *p++=b; *p++=a;
    *p++=x1; *p++=y1; *p++=u1; *p++=v1; *p++=r; *p++=g; *p++=b; *p++=a;
    *p++=x;  *p++=y1; *p++=u0; *p++=v1; *p++=r; *p++=g; *p++=b; *p++=a;
    s_vertexCount += kVertsPerQuad;
}

void Text2D::emitFillQuad(float x, float y, float w, float h,
                            float r0, float g0, float b0, float a0,
                            float r1, float g1, float b1, float a1)
{
    if (!s_inFillMode && s_vertexCount > 0) { flush(); }
    s_inFillMode = true;
    if (s_vertexCount + kVertsPerQuad > kMaxQuads * kVertsPerQuad) flushFill();

    float x1 = x+w, y1 = y+h;
    float* p = s_vbuf + s_vertexCount * kFloatsPerVertex;
    *p++=x;  *p++=y;  *p++=0; *p++=0; *p++=r0; *p++=g0; *p++=b0; *p++=a0;
    *p++=x1; *p++=y;  *p++=0; *p++=0; *p++=r1; *p++=g1; *p++=b1; *p++=a1;
    *p++=x;  *p++=y1; *p++=0; *p++=0; *p++=r0; *p++=g0; *p++=b0; *p++=a0;
    *p++=x1; *p++=y;  *p++=0; *p++=0; *p++=r1; *p++=g1; *p++=b1; *p++=a1;
    *p++=x1; *p++=y1; *p++=0; *p++=0; *p++=r1; *p++=g1; *p++=b1; *p++=a1;
    *p++=x;  *p++=y1; *p++=0; *p++=0; *p++=r0; *p++=g0; *p++=b0; *p++=a0;
    s_vertexCount += kVertsPerQuad;
}

// ---------------------------------------------------------------------------
// bindFontTex — switch atlas mid-frame (flushes pending quads first)
// ---------------------------------------------------------------------------
void Text2D::bindFontTex(FontSet set)
{
    if (set == s_currentFontSet) return;
    // Flush pending quads before switching texture
    if (s_vertexCount > 0 && !s_inFillMode) flush();
    s_currentFontSet = set;
}

// ---------------------------------------------------------------------------
// flush — draw pending text quads
// THE KEY FIX: glBindSampler(0,0) detaches BSP lightmap sampler before draw
// ---------------------------------------------------------------------------
void Text2D::flush()
{
    if (s_vertexCount == 0) return;

    GL2DState st; saveGL(st); set2DState();

    glUseProgram(s_textProg);
    GLint locScreen = glGetUniformLocation(s_textProg, "uScreenSize");
    GLint locTex    = glGetUniformLocation(s_textProg, "uFontTex");
    if (locScreen >= 0) glUniform2f(locScreen, (float)s_screenW, (float)s_screenH);
    if (locTex    >= 0) glUniform1i(locTex, 0);

    glActiveTexture(GL_TEXTURE0);
    // Choose atlas based on current font set
    uint32_t tex = (s_currentFontSet == FontSet::Alternate && s_fontTexAlt)
                   ? s_fontTexAlt : s_fontTex;
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindSampler(0, 0);  // CRITICAL: detach BSP mipmap sampler

    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    setupVAOAttribs(s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(s_vertexCount * kFloatsPerVertex * sizeof(float)),
                    s_vbuf);
    glDrawArrays(GL_TRIANGLES, 0, s_vertexCount);

    restoreGL(st);
    s_vertexCount = 0;
    s_inFillMode  = false;
}

// ---------------------------------------------------------------------------
// flushFill
// ---------------------------------------------------------------------------
void Text2D::flushFill()
{
    if (s_vertexCount == 0) return;

    GL2DState st; saveGL(st); set2DState();

    glUseProgram(s_fillProg);
    GLint locScreen = glGetUniformLocation(s_fillProg, "uScreenSize");
    if (locScreen >= 0) glUniform2f(locScreen, (float)s_screenW, (float)s_screenH);

    glBindVertexArray(s_fillVao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    setupVAOAttribs(s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(s_vertexCount * kFloatsPerVertex * sizeof(float)),
                    s_vbuf);
    glDrawArrays(GL_TRIANGLES, 0, s_vertexCount);

    restoreGL(st);
    s_vertexCount = 0;
    s_inFillMode  = false;
}

// ---------------------------------------------------------------------------
// drawFill / drawFillGradientH
// ---------------------------------------------------------------------------
void Text2D::drawFill(int x, int y, int w, int h, Vec4 c)
{
    emitFillQuad((float)x,(float)y,(float)w,(float)h,
                 c.x,c.y,c.z,c.w, c.x,c.y,c.z,c.w);
}

void Text2D::drawFillGradientH(int x, int y, int w, int h, Vec4 cL, Vec4 cR)
{
    emitFillQuad((float)x,(float)y,(float)w,(float)h,
                 cL.x,cL.y,cL.z,cL.w, cR.x,cR.y,cR.z,cR.w);
}

// ---------------------------------------------------------------------------
// drawCharInternal — core glyph emit
// ---------------------------------------------------------------------------
void Text2D::drawCharInternal(int x, int y, int charCode, Vec4 color, FontSet set)
{
    if (charCode < 0 || charCode > 127) charCode = 127;

    // Switch atlas if needed (flushes if necessary)
    bindFontTex(set);

    int gx = charCode % 16;
    int gy = charCode / 16;

    // 0.5px sub-texel inset to prevent bilinear bleed from adjacent glyphs
    constexpr float kAtlas = 128.0f;
    constexpr float kInset = 0.5f / kAtlas;
    constexpr float kStep  = 8.0f  / kAtlas;

    float u0 = gx * kStep + kInset;
    float v0 = gy * kStep + kInset;
    float u1 = u0 + kStep - kInset * 2.0f;
    float v1 = v0 + kStep - kInset * 2.0f;

    float fw = (float)(kGlyphW * s_scale);
    float fh = (float)(kGlyphH * s_scale);

    emitQuad((float)x, (float)y, fw, fh, u0, v0, u1, v1,
             color.x, color.y, color.z, color.w);
}

// ---------------------------------------------------------------------------
// Public draw functions — normal set
// ---------------------------------------------------------------------------
void Text2D::drawChar(int x, int y, int charCode, Vec4 color)
{
    drawCharInternal(x, y, charCode, color, FontSet::Normal);
}

void Text2D::drawString(int x, int y, const char* text, Vec4 color)
{
    if (!text) return;
    int cx = x;
    while (*text)
    {
        unsigned char c = (unsigned char)*text++;
        if (c >= 128) c = '?';
        drawCharInternal(cx, y, c, color, FontSet::Normal);
        cx += charWidth();
    }
}

void Text2D::drawStringShadow(int x, int y, const char* text, Vec4 color,
                               Vec4 shadowColor, int ox, int oy)
{
    drawString(x+ox, y+oy, text, shadowColor);
    if (s_vertexCount > 0) flush();
    drawString(x, y, text, color);
}

// ---------------------------------------------------------------------------
// Public draw functions — alternate set (gold/brown in Q2 conchars)
// Falls back to normal set if no alternate atlas was loaded.
// ---------------------------------------------------------------------------
void Text2D::drawCharAlt(int x, int y, int charCode, Vec4 color)
{
    FontSet set = s_fontTexAlt ? FontSet::Alternate : FontSet::Normal;
    drawCharInternal(x, y, charCode, color, set);
}

void Text2D::drawStringAlt(int x, int y, const char* text, Vec4 color)
{
    if (!text) return;
    FontSet set = s_fontTexAlt ? FontSet::Alternate : FontSet::Normal;
    int cx = x;
    while (*text)
    {
        unsigned char c = (unsigned char)*text++;
        if (c >= 128) c = '?';
        drawCharInternal(cx, y, c, color, set);
        cx += charWidth();
    }
}

void Text2D::drawStringShadowAlt(int x, int y, const char* text, Vec4 color,
                                  Vec4 shadowColor, int ox, int oy)
{
    drawStringAlt(x+ox, y+oy, text, shadowColor);
    if (s_vertexCount > 0) flush();
    drawStringAlt(x, y, text, color);
}

// ---------------------------------------------------------------------------
// stringWidth
// ---------------------------------------------------------------------------
int Text2D::stringWidth(const char* text)
{
    int w = 0;
    while (text && *text++) w += charWidth();
    return w;
}

} // namespace nova