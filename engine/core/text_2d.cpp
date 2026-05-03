// ============================================================
// FILE:    engine/core/text_2d.cpp
// MODULE:  Core > 2D Text Overlay
// VERSION: v5 — External bitmap font support (Q2-style)
//
// FONT LOADING:
//   External: 128×128 RGBA image, 16×16 glyph grid, 8×8 per glyph.
//             Alpha channel = glyph mask. If no alpha, uses luminance.
//             Compatible with Q2's conchars.pcx format.
//   Builtin:  IBM CP437 8×8 hardcoded bitmap (always available).
//
// SAMPLER BUG FIX (carried from v4):
//   glBindSampler(0, 0) before drawing text to detach any sampler
//   object left by BSP lightmap rendering. Without this, the
//   mipmap sampler makes the font atlas return (0,0,0,0).
// ============================================================

#include "engine/core/text_2d.h"
#include "engine/core/image_load.h"

#include <glad/glad.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

namespace nova
{

// ---------------------------------------------------------------------------
// Static storage
// ---------------------------------------------------------------------------
uint32_t Text2D::s_fontTex      = 0;
uint32_t Text2D::s_textProg     = 0;
uint32_t Text2D::s_fillProg     = 0;
uint32_t Text2D::s_vbo          = 0;
uint32_t Text2D::s_vao          = 0;
uint32_t Text2D::s_fillVao      = 0;
bool     Text2D::s_initialized  = false;
bool     Text2D::s_inFrame      = false;
int      Text2D::s_screenW      = 0;
int      Text2D::s_screenH      = 0;
int      Text2D::s_scale        = 2;
bool     Text2D::s_usingExternalFont = false;
float    Text2D::s_vbuf[kMaxQuads * kFloatsPerQuad];
int      Text2D::s_vertexCount  = 0;
bool     Text2D::s_inFillMode   = false;

// ---------------------------------------------------------------------------
// Built-in IBM CP437 8×8 font bitmap (ASCII 32–127)
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
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // DEL→block
};

