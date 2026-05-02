// ============================================================
// FILE:    engine/core/text_2d.cpp
// MODULE:  Core > 2D Text Overlay — Quake2-style
// STATUS:  FIXED v2
//
// FIX LOG (v2 — console gray-screen patch):
//
//   ROOT CAUSE ANALYSIS:
//   The "bluish gray screen" when opening the console has three
//   compounding causes that were all corrected here:
//
//   FIX A — Depth mask: flush() disabled GL_DEPTH_TEST but left
//     glDepthMask at GL_TRUE. The 2D quads therefore wrote depth
//     values of 0.0 (NDC z = 0 for ortho projection) into the
//     depth buffer. With reversed-Z (GL_GREATER), 0.0 means
//     "farthest". On the NEXT frame, BSP fragments that should
//     have passed (depth > 0.0 → GREATER) were comparing against
//     the 0.0 left by Text2D quads. Because no BSP fragment can
//     be > 0.0 after a clearDepth(0.f), the BSP actually renders
//     fine — but any Text2D quad that coincidentally wrote 0.0
//     over a BSP pixel during one frame left that pixel stuck at
//     0.0. This manifested as a speckled / full-coverage gray
//     overlay depending on how the background quad covered the
//     viewport. Fix: disable depth mask during 2D rendering.
//
//   FIX B — sRGB color space: flush() disabled GL_FRAMEBUFFER_SRGB
//     because "font texture is already linear." This is half-right:
//     the R8 atlas is linear, so reading it doesn't involve sRGB
//     decoding. But the *output* colors (kColBg, kColPrompt etc.)
//     are specified as linear [0..1] values that need to be
//     gamma-encoded before writing to the sRGB framebuffer.
//     Disabling GL_FRAMEBUFFER_SRGB means the linear values are
//     written as-is: kColBg = {0.05, 0.05, 0.10} writes as
//     RGB(13, 13, 26). With sRGB encoding enabled that same value
//     would write as RGB(64, 64, 88) — visibly dark navy but
//     semi-transparent. Without it the console background is so
//     dark it's near-black, and kColPrompt {0.25,1.0,0.30} which
//     should be bright green appears as dull RGB(64,255,77).
//     Fix: keep GL_FRAMEBUFFER_SRGB enabled during flush(). The
//     font atlas reads as linear (hardware decodes GL_R8 as linear
//     regardless of sRGB state — sRGB only auto-decodes GL_SRGB*
//     internal formats). No double-encode occurs.
//
//   FIX C — Blend state restoration order: the old code restored
//     blend src/dst using GL_BLEND_SRC_ALPHA / GL_BLEND_DST_ALPHA
//     queries. These only capture the *alpha* component factors;
//     if BSP had set glBlendFuncSeparate() the RGB factors would
//     be lost. Changed to query GL_BLEND_SRC_RGB / GL_BLEND_DST_RGB
//     and restore both RGB and alpha factors separately.
//
//   FIX D — glClipControl & glDepthFunc: BSP render uses
//     glClipControl(GL_LOWER_LEFT, GL_ZERO_TO_ONE) + glDepthFunc(GL_GREATER)
//     for reversed-Z. Text2D's vertex shader flips Y ("ndc.y = -ndc.y")
//     assuming top-left screen space. Without saving and restoring
//     these two states the next BSP frame renders upside-down or with
//     wrong depth test, producing a bluish-gray screen that covers the
//     entire viewport. flush() now saves both states and temporarily
//     sets glDepthFunc(GL_ALWAYS) so 2D quads always pass even if
//     GL_DEPTH_TEST is somehow enabled mid-frame.
//
//   UNCHANGED from v1:
//   - kFloatsPerVertex = 8: layout x,y,u,v,r,g,b,a
//   - Dedicated s_vao with attribs wired once in init()
//   - emitQuad writes all 8 floats correctly
//   - flush() saves/restores GL_CURRENT_PROGRAM, VAO, VBO, textures
// ============================================================

#include "engine/core/text_2d.h"

