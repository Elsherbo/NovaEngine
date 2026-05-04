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
//   Both sets are stored internally as two separate atlases at
//   NATIVE glyph resolution (e.g. 32px for a 512px sheet).
//   s_scale is always 1. kDisplayGlyphSize controls the rendered
//   size in pixels: we always render at exactly kDisplayGlyphSize px
//   by setting s_glyphW/H to that value after loading.
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
    // ---- Target display size ----
    // All glyphs render at exactly this many pixels regardless of
    // source atlas resolution. Change this to resize all console text.
    // 16 = one pixel per Q2 glyph pixel at 512px sheet resolution * 0.5
    // 8  = tiny (original Q2 console size)
    // 16 = comfortable (recommended for 1080p+)
    static constexpr int kDisplayGlyphSize = 16;

    // ---- Lifecycle ----
    static void init();
    static void shutdown();

    // ---- External font loading ----
    static bool tryLoadQ2Conchars(AssetFS* assets, const char* logicalPath);
    static bool tryLoadFont(AssetFS* assets, const char* logicalPath);
    static bool loadFontFromPixels(const uint8_t* rgba, int width, int height,
                                   const uint8_t* altRgba = nullptr);
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
    static void drawCharAlt(int x, int y, int charCode, Vec4 color);
    static void drawStringAlt(int x, int y, const char* text, Vec4 color);
    static void drawStringShadowAlt(int x, int y, const char* text, Vec4 color,
                                     Vec4 shadowColor = {0,0,0,0.7f},
                                     int ox = 1, int oy = 1);

    // ---- Fill primitives (no font needed) ----
    static void drawFill(int x, int y, int w, int h, Vec4 color);
    static void drawFillGradientH(int x, int y, int w, int h, Vec4 colorL, Vec4 colorR);

    // ---- Font metrics ----
    // These always return kDisplayGlyphSize (or 8 for builtin font).
    // Use these everywhere — never hardcode pixel sizes.
    static constexpr int kGlyphW = 8;
    static constexpr int kGlyphH = 8;

    static void setScale(int scale);       // legacy: prefer kDisplayGlyphSize
    static int  getScale()    { return s_scale; }

    // charWidth/charHeight return the ACTUAL rendered pixel size.
    // For external fonts: kDisplayGlyphSize (e.g. 16).
    // For builtin font:   8 * s_scale.
    static int charWidth()  { return s_glyphW; }
    static int charHeight() { return s_glyphH; }
    static int  stringWidth(const char* text);

    // ---- State queries ----
    static bool hasAlternateSet()  { return s_fontTexAlt != 0; }
    static bool usingExternalFont(){ return s_usingExternalFont; }

    // ---- Internal layout constant (public for setupVAOAttribs) ----
    static constexpr int kFloatsPerVertex = 8;

private:
    enum class FontSet { Normal, Alternate };

    static void flush();
    static void flushFill();

    static void buildNativeAtlas(const uint8_t* src, int srcSize, int glyphSrc,
                                 uint8_t* atlas, int atlasW,
                                 uint8_t keyR, uint8_t keyG, uint8_t keyB,
                                 int halfOffset);

    static void buildBuiltinAtlasBuffer(uint8_t* out);
    static void uploadAtlasToGPU(uint32_t& texId, const uint8_t* rgba8, int w, int h);
    static void applyColorKey(const uint8_t* src, int srcSize, int glyphSrc,
                               uint8_t* atlas, int atlasDst, int glyphDst,
                               uint8_t keyR, uint8_t keyG, uint8_t keyB,
                               int halfOffset);

    static void emitQuad(float x, float y, float w, float h,
                         float u0, float v0, float u1, float v1,
                         float r, float g, float b, float a);
    static void emitFillQuad(float x, float y, float w, float h,
                              float r0, float g0, float b0, float a0,
                              float r1, float g1, float b1, float a1);

    static void drawCharInternal(int x, int y, int charCode, Vec4 color, FontSet set);
    static void bindFontTex(FontSet set);

    // GL objects
    static uint32_t s_fontTex;
    static uint32_t s_fontTexAlt;
    static uint32_t s_textProg;
    static uint32_t s_fillProg;
    static uint32_t s_vbo;
    static uint32_t s_vao;
    static uint32_t s_fillVao;

    // s_glyphW/H = the RENDERED pixel size of each character.
    // For external fonts this equals kDisplayGlyphSize.
    // For the builtin font this equals 8 * s_scale.
    static int s_glyphW;
    static int s_glyphH;

    // s_atlasGlyphW/H = the NATIVE pixel size in the GPU atlas texture.
    // UV computation uses this, not s_glyphW/H.
    static int s_atlasGlyphW;
    static int s_atlasGlyphH;

    // s_scale is kept for the builtin font path only. External fonts
    // always set s_glyphW/H = kDisplayGlyphSize and s_scale = 1.
    static int s_scale;

    // State
    static bool     s_initialized;
    static bool     s_inFrame;
    static int      s_screenW, s_screenH;
    static bool     s_usingExternalFont;
    static FontSet  s_currentFontSet;

    // Vertex buffer
    static constexpr int kVertsPerQuad  = 6;
    static constexpr int kFloatsPerQuad = kVertsPerQuad * kFloatsPerVertex;
    static constexpr int kMaxQuads      = 8192;
    static float s_vbuf[kMaxQuads * kFloatsPerQuad];
    static int   s_vertexCount;
    static bool  s_inFillMode;
};

} // namespace nova