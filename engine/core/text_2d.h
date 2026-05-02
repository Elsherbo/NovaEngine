// ============================================================
// FILE:    engine/core/text_2d.h
// MODULE:  Core > 2D Text Overlay
// VERSION: v4 — Complete rewrite, invisible-text bug fixed
//
// ROOT CAUSE OF INVISIBLE TEXT (v3 bug):
//   The BSP renderer calls glBindSampler(0, lmAtlasSampler) which
//   installs a GL sampler OBJECT on texture unit 0. Sampler objects
//   COMPLETELY override a texture's own GL_TEXTURE_MIN/MAG_FILTER
//   parameters. flush() correctly binds s_fontTex to unit 0 but
//   never unbinds the BSP sampler object — so the font texture was
//   being sampled through the lightmap atlas sampler parameters.
//   The atlas sampler uses GL_LINEAR_MIPMAP_LINEAR (trilinear) with
//   an LOD bias of -0.5. Since s_fontTex has mipLevels=1 (no mip
//   chain), the trilinear sampler returns undefined/0 values when
//   asked for mip levels that don't exist. Result: all texels return
//   vec4(0,0,0,0), mask=0, every fragment discarded → invisible text.
//
// FIX: flush() now calls glBindSampler(0, 0) before drawing to detach
//   any sampler object from unit 0, then restores the previous sampler
//   binding after drawing. This lets the texture's own parameters take
//   effect, which are GL_NEAREST for the font atlas.
//
// ADDITIONAL FIXES in v4:
//   - Dedicated text VAO with explicit attrib setup every flush() call
//     (eliminates BSP VAO state bleed entirely)
//   - glBindSampler save/restore added to both flush() and flushFill()
//   - Font atlas now uses GL_NEAREST on both min and mag (was only mag)
//   - charWidth / charHeight configurable scale (default 2x = 16px)
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

    // --- Primitives ---
    static void drawFill(int x, int y, int w, int h, Vec4 color);
    // Horizontal gradient fill (left-color to right-color)
    static void drawFillGradientH(int x, int y, int w, int h, Vec4 colorL, Vec4 colorR);
    static void drawChar(int x, int y, int charCode, Vec4 color);
    static void drawString(int x, int y, const char* text, Vec4 color);
    // Draw string with a drop-shadow offset by (ox,oy) pixels
    static void drawStringShadow(int x, int y, const char* text, Vec4 color,
                                  Vec4 shadowColor = {0,0,0,0.7f},
                                  int ox = 1, int oy = 1);

    // --- Font metrics ---
    // The 8x8 bitmap font is rendered at 2x scale → 16x16 on screen.
    static constexpr int kGlyphW = 8;
    static constexpr int kGlyphH = 8;
    static constexpr int kScale  = 2;
    static int charWidth()  { return kGlyphW * kScale; }
    static int charHeight() { return kGlyphH * kScale; }
    static int stringWidth(const char* text);

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
    static uint32_t s_vao;        // text VAO
    static uint32_t s_fillVao;    // fill VAO
    static bool     s_initialized;

    static bool     s_inFrame;
    static int      s_screenW, s_screenH;

    // Vertex layout: x,y, u,v, r,g,b,a  (8 floats — text quads)
    // Fill vertex:   x,y, _,_, r,g,b,a  (8 floats — same layout, uv unused)
    static constexpr int kVertsPerQuad    = 6;
    static constexpr int kFloatsPerVertex = 8;
    static constexpr int kFloatsPerQuad   = kVertsPerQuad * kFloatsPerVertex;
    static constexpr int kMaxQuads        = 8192;

    // Single shared VBO, two separate VAOs (text vs fill).
    // The VAO stores the glVertexAttribPointer state independently.
    static float s_vbuf[kMaxQuads * kFloatsPerQuad];
    static int   s_vertexCount;
    static bool  s_inFillMode; // true when pending verts are fill quads
};

} // namespace nova