// ============================================================
// FILE:    engine/core/text_2d.h
// MODULE:  Core > 2D Text Overlay
// ============================================================
#pragma once

#include "engine/core/math/vec.h"
#include <cstdint>

namespace nova
{

class Text2D
{
public:
    // ---- Initialize once (creates font atlas + GL resources) ----
    static void init();
    static void shutdown();

    // ---- Frame boundaries ----
    static void begin(int screenW, int screenH);
    static void end();

    // ---- Drawing primitives (between begin/end) ----
    static void drawFill(int x, int y, int w, int h, Vec4 color);
    static void drawChar(int x, int y, int charCode, Vec4 color);
    static void drawString(int x, int y, const char* text, Vec4 color);

    // ---- Font metrics ----
    static int charWidth()  { return 8; }
    static int charHeight() { return 8; }
    static int stringWidth(const char* text);

private:
    static void flush();
    static void emitQuad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         float r, float g, float b, float a);

    // GL resources
    static uint32_t s_fontTex;
    static uint32_t s_prog;
    static uint32_t s_vbo;
    static bool     s_initialized;

    // Drawing state
    static bool     s_inFrame;
    static int      s_screenW, s_screenH;

    // Vertex format: x,y, u,v, r,g,b,a  (7 floats)
    static constexpr int kVertsPerQuad = 6; // 2 triangles
    static constexpr int kFloatsPerVertex = 7;
    static constexpr int kFloatsPerQuad = kVertsPerQuad * kFloatsPerVertex;
    static constexpr int kMaxQuads = 2048;
    static float s_vbuf[kMaxQuads * kFloatsPerQuad];
    static int   s_vertexCount;
};

} // namespace nova