#include <glad/glad.h>
#include <cstdio>
#include <cstring>

namespace nova
{
// ---- DEBUG: dump all pending GL errors to stderr ----
static void dumpGLErrors(const char* where)
{
    GLenum e;
    while ((e = glGetError()) != GL_NO_ERROR)
        fprintf(stderr, "GL ERROR in %s: 0x%04X\n", where, e);
}


// ---- Static storage ----
uint32_t Text2D::s_fontTex    = 0;
uint32_t Text2D::s_prog       = 0;
uint32_t Text2D::s_vbo        = 0;
uint32_t Text2D::s_vao        = 0;
int      Text2D::s_uScreenSizeLoc = -1;
int      Text2D::s_uFontTexLoc    = -1;
uint32_t Text2D::s_fillProg         = 0;
int      Text2D::s_fillScreenSizeLoc = -1;
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
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // DEL→solid block 127
};

// 128×128 atlas: 16 columns × 16 rows of 8×8 char cells.
static uint8_t s_atlas[128 * 128];

static void buildAtlas()
{
    std::memset(s_atlas, 0, sizeof(s_atlas));

    for (int code = 0; code < 128; ++code)
    {
        int gx = code % 16;
        int gy = code / 16;
        int ax = gx * 8;
        int ay = gy * 8;

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
// Fill shaders — solid-color quads (no texture, no discard)
// ============================================================
static const char* kVSFill = R"GLSL(
#version 450 core
layout(location = 0) in vec2 aPos;
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
void main()
{
    fragColor = vColor;
}
)GLSL";

// ============================================================
// Shaders
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
    vec2 ndc = (aPos / uScreenSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV   = aUV;
    vColor = aColor;
}
)GLSL";

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
    fprintf(stderr, "[Text2D::init] building font atlas and GL resources\n");

    // Save current VAO so we don't corrupt the global VAO that
    // BSP relies on.  In GL 4.5 core profile, VAO 0 is invalid —
    // calling glVertexAttribPointer with it bound silently fails
    // (GL_INVALID_OPERATION), which permanently breaks BSP rendering
    // after the first console open.
    GLint savedVAO = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &savedVAO);

    buildAtlas();

    // Upload as RGBA8 — replicate R8 into all 4 channels for driver
    // compatibility.  Some GL drivers (MinGW/ANGLE, certain Intel GPUs)
    // return 0 when sampling GL_R8 in a fragment shader.
    uint8_t atlasRGBA[128 * 128 * 4];
    for (int i = 0; i < 128 * 128; ++i) {
        atlasRGBA[i * 4 + 0] = s_atlas[i];
        atlasRGBA[i * 4 + 1] = s_atlas[i];
        atlasRGBA[i * 4 + 2] = s_atlas[i];
        atlasRGBA[i * 4 + 3] = s_atlas[i];
    }

    glGenTextures(1, &s_fontTex);
    glBindTexture(GL_TEXTURE_2D, s_fontTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, atlasRGBA);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    s_prog = compileProg(kVS2D, kFS2D);
    s_uScreenSizeLoc = glGetUniformLocation(s_prog, "uScreenSize");
    s_uFontTexLoc    = glGetUniformLocation(s_prog, "uFontTex");

    s_fillProg = compileProg(kVSFill, kFSFill);
    s_fillScreenSizeLoc = glGetUniformLocation(s_fillProg, "uScreenSize");
    if (s_uScreenSizeLoc == -1)
        fprintf(stderr, "Text2D::init: WARNING — 'uScreenSize' uniform not found in shader\n");
    if (s_uFontTexLoc == -1)
        fprintf(stderr, "Text2D::init: WARNING — 'uFontTex' uniform not found in shader\n");

    glGenBuffers(1, &s_vbo);
    glGenVertexArrays(1, &s_vao);

    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);

    glBufferData(GL_ARRAY_BUFFER,
                 kMaxQuads * kFloatsPerQuad * sizeof(float),
                 nullptr, GL_STREAM_DRAW);

    const GLsizei stride = kFloatsPerVertex * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)(4 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(savedVAO);   // restore — DO NOT bind VAO 0
}