// ---------------------------------------------------------------------------
// Build the built-in atlas into a 128×128 RGBA8 CPU buffer
// ---------------------------------------------------------------------------
static void buildBuiltinAtlasBuffer(uint8_t* out)  // out must be 128*128*4 bytes
{
    memset(out, 0, 128 * 128 * 4);
    for (int code = 0; code < 128; ++code)
    {
        int gx = code % 16, gy = code / 16;
        int ax = gx * 8, ay = gy * 8;
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
                    out[idx+3] = v;  // alpha = luminance
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Shaders (same as v4)
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
    if (mask < 0.1) discard;
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
// GL helpers (identical to v4)
// ---------------------------------------------------------------------------
static uint32_t compileProg(const char* vs, const char* fs)
{
    GLuint prog = glCreateProgram();
    char log[512]; GLint ok;
    auto stage = [&](GLenum type, const char* src)
    {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) { glGetShaderInfoLog(sh, 512, nullptr, log);
                   fprintf(stderr, "Text2D shader: %s\n", log); }
        glAttachShader(prog, sh);
        glDeleteShader(sh);
    };
    stage(GL_VERTEX_SHADER, vs);
    stage(GL_FRAGMENT_SHADER, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { glGetProgramInfoLog(prog, 512, nullptr, log);
               fprintf(stderr, "Text2D link: %s\n", log); }
    return prog;
}

struct GL2DState
{
    GLint prog, vao, vbo, texUnit, tex2D_unit0, sampler_unit0;
    GLboolean depth, cull, blend, depthMask;
    GLint blendSrcRGB, blendDstRGB, blendSrcA, blendDstA, depthFunc;
    GLint clipOrigin, clipDepth;
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
    s.depth = glIsEnabled(GL_DEPTH_TEST);
    s.cull  = glIsEnabled(GL_CULL_FACE);
    s.blend = glIsEnabled(GL_BLEND);
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
    glBindSampler(0, (GLuint)s.sampler_unit0);
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
// uploadAtlasToGPU — creates/replaces s_fontTex from a 128×128 RGBA8 buffer
// ---------------------------------------------------------------------------
void Text2D::uploadAtlasToGPU(const uint8_t* rgba8, int w, int h)
{
    if (s_fontTex) { glDeleteTextures(1, &s_fontTex); s_fontTex = 0; }

    glGenTextures(1, &s_fontTex);
    glBindTexture(GL_TEXTURE_2D, s_fontTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba8);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,  0);
    glBindTexture(GL_TEXTURE_2D, 0);
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

    // Upload built-in font
    uint8_t atlas[128 * 128 * 4];
    buildBuiltinAtlasBuffer(atlas);
    uploadAtlasToGPU(atlas, 128, 128);
    s_usingExternalFont = false;

    s_textProg = compileProg(kVSText, kFSText);
    s_fillProg = compileProg(kVSFill, kFSFill);

    glGenBuffers(1, &s_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 (GLsizeiptr)(kMaxQuads * kFloatsPerQuad * sizeof(float)),
                 nullptr, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenVertexArrays(1, &s_vao);
    glBindVertexArray(s_vao);
    setupVAOAttribs(s_vbo);
    glBindVertexArray(0);

    glGenVertexArrays(1, &s_fillVao);
    glBindVertexArray(s_fillVao);
    setupVAOAttribs(s_vbo);
    glBindVertexArray(0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    if (savedVAO) glBindVertexArray(savedVAO);

    fprintf(stderr, "[Text2D] init: fontTex=%u (builtin)\n", s_fontTex);
}

// ---------------------------------------------------------------------------
// tryLoadFont — Q2-style external font
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
        fprintf(stderr, "[Text2D] failed to decode font '%s': %s\n", logicalPath, err.c_str());
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
bool Text2D::loadFontFromPixels(const uint8_t* rgba, int width, int height)
{
    if (!rgba || width != 128 || height != 128) return false;

    // Q2's conchars has a green background (color key 0x00,0x00,0x00 or similar).
    // We convert: if a pixel is the "transparent" color (very dark green in Q2),
    // set alpha=0; otherwise alpha=255.
    // Strategy: use the top-left pixel (0,0) as the color key for transparency.
    // This handles both Q2 (green BG) and custom fonts (black BG).

    std::vector<uint8_t> fixed(128 * 128 * 4);

    // Sample the color key from pixel (0,0) — should be the background color
    const uint8_t keyR = rgba[0];
    const uint8_t keyG = rgba[1];
    const uint8_t keyB = rgba[2];

    for (int i = 0; i < 128 * 128; ++i)
    {
        uint8_t r = rgba[i*4+0];
        uint8_t g = rgba[i*4+1];
        uint8_t b = rgba[i*4+2];
        uint8_t a = rgba[i*4+3];

        // If the source already has a proper alpha channel, use it directly
        // (non-trivial alpha = the image was authored with transparency)
        bool hasRealAlpha = (a < 250 && a > 5);

        if (hasRealAlpha)
        {
            fixed[i*4+0] = r;
            fixed[i*4+1] = g;
            fixed[i*4+2] = b;
            fixed[i*4+3] = a;
        }
        else
        {
            // Color key transparency: background color → alpha 0
            int dr = (int)r - keyR;
            int dg = (int)g - keyG;
            int db = (int)b - keyB;
            int dist = dr*dr + dg*dg + db*db;
            if (dist < 400) // within ~20 units of key color
            {
                fixed[i*4+0] = fixed[i*4+1] = fixed[i*4+2] = fixed[i*4+3] = 0;
            }
            else
            {
                fixed[i*4+0] = r;
                fixed[i*4+1] = g;
                fixed[i*4+2] = b;
                fixed[i*4+3] = 255;
            }
        }
    }

    uploadAtlasToGPU(fixed.data(), 128, 128);
    s_usingExternalFont = true;

    fprintf(stdout, "[Text2D] external font loaded (colorKey=%d,%d,%d)\n",
            keyR, keyG, keyB);
    return true;
}

// ---------------------------------------------------------------------------
// resetToBuiltinFont
// ---------------------------------------------------------------------------
void Text2D::resetToBuiltinFont()
{
    uint8_t atlas[128 * 128 * 4];
    buildBuiltinAtlasBuffer(atlas);
    uploadAtlasToGPU(atlas, 128, 128);
    s_usingExternalFont = false;
    fprintf(stdout, "[Text2D] reset to builtin font\n");
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
    if (s_fontTex)  { glDeleteTextures(1, &s_fontTex);    s_fontTex = 0; }
    if (s_textProg) { glDeleteProgram(s_textProg);         s_textProg = 0; }
    if (s_fillProg) { glDeleteProgram(s_fillProg);         s_fillProg = 0; }
    if (s_vbo)      { glDeleteBuffers(1, &s_vbo);          s_vbo = 0; }
    if (s_vao)      { glDeleteVertexArrays(1, &s_vao);     s_vao = 0; }
    if (s_fillVao)  { glDeleteVertexArrays(1, &s_fillVao); s_fillVao = 0; }
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
// emitQuad / emitFillQuad (unchanged from v4)
// ---------------------------------------------------------------------------
void Text2D::emitQuad(float x, float y, float w, float h,
                       float u0, float v0, float u1, float v1,
                       float r, float g, float b, float a)
{
    if (s_inFillMode && s_vertexCount > 0) { flushFill(); }
    s_inFillMode = false;
    if (s_vertexCount + kVertsPerQuad > kMaxQuads * kVertsPerQuad) flush();

    float x1 = x + w, y1 = y + h;
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

    float x1 = x + w, y1 = y + h;
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
// flush / flushFill (sampler detach fix carried from v4)
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
    glBindTexture(GL_TEXTURE_2D, s_fontTex);
    glBindSampler(0, 0);  // detach BSP lightmap sampler — critical

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
// drawChar — picks an 8×8 block from the 128×128 atlas (16×16 grid)
// ---------------------------------------------------------------------------
void Text2D::drawChar(int x, int y, int charCode, Vec4 color)
{
    if (charCode < 0 || charCode > 127) charCode = 127;
    int gx = charCode % 16, gy = charCode / 16;

    // Sub-texel inset (0.5px) prevents bilinear bleed from adjacent glyphs
    constexpr float kAtlas = 128.0f;
    constexpr float kInset = 0.5f / kAtlas;
    constexpr float kStep  = 8.0f  / kAtlas;

    float u0 = gx * kStep + kInset;
    float v0 = gy * kStep + kInset;
    float u1 = u0 + kStep - kInset * 2.0f;
    float v1 = v0 + kStep - kInset * 2.0f;

    float fw = (float)(kGlyphW * s_scale);
    float fh = (float)(kGlyphH * s_scale);
    emitQuad((float)x,(float)y, fw, fh, u0,v0,u1,v1,
             color.x, color.y, color.z, color.w);
}

// ---------------------------------------------------------------------------
// drawString / drawStringShadow / stringWidth
// ---------------------------------------------------------------------------
void Text2D::drawString(int x, int y, const char* text, Vec4 color)
{
    if (!text) return;
    int cx = x;
    while (*text)
    {
        unsigned char c = (unsigned char)*text++;
        if (c >= 128) c = '?';
        drawChar(cx, y, c, color);
        cx += charWidth();
    }
}

void Text2D::drawStringShadow(int x, int y, const char* text, Vec4 color,
                               Vec4 shadowColor, int ox, int oy)
{
    drawString(x + ox, y + oy, text, shadowColor);
    if (s_vertexCount > 0) { flush(); }
    drawString(x, y, text, color);
}

int Text2D::stringWidth(const char* text)
{
    int w = 0;
    while (text && *text++) w += charWidth();
    return w;
}

} // namespace nova