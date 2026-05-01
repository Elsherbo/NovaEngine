// ============================================================
// FILE:    engine/core/text_2d.cpp
// MODULE:  Core > 2D Text Overlay — Quake2-style
// STATUS:  FIXED
//
// FIX LOG:
//   1. kFloatsPerVertex = 8: layout is x,y,u,v,r,g,b,a.
//      Old layout had 7 floats; the write loop wrote alpha at
//      index 6 which clobbered the blue channel. The color
//      glVertexAttribPointer(count=4, offset=16) then read
//      r,g,a,[next_vertex.x] as RGBA — vColor.a was garbage.
//
//   2. Dedicated VAO (s_vao): created in init(), attribs are
//      wired once. flush() simply binds s_vao instead of
//      calling glBindVertexArray(0) which is invalid in GL 4.5
//      core profile and silently breaks all attrib setup.
//
//   3. emitQuad: correctly stores all 8 floats in one shot
//      using a flat array write — no separate alpha write that
//      could mis-index.
//
//   4. flush(): saves/restores GL state properly using DSA-
//      compatible queries; does NOT touch the BSP global VAO.
// ============================================================

#include "engine/core/text_2d.h"

#include <glad/glad.h>
#include <cstdio>
#include <cstring>

namespace nova
{

// ---- Static storage ----
uint32_t Text2D::s_fontTex    = 0;
uint32_t Text2D::s_prog       = 0;
uint32_t Text2D::s_vbo        = 0;
uint32_t Text2D::s_vao        = 0;      // FIX: dedicated VAO
bool     Text2D::s_initialized = false;
bool     Text2D::s_inFrame     = false;
int      Text2D::s_screenW     = 0;
int      Text2D::s_screenH     = 0;
float    Text2D::s_vbuf[kMaxQuads * kFloatsPerQuad];
int      Text2D::s_vertexCount = 0;

// ============================================================
// 8x8 bitmap font — ASCII 32-127 (96 printable chars, 8 bytes each).
// Each byte encodes one row of 8 pixels, MSB = leftmost pixel.
// ============================================================
static const uint8_t kFont8x8[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00, // ' '  32
    0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00, // '!'  33
    0x6C,0x6C,0x00,0x00,0x00,0x00,0x00,0x00, // '"'  34
    0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00, // '#'  35
    0x18,0x7E,0xC0,0x7C,0x06,0xFC,0x18,0x00, // '$'  36
    0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00, // '%'  37
    0x38,0x6C,0x38,0x76,0xCC,0xCC,0x76,0x00, // '&'  38
    0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00, // '\'' 39
    0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00, // '('  40
    0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00, // ')'  41
    0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00, // '*'  42
    0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00, // '+'  43
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30, // ','  44
    0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00, // '-'  45
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00, // '.'  46
    0x00,0x06,0x0C,0x18,0x30,0x60,0xC0,0x00, // '/'  47
    0x38,0x6C,0xC6,0xC6,0xD6,0x6C,0x38,0x00, // '0'  48
    0x18,0x38,0x78,0x18,0x18,0x18,0x7E,0x00, // '1'  49
    0x7C,0xC6,0x06,0x3C,0x60,0xC6,0xFE,0x00, // '2'  50
    0x7C,0xC6,0x06,0x3C,0x06,0xC6,0x7C,0x00, // '3'  51
    0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x1E,0x00, // '4'  52
    0xFE,0xC0,0xFC,0x06,0x06,0xC6,0x7C,0x00, // '5'  53
    0x38,0x60,0xC0,0xFC,0xC6,0xC6,0x7C,0x00, // '6'  54
    0xFE,0xC6,0x0C,0x18,0x30,0x30,0x30,0x00, // '7'  55
    0x7C,0xC6,0xC6,0x7C,0xC6,0xC6,0x7C,0x00, // '8'  56
    0x7C,0xC6,0xC6,0x7E,0x06,0x0C,0x78,0x00, // '9'  57
    0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00, // ':'  58
    0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00, // ';'  59
    0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00, // '<'  60
    0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00, // '='  61
    0x60,0x30,0x18,0x0C,0x18,0x30,0x60,0x00, // '>'  62
    0x7C,0xC6,0x0C,0x18,0x18,0x00,0x18,0x00, // '?'  63
    0x38,0x6C,0xDE,0xDE,0xC0,0x78,0x00,0x00, // '@'  64
    0x38,0x6C,0xC6,0xFE,0xC6,0xC6,0xC6,0x00, // 'A'  65
    0xFC,0x66,0x66,0x7C,0x66,0x66,0xFC,0x00, // 'B'  66
    0x7C,0xC6,0xC0,0xC0,0xC0,0xC6,0x7C,0x00, // 'C'  67
    0xF8,0x6C,0x66,0x66,0x66,0x6C,0xF8,0x00, // 'D'  68
    0xFE,0x62,0x60,0x78,0x60,0x62,0xFE,0x00, // 'E'  69
    0xFE,0x62,0x60,0x78,0x60,0x60,0xF0,0x00, // 'F'  70
    0x7C,0xC6,0xC0,0xCE,0xC6,0xC6,0x7E,0x00, // 'G'  71
    0xC6,0xC6,0xC6,0xFE,0xC6,0xC6,0xC6,0x00, // 'H'  72
    0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00, // 'I'  73
    0x1E,0x0C,0x0C,0x0C,0xCC,0xCC,0x78,0x00, // 'J'  74
    0xE6,0x66,0x6C,0x78,0x6C,0x66,0xE6,0x00, // 'K'  75
    0xF0,0x60,0x60,0x60,0x62,0x66,0xFE,0x00, // 'L'  76
    0xC6,0xEE,0xFE,0xD6,0xC6,0xC6,0xC6,0x00, // 'M'  77
    0xC6,0xE6,0xF6,0xDE,0xCE,0xC6,0xC6,0x00, // 'N'  78
    0x7C,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00, // 'O'  79
    0xFC,0x66,0x66,0x7C,0x60,0x60,0xF0,0x00, // 'P'  80
    0x7C,0xC6,0xC6,0xC6,0xD6,0x6C,0x76,0x00, // 'Q'  81
    0xFC,0x66,0x66,0x7C,0x6C,0x66,0xE6,0x00, // 'R'  82
    0x7C,0xC6,0x60,0x38,0x0C,0xC6,0x7C,0x00, // 'S'  83
    0x7E,0x7E,0x5A,0x18,0x18,0x18,0x3C,0x00, // 'T'  84
    0xC6,0xC6,0xC6,0xC6,0xC6,0xC6,0x7C,0x00, // 'U'  85
    0xC6,0xC6,0xC6,0xC6,0xC6,0x6C,0x38,0x00, // 'V'  86
    0xC6,0xC6,0xC6,0xD6,0xFE,0xEE,0xC6,0x00, // 'W'  87
    0xC6,0xC6,0x6C,0x38,0x6C,0xC6,0xC6,0x00, // 'X'  88
    0x66,0x66,0x66,0x3C,0x18,0x18,0x3C,0x00, // 'Y'  89
    0xFE,0xC6,0x86,0x0C,0x18,0x30,0xFE,0x00, // 'Z'  90
    0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00, // '['  91
    0x00,0xC0,0x60,0x30,0x18,0x0C,0x06,0x00, // '\\' 92
    0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00, // ']'  93
    0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00, // '^'  94
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF, // '_'  95
    0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00, // '`'  96
    0x00,0x00,0x7C,0x0C,0x7C,0xCC,0x76,0x00, // 'a'  97
    0xE0,0x60,0x60,0x7C,0x66,0x66,0xDC,0x00, // 'b'  98
    0x00,0x00,0x7C,0xC6,0xC0,0xC6,0x7C,0x00, // 'c'  99
    0x1C,0x0C,0x0C,0x7C,0xCC,0xCC,0x76,0x00, // 'd' 100
    0x00,0x00,0x7C,0xC6,0xFE,0xC0,0x7C,0x00, // 'e' 101
    0x38,0x6C,0x60,0xF0,0x60,0x60,0xF0,0x00, // 'f' 102
    0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x78, // 'g' 103
    0xE0,0x60,0x60,0x6C,0x76,0x66,0xE6,0x00, // 'h' 104
    0x18,0x18,0x00,0x38,0x18,0x18,0x3C,0x00, // 'i' 105
    0x0C,0x0C,0x00,0x1C,0x0C,0xCC,0xCC,0x78, // 'j' 106
    0xE0,0x60,0x66,0x6C,0x78,0x6C,0xE6,0x00, // 'k' 107
    0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00, // 'l' 108
    0x00,0x00,0xEC,0xFE,0xD6,0xD6,0xC6,0x00, // 'm' 109
    0x00,0x00,0xDC,0x66,0x66,0x66,0x66,0x00, // 'n' 110
    0x00,0x00,0x7C,0xC6,0xC6,0xC6,0x7C,0x00, // 'o' 111
    0x00,0x00,0xDC,0x66,0x66,0x7C,0x60,0xF0, // 'p' 112
    0x00,0x00,0x76,0xCC,0xCC,0x7C,0x0C,0x1E, // 'q' 113
    0x00,0x00,0xDE,0x76,0x60,0x60,0xF0,0x00, // 'r' 114
    0x00,0x00,0x7E,0xC0,0x7C,0x06,0xFC,0x00, // 's' 115
    0x18,0x18,0x7E,0x18,0x18,0x18,0x0E,0x00, // 't' 116
    0x00,0x00,0xCC,0xCC,0xCC,0xCC,0x76,0x00, // 'u' 117
    0x00,0x00,0xC6,0xC6,0xC6,0x6C,0x38,0x00, // 'v' 118
    0x00,0x00,0xC6,0xD6,0xFE,0xEE,0xC6,0x00, // 'w' 119
    0x00,0x00,0xCC,0x6C,0x38,0x6C,0xCC,0x00, // 'x' 120
    0x00,0x00,0xCC,0xCC,0xCC,0x7C,0x0C,0xF8, // 'y' 121
    0x00,0x00,0xFE,0xCC,0x18,0x30,0xFE,0x00, // 'z' 122
    0x0C,0x18,0x18,0x70,0x18,0x18,0x0C,0x00, // '{' 123
    0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00, // '|' 124
    0x30,0x18,0x18,0x0E,0x18,0x18,0x30,0x00, // '}' 125
    0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00, // '~' 126
    0xFF,0x81,0x81,0x81,0x81,0x81,0x81,0xFF, // DEL→block 127
};

// 128×128 atlas: 16 columns × 16 rows of 8×8 char cells.
// Cell (col, row) holds char code = row*16 + col.
static uint8_t s_atlas[128 * 128];

static void buildAtlas()
{
    std::memset(s_atlas, 0, sizeof(s_atlas));

    for (int code = 0; code < 128; ++code)
    {
        // Map code → grid position
        int gx = code % 16;
        int gy = code / 16;
        int ax = gx * 8;
        int ay = gy * 8;

        // Printable range: 32-127 → font data at (code-32)*8
        if (code >= 32 && code <= 127)
        {
            int fi = (code - 32) * 8;
            for (int row = 0; row < 8; ++row)
            {
                uint8_t bits = kFont8x8[fi + row];
                for (int col = 0; col < 8; ++col)
                    s_atlas[(ay + row) * 128 + (ax + col)] =
                        ((bits >> (7 - col)) & 1) ? 255 : 0;
            }
        }
    }
}

// ============================================================
// Shaders — simple 2D overlay
// ============================================================
static const char* kVS2D = R"GLSL(
#version 450 core

layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform vec2 uScreenSize;

out vec2 vUV;
out vec4 vColor;

void main()
{
    // Convert screen-pixel coords to NDC.
    // Screen space: origin top-left, +X right, +Y down.
    // NDC:          origin center,   +X right, +Y up.
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV   = aUV;
    vColor = aColor;
}
)GLSL";

