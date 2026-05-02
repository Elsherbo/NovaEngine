// ============================================================
// FILE:    engine/core/text_2d.h
// MODULE:  Core > 2D Text Overlay
// STATUS:  FIXED v3 — advance/quad size consistency
//
// FIX LOG (v3):
//   BUG: drawChar emitted 16×16 quads but drawString advanced
//        cursor by only 8px — 50% overlap made every glyph
//        overwrite the right half of the previous one, producing
//        an unreadable smear.
//   FIX: drawString and stringWidth now advance 16px to match
//        the quad width. charWidth() updated to return 16.
//        All layout math in console.cpp now resolves correctly.
// ============================================================
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

    static void drawFill(int x, int y, int w, int h, Vec4 color);
    static void drawChar(int x, int y, int charCode, Vec4 color);
    static void drawString(int x, int y, const char* text, Vec4 color);

    // ---- Font metrics ----
    // Both return 16: the 8×8 atlas glyph is upscaled 2× to 16×16 on screen.
    static int charWidth()  { return 16; }
    static int charHeight() { return 16; }
    static int stringWidth(const char* text);

private:
    static void flush();
    static void flushFill();
    static void emitQuad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         float r, float g, float b, float a);

    static uint32_t s_fontTex;
    static uint32_t s_prog;
    static uint32_t s_vbo;
    static uint32_t s_vao;
    static int s_uScreenSizeLoc;
    static int s_uFontTexLoc;
    static uint32_t s_fillProg;
    static int s_fillScreenSizeLoc;
    static bool     s_initialized;

    static bool     s_inFrame;
    static int      s_screenW, s_screenH;

    // Vertex format: x,y, u,v, r,g,b,a  (8 floats per vertex)
    static constexpr int kVertsPerQuad    = 6;
    static constexpr int kFloatsPerVertex = 8;
    static constexpr int kFloatsPerQuad   = kVertsPerQuad * kFloatsPerVertex;
    static constexpr int kMaxQuads        = 4096;  // doubled from 2048
    static float s_vbuf[kMaxQuads * kFloatsPerQuad];
    static int   s_vertexCount;
};

} // namespace nova