// ============================================================
// shutdown
// ============================================================
void Text2D::shutdown()
{
    if (s_fontTex) { glDeleteTextures(1,      &s_fontTex); s_fontTex = 0; }
    if (s_prog)    { glDeleteProgram(s_prog);               s_prog    = 0; }
    if (s_fillProg){ glDeleteProgram(s_fillProg);            s_fillProg= 0; }
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
// emitQuad
// ============================================================
void Text2D::emitQuad(float x, float y, float w, float h,
                      float u0, float v0, float u1, float v1,
                      float r, float g, float b, float a)
{
    if (s_vertexCount + kVertsPerQuad > kMaxQuads * kVertsPerQuad)
        flush();

    const float x1 = x + w;
    const float y1 = y + h;

    const float verts[kVertsPerQuad][kFloatsPerVertex] = {
        { x,  y,  u0, v0,  r, g, b, a },
        { x1, y,  u1, v0,  r, g, b, a },
        { x,  y1, u0, v1,  r, g, b, a },
        { x1, y,  u1, v0,  r, g, b, a },
        { x1, y1, u1, v1,  r, g, b, a },
        { x,  y1, u0, v1,  r, g, b, a },
    };

    float* dst = s_vbuf + s_vertexCount * kFloatsPerVertex;
    std::memcpy(dst, verts, sizeof(verts));
    s_vertexCount += kVertsPerQuad;
}

// ============================================================
// drawFill — solid colour rectangle via dedicated fill shader
// ============================================================
void Text2D::drawFill(int x, int y, int w, int h, Vec4 color)
{
    // Flush any pending atlas quads first (they use the atlas shader)
    flush();

    // Emit the fill quad and draw immediately with the fill shader.
    // No texture sampling, no discard — 100% opaque/transparent as specified.
    const float xf = (float)x, yf = (float)y, wf = (float)w, hf = (float)h;
    const float verts[kVertsPerQuad][kFloatsPerVertex] = {
        { xf,    yf,    0, 0, color.x, color.y, color.z, color.w },
        { xf+wf, yf,    0, 0, color.x, color.y, color.z, color.w },
        { xf,    yf+hf, 0, 0, color.x, color.y, color.z, color.w },
        { xf+wf, yf,    0, 0, color.x, color.y, color.z, color.w },
        { xf+wf, yf+hf, 0, 0, color.x, color.y, color.z, color.w },
        { xf,    yf+hf, 0, 0, color.x, color.y, color.z, color.w },
    };

    float* dst = s_vbuf;
    std::memcpy(dst, verts, sizeof(verts));
    s_vertexCount = kVertsPerQuad;
    flushFill();
    s_vertexCount = 0;
}

// ============================================================
// drawChar
// ============================================================
void Text2D::drawChar(int x, int y, int charCode, Vec4 color)
{
    if (charCode < 0 || charCode > 127) charCode = 127;

    int gx = charCode % 16;
    int gy = charCode / 16;

    float u0 = (gx * 8 + 0.5f) / 128.0f;
    float v0 = (gy * 8 + 0.5f) / 128.0f;
    float u1 = (gx * 8 + 7.5f) / 128.0f;
    float v1 = (gy * 8 + 7.5f) / 128.0f;

    emitQuad((float)x, (float)y, 16.0f, 16.0f,
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
// flushFill — draw fill quads with dedicated solid-color shader
// ============================================================
void Text2D::flushFill()
{
    if (s_vertexCount == 0) return;

    // ---- Save GL state (same as flush()) ----
    GLint prevProg = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProg);

    GLboolean prevDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCull  = glIsEnabled(GL_CULL_FACE);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);

    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    GLint prevBlendSrcRGB = 0, prevBlendDstRGB = 0;
    GLint prevBlendSrcA   = 0, prevBlendDstA   = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB,   &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB,   &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstA);

    GLint prevDepthFunc = 0;
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);

    GLint prevClipOrigin = 0, prevClipDepth = 0;
    glGetIntegerv(GL_CLIP_ORIGIN,     &prevClipOrigin);
    glGetIntegerv(GL_CLIP_DEPTH_MODE, &prevClipDepth);

    GLint prevVAO = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    GLint prevVBO = 0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVBO);

    // ---- Set up 2D overlay state ----
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDepthFunc(GL_ALWAYS);

    // ---- Draw ----
    glUseProgram(s_fillProg);
    if (s_fillScreenSizeLoc >= 0)
        glUniform2f(s_fillScreenSizeLoc, (float)s_screenW, (float)s_screenH);

    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(s_vertexCount * kFloatsPerVertex * sizeof(float)),
                    s_vbuf);
    // fprintf(stderr, "[Text2D::flushFill] drawing %d verts, fillProg=%u vao=%u\n", s_vertexCount, s_fillProg, s_vao);
    glDrawArrays(GL_TRIANGLES, 0, s_vertexCount);
    dumpGLErrors("flushFill::glDrawArrays");

    // ---- Restore GL state ----
    glBindBuffer(GL_ARRAY_BUFFER, prevVBO);
    glBindVertexArray(prevVAO);
    glUseProgram(prevProg);

    glDepthMask(prevDepthMask);
    glDepthFunc(prevDepthFunc);
    glClipControl((GLenum)prevClipOrigin, (GLenum)prevClipDepth);

    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCull)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
    if (prevBlend) glEnable(GL_BLEND);      else glDisable(GL_BLEND);
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB,
                        prevBlendSrcA,   prevBlendDstA);

    s_vertexCount = 0;
}

