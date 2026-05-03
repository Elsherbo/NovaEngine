#pragma once

#include "engine/core/math/vec.h"
#include "engine/core/asset_fs.h"
#include <cstdint>

namespace nova
{

// ============================================================
// Text2D — 2D bitmap font renderer
//
// Font sources (priority order):
//   1. Loaded external bitmap font (Q2-style 128×128 grid)
//   2. Built-in hardcoded IBM CP437 8×8 font (always available)
//
// External font format:
//   128×128 image, 16×16 grid of 8×8 glyphs, ASCII layout.
//   Supports PNG/TGA/PCX loaded via AssetFS.
//   Q2 uses conchars.pcx from pak0.pak — compatible.
//
// Usage:
//   Text2D::init();
//   Text2D::tryLoadFont(assets, "pics/conchars.pcx"); // optional
//   Text2D::begin(w, h);
//   Text2D::drawString(10, 10, "Hello", {1,1,1,1});
//   Text2D::end();
// ============================================================

class Text2D
{
public:
    // ---- Lifecycle ----
    static void init();
    static void shutdown();

    // ---- External font loading ----
    // Attempts to load a Q2-style 128×128 bitmap font from AssetFS.
    // Falls back to built-in font if loading fails.
    // Call after init(), before first begin().
    // Returns true if the external font was loaded successfully.
    static bool tryLoadFont(AssetFS* assets, const char* logicalPath);

    // Load from raw RGBA8 pixels (width x height, must be 128x128).
    // Alpha channel is used as the glyph mask (white=opaque, black=transparent).
    // If the source has no alpha (RGB), luminance is used as alpha.
    static bool loadFontFromPixels(const uint8_t* rgba, int width, int height);

    // Reset to built-in hardcoded font.
    static void resetToBuiltinFont();

    // ---- Frame ----
    static void begin(int screenW, int screenH);
    static void end();

    // ---- Primitives ----
    static void drawFill(int x, int y, int w, int h, Vec4 color);
    static void drawFillGradientH(int x, int y, int w, int h, Vec4 colorL, Vec4 colorR);
    static void drawChar(int x, int y, int charCode, Vec4 color);
    static void drawString(int x, int y, const char* text, Vec4 color);
    static void drawStringShadow(int x, int y, const char* text, Vec4 color,
                                  Vec4 shadowColor = {0,0,0,0.7f},
                                  int ox = 1, int oy = 1);

    // ---- Font metrics ----
    // Glyph size is always 8×8 in the atlas.
    // Scale controls display size (default 2 = 16×16 pixels).
    static constexpr int kGlyphW = 8;
    static constexpr int kGlyphH = 8;

    static void  setScale(int scale);  // 1=tiny, 2=normal, 3=large
    static int   getScale()     { return s_scale; }
    static int   charWidth()    { return kGlyphW * s_scale; }
    static int   charHeight()   { return kGlyphH * s_scale; }
    static int   stringWidth(const char* text);

    // ---- Internal layout (public for setupVAOAttribs) ----
    static constexpr int kFloatsPerVertex = 8;

private:
    static void flush();
    static void flushFill();
    static void buildBuiltinAtlas();
    static void uploadAtlasToGPU(const uint8_t* rgba8, int w, int h);

    static void emitQuad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         float r, float g, float b, float a);
    static void emitFillQuad(float x, float y, float w, float h,
                              float r0, float g0, float b0, float a0,
                              float r1, float g1, float b1, float a1);

    // GL objects
    static uint32_t s_fontTex;
    static uint32_t s_textProg;
    static uint32_t s_fillProg;
    static uint32_t s_vbo;
    static uint32_t s_vao;
    static uint32_t s_fillVao;

    // State
    static bool s_initialized;
    static bool s_inFrame;
    static int  s_screenW, s_screenH;
    static int  s_scale;
    static bool s_usingExternalFont;

    // Vertex buffer
    static constexpr int kVertsPerQuad  = 6;
    static constexpr int kFloatsPerQuad = kVertsPerQuad * kFloatsPerVertex;
    static constexpr int kMaxQuads      = 8192;
    static float s_vbuf[kMaxQuads * kFloatsPerQuad];
    static int   s_vertexCount;
    static bool  s_inFillMode;
};

} // namespace nova