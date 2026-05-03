#pragma once

#include "engine/core/math/vec.h"
#include <cstdint>

namespace nova
{

class Text2D
{
public:
    static void init();
    static void shutdown();

    static void begin(int screenW, int screenH);
    static void end();

    // --- Primitives ---
    static void drawFill(int x, int y, int w, int h, Vec4 color);
    static void drawFillGradientH(int x, int y, int w, int h, Vec4 colorL, Vec4 colorR);
    static void drawChar(int x, int y, int charCode, Vec4 color);
    static void drawString(int x, int y, const char* text, Vec4 color);
    static void drawStringShadow(int x, int y, const char* text, Vec4 color,
                                  Vec4 shadowColor = {0,0,0,0.7f},
                                  int ox = 1, int oy = 1);

    // --- Font metrics ---
    static constexpr int kGlyphW = 8;
    static constexpr int kGlyphH = 8;
    static constexpr int kScale  = 2;
    static int charWidth()  { return kGlyphW * kScale; }
    static int charHeight() { return kGlyphH * kScale; }
    static int stringWidth(const char* text);

    // --- Vertex layout constants (public: also used by setupVAOAttribs free function) ---
    static constexpr int kFloatsPerVertex = 8;

private:
    static void flush();
    static void flushFill();
    static void emitQuad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         float r, float g, float b, float a);
    static void emitFillQuad(float x, float y, float w, float h,
                              float r0, float g0, float b0, float a0,
                              float r1, float g1, float b1, float a1);

    static uint32_t s_fontTex;
    static uint32_t s_textProg;
    static uint32_t s_fillProg;
    static uint32_t s_vbo;
    static uint32_t s_vao;
    static uint32_t s_fillVao;
    static bool     s_initialized;

    static bool     s_inFrame;
    static int      s_screenW, s_screenH;

    static constexpr int kVertsPerQuad    = 6;
    // kFloatsPerVertex is declared public above
    static constexpr int kFloatsPerQuad   = kVertsPerQuad * kFloatsPerVertex;
    static constexpr int kMaxQuads        = 8192;

    static float s_vbuf[kMaxQuads * kFloatsPerQuad];
    static int   s_vertexCount;
    static bool  s_inFillMode;
};

} // namespace nova