// Monochrome atlas: alpha channel only.
// Text pixels where atlas.r > 0 are drawn using the supplied colour.
// Background pixels are discarded (no overdraw cost).
static const char* kFS2D = R"GLSL(
#version 450 core

in vec2  vUV;
in vec4  vColor;

uniform sampler2D uFontTex;

out vec4 fragColor;

void main()
{
    float mask = texture(uFontTex, vUV).r;
    if (mask < 0.1) discard;
    fragColor = vec4(vColor.rgb, vColor.a * mask);
}
)GLSL";

// ============================================================
// Compile + link a two-stage shader program
// ============================================================
static uint32_t compileProg(const char* vs, const char* fs)
{
    GLuint prog = glCreateProgram();
    GLint  ok   = 0;
    char   log[512];

    auto addStage = [&](GLenum type, const char* src)
    {
        GLuint sh = glCreateShader(type);
        glShaderSource(sh, 1, &src, nullptr);
        glCompileShader(sh);
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok)
        {
            glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
            fprintf(stderr, "Text2D shader compile error:\n%s\n", log);
        }
        glAttachShader(prog, sh);
        glDeleteShader(sh);
    };

    addStage(GL_VERTEX_SHADER,   vs);
    addStage(GL_FRAGMENT_SHADER, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "Text2D shader link error:\n%s\n", log);
    }
    return prog;
}

