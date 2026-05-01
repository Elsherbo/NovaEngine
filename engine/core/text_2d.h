// ============================================================
// FILE:    engine/core/text_2d.h
// MODULE:  Core > 2D Text Overlay
// STATUS:  FIXED
//
// FIX LOG:
//   1. kFloatsPerVertex changed from 7 to 8 (x,y,u,v,r,g,b,a).
//      Old layout stored alpha at index 6, overwriting the blue
//      channel, and the color attrib pointer read 4 floats from
//      offset 16 which bled into the next vertex — vColor.a was
//      garbage (often 0), making all text fully transparent.
//
//   2. Added s_vao — a dedicated VAO for Text2D.
//      The old code called glBindVertexArray(0) and then set up
//      vertex attribs.  In GL 4.5 core profile, VAO 0 is NOT a
//      valid object; all glVertexAttribPointer calls silently fail
//      (GL_INVALID_OPERATION) and glDrawArrays produces nothing.
//      Text2D now owns its own VAO with attribs pre-wired at init.
//
//   3. flush() now just binds the VAO + uploads VBO data and draws.
//      No per-frame glVertexAttribPointer calls needed.
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
    static void flushFill();

    // Emits one quad (6 vertices) into s_vbuf.
    // All 8 floats per vertex are written correctly.
    static void emitQuad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         float r, float g, float b, float a);

    // GL resources
    static uint32_t s_fontTex;
    static uint32_t s_prog;
    static uint32_t s_vbo;
    static uint32_t s_vao;
    static int s_uScreenSizeLoc;
    static int s_uFontTexLoc;
    static uint32_t s_fillProg;
    static int s_fillScreenSizeLoc;
    static bool     s_initialized;

    // Drawing state
    static bool     s_inFrame;
    static int      s_screenW, s_screenH;

    // Vertex format: x,y, u,v, r,g,b,a  (8 floats per vertex)
    // FIX 1: was 7 (missing alpha as separate field, blue was overwritten)
    static constexpr int kVertsPerQuad    = 6;   // 2 triangles
    static constexpr int kFloatsPerVertex = 8;   // FIX: was 7
    static constexpr int kFloatsPerQuad   = kVertsPerQuad * kFloatsPerVertex;
    static constexpr int kMaxQuads        = 2048;
    static float s_vbuf[kMaxQuads * kFloatsPerQuad];
    static int   s_vertexCount;            // in vertices, not quads
};

} // namespace nova