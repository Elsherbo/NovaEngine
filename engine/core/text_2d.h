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
//   1. Loaded external bitmap font (Q2-style conchars)
//   2. Built-in hardcoded IBM CP437 8×8 font (always available)
//
// Q2 conchars format:
//   Square image, divisible by 16. Standard sizes: 128, 256, 512.
//   16×16 grid of equally-sized glyphs, ASCII layout.
//   Top half (rows 0-7)    = normal character set (silver/white)
//   Bottom half (rows 8-15) = alternate character set (gold/brown)
//   Black (or top-left pixel) = transparent background (color key).
//
//   Both sets are stored internally as two separate 128×128 atlases:
//     Atlas 0 = normal set   → drawString() / drawChar()
//     Atlas 1 = alternate set → drawStringAlt() / drawCharAlt()
//
// Usage:
//   Text2D::init();
//   Text2D::tryLoadQ2Conchars(&assets, "pics/conchars.png");
//   Text2D::begin(w, h);
//   Text2D::drawString(10, 10, "normal text", {1,1,1,1});
//   Text2D::drawStringAlt(10, 26, "gold text", {1,1,1,1});
//   Text2D::end();
// ============================================================

class Text2D
{
public:
    // ---- Lifecycle ----
    static void init();
    static void shutdown();

    // ---- External font loading ----

    // Load a Q2-style conchars image. Accepts any square image whose
    // width is a multiple of 16 (128, 256, 512, 1024...).
    // Automatically downsamples to 8x8 glyphs using a box filter.
    // Loads BOTH the normal set (top half) and alternate set (bottom half).
    // Returns true on success; falls back to built-in font on failure.
    static bool tryLoadQ2Conchars(AssetFS* assets, const char* logicalPath);

    // Load a simple 128×128 generic bitmap font (single set, no alternate).
    // For non-Q2 fonts found online (int10h, UltimateDOSFont etc.)
    static bool tryLoadFont(AssetFS* assets, const char* logicalPath);

    // Load from raw RGBA8 pixels directly (width x height must be 128x128).
    // altRgba: optional second 128×128 buffer for the alternate color set.
    static bool loadFontFromPixels(const uint8_t* rgba, int width, int height,
                                   const uint8_t* altRgba = nullptr);

    // Reset to built-in hardcoded IBM CP437 font (clears alternate set too).
    static void resetToBuiltinFont();

    // ---- Frame ----
    static void begin(int screenW, int screenH);
    static void end();

    // ---- Primitives — normal set ----
    static void drawChar(int x, int y, int charCode, Vec4 color);
    static void drawString(int x, int y, const char* text, Vec4 color);
    static void drawStringShadow(int x, int y, const char* text, Vec4 color,
                                  Vec4 shadowColor = {0,0,0,0.7f},
                                  int ox = 1, int oy = 1);

    // ---- Primitives — alternate set (gold/brown in Q2 conchars) ----
    // Falls back to normal set if no alternate atlas was loaded.
    static void drawCharAlt(int x, int y, int charCode, Vec4 color);
    static void drawStringAlt(int x, int y, const char* text, Vec4 color);
    static void drawStringShadowAlt(int x, int y, const char* text, Vec4 color,
                                     Vec4 shadowColor = {0,0,0,0.7f},
                                     int ox = 1, int oy = 1);

    // ---- Fill primitives (no font needed) ----
    static void drawFill(int x, int y, int w, int h, Vec4 color);
    static void drawFillGradientH(int x, int y, int w, int h, Vec4 colorL, Vec4 colorR);

    // ---- Font metrics ----
    static constexpr int kGlyphW = 8;
    static constexpr int kGlyphH = 8;

    static void setScale(int scale);       // 1=tiny 2=normal 3=large 4=huge
    static int  getScale()    { return s_scale; }
    static int charWidth()  { return s_glyphW * s_scale; }
    static int charHeight() { return s_glyphH * s_scale; }
    static int  stringWidth(const char* text);

    // ---- State queries ----
    static bool hasAlternateSet() { return s_fontTexAlt != 0; }
    static bool usingExternalFont() { return s_usingExternalFont; }

    // ---- Internal layout constant (public for setupVAOAttribs) ----
    static constexpr int kFloatsPerVertex = 8;

private:
    // Which atlas to draw from
    enum class FontSet { Normal, Alternate };

    static void flush();
    static void flushFill();

    static void buildNativeAtlas(const uint8_t* src, int srcSize, int glyphSrc,
                                 uint8_t* atlas, int atlasW,
                                 uint8_t keyR, uint8_t keyG, uint8_t keyB,
                                 int halfOffset);

    static void buildBuiltinAtlasBuffer(uint8_t* out);  // out = 128*128*4 bytes
    static void uploadAtlasToGPU(uint32_t& texId, const uint8_t* rgba8, int w, int h);
    static void applyColorKey(const uint8_t* src, int srcSize, int glyphSrc,
                               uint8_t* atlas, int atlasDst, int glyphDst,
                               uint8_t keyR, uint8_t keyG, uint8_t keyB,
                               int halfOffset);  // halfOffset=0 for top, 8 for bottom

    static void emitQuad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         float r, float g, float b, float a);
    static void emitFillQuad(float x, float y, float w, float h,
                              float r0, float g0, float b0, float a0,
                              float r1, float g1, float b1, float a1);

    static void drawCharInternal(int x, int y, int charCode, Vec4 color, FontSet set);
    static void bindFontTex(FontSet set);

    // GL objects
    static uint32_t s_fontTex;        // normal atlas
    static uint32_t s_fontTexAlt;     // alternate atlas (0 if none)
    static uint32_t s_textProg;
    static uint32_t s_fillProg;
    static uint32_t s_vbo;
    static uint32_t s_vao;
    static uint32_t s_fillVao;

    static int s_glyphW;  // native glyph width in atlas
    static int s_glyphH;  // native glyph height in atlas

    // State
    static bool     s_initialized;
    static bool     s_inFrame;
    static int      s_screenW, s_screenH;
    static int      s_scale;
    static bool     s_usingExternalFont;
    static FontSet  s_currentFontSet;   // which atlas is currently bound

    // Vertex buffer
    static constexpr int kVertsPerQuad  = 6;
    static constexpr int kFloatsPerQuad = kVertsPerQuad * kFloatsPerVertex;
    static constexpr int kMaxQuads      = 8192;
    static float s_vbuf[kMaxQuads * kFloatsPerQuad];
    static int   s_vertexCount;
    static bool  s_inFillMode;
};

} // namespace nova