// ============================================================
// init
// ============================================================
void Text2D::init()
{
    if (s_initialized) return;
    s_initialized = true;

    // ---- Font atlas texture (GL_R8 — single red channel) ----
    buildAtlas();

    glGenTextures(1, &s_fontTex);
    glBindTexture(GL_TEXTURE_2D, s_fontTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, 128, 128, 0,
                 GL_RED, GL_UNSIGNED_BYTE, s_atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    // ---- Shader ----
    s_prog = compileProg(kVS2D, kFS2D);

    // ---- VBO + VAO ----
    // FIX 2: Create our own VAO. Vertex attribs are configured once here
    // and re-used every flush() by simply binding s_vao. This is valid
    // in GL 4.5 core profile (unlike VAO 0 which is not a valid object).
    glGenBuffers(1, &s_vbo);
    glGenVertexArrays(1, &s_vao);

    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);

    // Pre-allocate the VBO to max capacity (STREAM_DRAW — rewritten every frame).
    glBufferData(GL_ARRAY_BUFFER,
                 kMaxQuads * kFloatsPerQuad * sizeof(float),
                 nullptr, GL_STREAM_DRAW);

    // Vertex layout (stride = 8 floats = 32 bytes):
    //   location 0: vec2 aPos    — offset  0
    //   location 1: vec2 aUV    — offset  8
    //   location 2: vec4 aColor — offset 16
    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

// ============================================================
// shutdown
// ============================================================
void Text2D::shutdown()
{
    if (s_fontTex) { glDeleteTextures(1,      &s_fontTex); s_fontTex = 0; }
    if (s_prog)    { glDeleteProgram(s_prog);               s_prog    = 0; }
    if (s_vbo)     { glDeleteBuffers(1,        &s_vbo);     s_vbo     = 0; }
    if (s_vao)     { glDeleteVertexArrays(1,   &s_vao);     s_vao     = 0; }
    s_initialized = false;
}

// ============================================================
// begin / end
// ============================================================
void Text2D::begin(int screenW, int screenH)
{
    s_inFrame    = true;
    s_screenW    = screenW;
    s_screenH    = screenH;
    s_vertexCount = 0;
}

void Text2D::end()
{
    if (!s_inFrame) return;
    flush();
    s_inFrame = false;
}

// ============================================================
// emitQuad — writes 6 vertices (2 triangles) for one quad.
//
// FIX 1: Each vertex now stores exactly 8 floats in one shot:
//   [x, y, u, v, r, g, b, a]
//
// The old code used a float[6][7] inner array that stored
// {x,y,u,v,r,g,b} (7 floats), then overwrote index 6 (blue)
// with alpha — losing blue and corrupting the color channel.
// ============================================================
void Text2D::emitQuad(float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1,
                      float r, float g, float b, float a)
{
    if (s_vertexCount + kVertsPerQuad > kMaxQuads * kVertsPerQuad)
        flush();  // auto-flush when full

    // Two triangles: TL, TR, BL  and  TR, BR, BL
    // (y increases downward in screen space)
    const float x1 = x + w;
    const float y1 = y + h;

    // 6 vertices, 8 floats each
    const float verts[kVertsPerQuad][kFloatsPerVertex] = {
        { x,  y,  u0, v0,  r, g, b, a },   // 0: TL
        { x1, y,  u1, v0,  r, g, b, a },   // 1: TR
        { x,  y1, u0, v1,  r, g, b, a },   // 2: BL
        { x1, y,  u1, v0,  r, g, b, a },   // 3: TR (shared)
        { x1, y1, u1, v1,  r, g, b, a },   // 4: BR
        { x,  y1, u0, v1,  r, g, b, a },   // 5: BL (shared)
    };

    float* dst = s_vbuf + s_vertexCount * kFloatsPerVertex;
    std::memcpy(dst, verts, sizeof(verts));
    s_vertexCount += kVertsPerQuad;
}

// ============================================================
// drawFill — solid colour rectangle (no text sampling).
// Uses a fully-lit pixel from the atlas (bottom-right cell, all 255).
// ============================================================
void Text2D::drawFill(int x, int y, int w, int h, Vec4 color)
{
    // Cell 127 (bottom-right of 16×16 grid) is a full block (0xFF).
    // Its UV covers the last 8×8 pixels: u ∈ [7/8, 1], v ∈ [7/8, 1].
    // The fragment shader only passes through pixels where mask >= 0.1,
    // which is the entire block — giving a solid fill.
    const float u0 = (7 * 8 + 0.5f) / 128.0f;
    const float v0 = (7 * 8 + 0.5f) / 128.0f;
    const float u1 = (7 * 8 + 7.5f) / 128.0f;
    const float v1 = (7 * 8 + 7.5f) / 128.0f;

    emitQuad((float)x, (float)y, (float)w, (float)h,
             u0, v0, u1, v1,
             color.x, color.y, color.z, color.w);
}

// ============================================================
// drawChar
// ============================================================
void Text2D::drawChar(int x, int y, int charCode, Vec4 color)
{
    if (charCode < 0 || charCode > 127) charCode = 127;

    int gx = charCode % 16;
    int gy = charCode / 16;

    // Half-texel inset to avoid bilinear bleeding from adjacent cells.
    float u0 = (gx * 8 + 0.5f) / 128.0f;
    float v0 = (gy * 8 + 0.5f) / 128.0f;
    float u1 = (gx * 8 + 7.5f) / 128.0f;
    float v1 = (gy * 8 + 7.5f) / 128.0f;

    emitQuad((float)x, (float)y, 8.0f, 8.0f,
             u0, v0, u1, v1,
             color.x, color.y, color.z, color.w);
}

// ============================================================
// drawString
// ============================================================
void Text2D::drawString(int x, int y, const char* text, Vec4 color)
{
    if (!text) return;
    int cx = x;
    while (*text)
    {
        unsigned char c = (unsigned char)*text++;
        if (c >= 128) c = '?';
        drawChar(cx, y, c, color);
        cx += 8;
    }
}

// ============================================================
// stringWidth
// ============================================================
int Text2D::stringWidth(const char* text)
{
    int w = 0;
    while (text && *text++) w += 8;
    return w;
}

// ============================================================
// flush — upload queued vertices and draw.
//
// FIX 2: Binds s_vao (dedicated VAO with pre-configured attribs)
// instead of glBindVertexArray(0).  In GL 4.5 core profile VAO 0
// is not valid; using it silently breaks glVertexAttribPointer
// and glDrawArrays produces nothing.
// ============================================================
void Text2D::flush()
{
    if (s_vertexCount == 0) return;

    // ---- Save relevant GL state ----
    GLint prevProg    = 0; glGetIntegerv(GL_CURRENT_PROGRAM,      &prevProg);
    GLint prevVAO     = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    GLint prevVBO     = 0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVBO);
    GLint prevTexUnit = 0; glGetIntegerv(GL_ACTIVE_TEXTURE,       &prevTexUnit);
    GLint prevTex2D   = 0;
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2D);

    GLboolean prevDepth  = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCull   = glIsEnabled(GL_CULL_FACE);
    GLboolean prevBlend  = glIsEnabled(GL_BLEND);
    GLboolean prevSRGB   = glIsEnabled(GL_FRAMEBUFFER_SRGB);

    GLint prevBlendSrc = 0, prevBlendDst = 0;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);

    // ---- Set up 2D overlay state ----
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_FRAMEBUFFER_SRGB);   // font texture is already linear
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // ---- Bind shader ----
    glUseProgram(s_prog);
    glUniform2f(glGetUniformLocation(s_prog, "uScreenSize"),
                (float)s_screenW, (float)s_screenH);
    glUniform1i(glGetUniformLocation(s_prog, "uFontTex"), 0);

    // ---- Bind font texture to slot 0 ----
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_fontTex);

    // ---- Upload vertex data ----
    // FIX 2: Bind our dedicated VAO. Attribs were already configured in init().
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(s_vertexCount * kFloatsPerVertex * sizeof(float)),
                    s_vbuf);

    // ---- Draw ----
    glDrawArrays(GL_TRIANGLES, 0, s_vertexCount);

    // ---- Restore GL state ----
    glBindBuffer(GL_ARRAY_BUFFER, prevVBO);
    glBindVertexArray(prevVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, prevTex2D);
    glActiveTexture(prevTexUnit);
    glUseProgram(prevProg);

    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCull)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
    if (prevBlend) glEnable(GL_BLEND);      else glDisable(GL_BLEND);
    if (prevSRGB)  glEnable(GL_FRAMEBUFFER_SRGB);
    glBlendFunc(prevBlendSrc, prevBlendDst);

    s_vertexCount = 0;
}

} // namespace nova