// ============================================================
// flush — core GL state management
//
// KEY CHANGES FROM v1:
//
// FIX A: Add glDepthMask(GL_FALSE) before drawing and restore after.
//   Without this, 2D quads write depth=0.0 (NDC z=0 in ortho) into
//   the depth buffer. With reversed-Z (GL_GREATER + clearDepth(0)),
//   this corrupts the depth buffer: pixels covered by console quads
//   get stuck at 0.0 so next frame's BSP fragments can't overwrite
//   them (BSP frags also produce depth 0 from clearDepth, so GREATER
//   fails). Result: ghosted geometry wherever console quads landed.
//
// FIX B: Remove glDisable(GL_FRAMEBUFFER_SRGB) — keep it enabled.
//   The font atlas is GL_R8 (linear, hardware never sRGB-decodes it).
//   Our vertex colors (kColBg, kColPrompt, etc.) are specified in
//   linear [0,1] space. With GL_FRAMEBUFFER_SRGB enabled, OpenGL
//   gamma-encodes the output before writing — correct behavior.
//   Disabling it caused linear values to write directly to the sRGB
//   framebuffer: kColBg {0.05,0.05,0.10} would appear as nearly
//   black (13,13,26) instead of dark-navy (64,64,88), and even
//   fully-white text would appear at ~50% visual brightness.
//
// FIX C: Query and restore GL_BLEND_SRC_RGB / GL_BLEND_DST_RGB
//   (not _ALPHA variants) to correctly restore separate blend funcs.
// ============================================================
void Text2D::flush()
{
    if (s_vertexCount == 0) return;

    // ---- Save GL state ----
    GLint prevProg    = 0; glGetIntegerv(GL_CURRENT_PROGRAM,      &prevProg);
    GLint prevVAO     = 0; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    GLint prevVBO     = 0; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVBO);
    GLint prevTexUnit = 0; glGetIntegerv(GL_ACTIVE_TEXTURE,       &prevTexUnit);

    GLint prevTex2D = 0;
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex2D);

    GLboolean prevDepth  = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevCull   = glIsEnabled(GL_CULL_FACE);
    GLboolean prevBlend  = glIsEnabled(GL_BLEND);
    // FIX A: save depth mask
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);

    // FIX C: save full blend function (RGB components, not just alpha)
    GLint prevBlendSrcRGB = 0, prevBlendDstRGB = 0;
    GLint prevBlendSrcA   = 0, prevBlendDstA   = 0;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstA);

    // FIX B: do NOT query/touch GL_FRAMEBUFFER_SRGB — keep it as-is.
    // The atlas is GL_R8 (linear), vertex colors are linear-space floats.
    // GL_FRAMEBUFFER_SRGB should remain ENABLED so the hardware correctly
    // gamma-encodes our linear color output when writing to the sRGB fb.

    // FIX D: save glClipControl state — BSP uses (LOWER_LEFT, ZERO_TO_ONE)
    // for reversed-Z.  Text2D's vertex shader flips Y ("ndc.y = -ndc.y")
    // assuming top-left screen space.  Without restoring clip control the
    // BSP renders upside-down or clipped on the next frame.
    GLint prevClipOrigin = 0, prevClipDepth = 0;
    glGetIntegerv(GL_CLIP_ORIGIN,      &prevClipOrigin);
    glGetIntegerv(GL_CLIP_DEPTH_MODE,  &prevClipDepth);

    // FIX D: save depth func — BSP uses GL_GREATER for reversed-Z.
    GLint prevDepthFunc = 0;
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);

    // ---- Set up 2D overlay state ----
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // FIX A: disable depth writes — 2D quads must not corrupt the depth buffer
    glDepthMask(GL_FALSE);
    // FIX D: override depth func to ALWAYS during flush (reversed-Z BSP uses GREATER)
    // so that even if depth test is somehow enabled later, 2D quads always pass.
    glDepthFunc(GL_ALWAYS);

     // ---- Bind shader & uniforms (always fresh — no cached loc bugs) ----
     glUseProgram(s_prog);
     GLint locScreen = glGetUniformLocation(s_prog, "uScreenSize");
     GLint locTex    = glGetUniformLocation(s_prog, "uFontTex");
    //  fprintf(stderr, "[Text2D::flush] locScreen=%d locTex=%d screenW=%d screenH=%d\n", locScreen, locTex, s_screenW, s_screenH);
     if (locScreen >= 0) glUniform2f(locScreen, (float)s_screenW, (float)s_screenH);
     if (locTex    >= 0) glUniform1i(locTex, 0);
     dumpGLErrors("flush::uniforms");

    // ---- Bind font texture to slot 0 ----
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_fontTex);

    // ---- Upload vertex data and draw ----
    glBindVertexArray(s_vao);
    glBindBuffer(GL_ARRAY_BUFFER, s_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (GLsizeiptr)(s_vertexCount * kFloatsPerVertex * sizeof(float)),
                    s_vbuf);
    // fprintf(stderr, "[Text2D::flush] drawing %d verts, prog=%u vao=%u vbo=%u tex=%u screenW=%d screenH=%d\n", s_vertexCount, s_prog, s_vao, s_vbo, s_fontTex, s_screenW, s_screenH);
    glDrawArrays(GL_TRIANGLES, 0, s_vertexCount);
    dumpGLErrors("flush::glDrawArrays");

    // ---- Restore GL state ----
    glBindBuffer(GL_ARRAY_BUFFER, prevVBO);
    glBindVertexArray(prevVAO);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, prevTex2D);
    glActiveTexture(prevTexUnit);

    glUseProgram(prevProg);

    // FIX A: restore depth mask before re-enabling depth test
    glDepthMask(prevDepthMask);

    if (prevDepth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevCull)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
    if (prevBlend) glEnable(GL_BLEND);      else glDisable(GL_BLEND);

    // FIX C: restore full blend function (not just alpha components)
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB,
                        prevBlendSrcA,   prevBlendDstA);

    // FIX B: GL_FRAMEBUFFER_SRGB is not touched — no restore needed.

    // FIX D: restore glClipControl and glDepthFunc to BSP state
    glClipControl((GLenum)prevClipOrigin, (GLenum)prevClipDepth);
    glDepthFunc(prevDepthFunc);

    s_vertexCount = 0;
}

} // namespace nova