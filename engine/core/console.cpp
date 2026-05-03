// ============================================================
// FILE:    engine/core/console.cpp
// MODULE:  Core > Console
// REDESIGN: Dark Cyan Theme — movement shooter aesthetic
//
// Design language:
//   - Near-black background (not blue-gray, genuine black)
//   - Electric cyan primary accent (#00D0FF family)
//   - 3-layer glow simulation on every structural line
//   - Corner bracket ornaments — pure HUD vocabulary
//   - Blinking status dot in header, synced to cursor
//   - ">" prompt instead of "]" — modern, clean
//   - Slim (3px) scrollbar with glow thumb
//   - Log prefix family: [WRN] [ERR] [DBG] [OK ] — equal width
//   - Shadow tinted dark-cyan, not dead black
// ============================================================

#include "engine/core/console.h"
#include "engine/core/text_2d.h"
#include "engine/core/cvar.h"
#include "engine/core/log.h"
#include "engine/platform/iplatform.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nova
{

// ---- Layout ----------------------------------------------------------------
static constexpr int   kConH       = 420;
static constexpr int   kHeaderH    = 28;    // slightly taller — room for dot
static constexpr int   kInputH     = 32;    // slightly taller — breathing room
static constexpr int   kPadX       = 14;
static constexpr int   kPadY       = 6;
static constexpr int   kScrollW    = 3;     // slim scrollbar
static constexpr int   kScrollGap  = 8;
static constexpr int   kCornerLen  = 22;    // corner bracket arm length
static constexpr int   kBorderW    = 2;     // corner bracket thickness
static constexpr float kSlideSpeed = 18.0f; // snappier

// ---- Background layers -----------------------------------------------------
// Near-pure black with faint cool tint — not the old blue-gray
static constexpr Vec4 kBgMain      = { 0.018f, 0.020f, 0.030f, 0.97f };
// Header: even darker so it reads as a separate zone
static constexpr Vec4 kBgHeader    = { 0.008f, 0.009f, 0.015f, 1.00f };
// Header gradient destination — subtle blue-black warmth
static constexpr Vec4 kBgHeaderMid = { 0.018f, 0.025f, 0.040f, 1.00f };
// Input area: deepest black — anchors the bottom
static constexpr Vec4 kBgInput     = { 0.006f, 0.007f, 0.012f, 1.00f };
// Scanlines: every 3rd pixel, very faint
static constexpr Vec4 kBgScanL     = { 0.000f, 0.000f, 0.000f, 0.08f };

// ---- Border / accent colors ------------------------------------------------
// Primary: electric cyan — the main accent on everything structural
static constexpr Vec4 kBorderCyan    = { 0.000f, 0.816f, 1.000f, 0.90f };
// Dim variant: for the status dot off-state and subdued lines
static constexpr Vec4 kBorderCyanDim = { 0.000f, 0.420f, 0.560f, 0.50f };
// Dark structural: for separator inset lines, scrollbar track edge
static constexpr Vec4 kBorderDark    = { 0.100f, 0.140f, 0.180f, 0.35f };

// ---- Scrollbar -------------------------------------------------------------
static constexpr Vec4 kScrollTrack = { 0.028f, 0.034f, 0.048f, 1.00f };
static constexpr Vec4 kScrollThumb = { 0.000f, 0.660f, 0.840f, 0.88f };

// ---- Log text colors — vibrant, distinct, intentional ----------------------
// Each slot maps to ConColor enum (0-7).
// Designed so every level is immediately readable at a glance:
//   Out   = near-white      (most common, must be easy on the eyes)
//   Cmd   = electric cyan   (same as accent — echoed commands stand out)
//   Err   = hot red/pink    (alarming, not just dark red)
//   Warn  = electric yellow (bright, not muddy orange)
//   Sys   = mint green      (system/engine messages — calm but distinct)
//   Suc   = neon green      (positive outcome — brighter than system)
//   Dim   = muted gray      (old lines, hints — recedes visually)
//   Acc   = electric cyan   (alias of Cmd for highlights)
static constexpr Vec4 kColTable[] = {
    { 0.900f, 0.904f, 0.920f, 1.00f }, // 0 Output  — near white
    { 0.000f, 0.840f, 1.000f, 1.00f }, // 1 Command — electric cyan
    { 1.000f, 0.200f, 0.345f, 1.00f }, // 2 Error   — hot red/pink
    { 1.000f, 0.855f, 0.000f, 1.00f }, // 3 Warn    — electric yellow
    { 0.360f, 0.910f, 0.610f, 1.00f }, // 4 System  — mint green
    { 0.160f, 1.000f, 0.590f, 1.00f }, // 5 Success — neon green
    { 0.330f, 0.345f, 0.400f, 0.80f }, // 6 Dim     — muted gray
    { 0.000f, 0.840f, 1.000f, 1.00f }, // 7 Accent  — electric cyan
};

// ---- Special-purpose text colors -------------------------------------------
static constexpr Vec4 kColHeader  = { 0.000f, 0.840f, 1.000f, 1.00f }; // header title
static constexpr Vec4 kColVersion = { 0.240f, 0.340f, 0.450f, 1.00f }; // version string
static constexpr Vec4 kColPrompt  = { 0.000f, 0.840f, 1.000f, 1.00f }; // input ">"
static constexpr Vec4 kColInput   = { 0.936f, 0.940f, 0.952f, 1.00f }; // input text
// Shadow tinted dark-cyan instead of dead black — richer depth
static constexpr Vec4 kShadow     = { 0.000f, 0.038f, 0.076f, 0.88f };

// ---- Helper: draw a "glowing" horizontal line ------------------------------
// Three overlapping fills simulate a neon glow without any shader magic:
//   outer halo  (wide,  very transparent)
//   inner halo  (medium, partly transparent)
//   core line   (narrow, nearly opaque)
// All coordinates are in screen pixels.
static void drawGlowLineH(int x, int y, int w, const Vec4& cyanBase, bool above)
{
    // Glow spreads toward 'above' side (true) or below (false).
    const int spread = above ? -1 : 1;
    Text2D::drawFill(x, y + spread * 3, w, 4, Vec4{ cyanBase.x, cyanBase.y, cyanBase.z, 0.040f });
    Text2D::drawFill(x, y + spread * 1, w, 3, Vec4{ cyanBase.x, cyanBase.y, cyanBase.z, 0.140f });
    Text2D::drawFill(x, y,              w, 2, Vec4{ cyanBase.x, cyanBase.y, cyanBase.z, 0.880f });
}

// ============================================================
Console::Console()  = default;
Console::~Console() = default;

// ---- addLogLine ------------------------------------------------------------
// Strip the Logger timestamp/level prefix, re-tag with our own short codes.
void Console::addLogLine(LogLevel level, const char* rawMsg)
{
    if (!rawMsg) return;

    // Logger emits: "[timestamp] [LEVEL] message"
    // Strip two "] " occurrences to reach the raw message.
    const char* msg = rawMsg;
    const char* p   = rawMsg;
    int brackets    = 0;
    while (*p)
    {
        if (*p == ']' && *(p + 1) == ' ')
        {
            if (++brackets == 2) { msg = p + 2; break; }
        }
        ++p;
    }

    ConColor col;
    switch (level)
    {
    case LogLevel::Debug: col = ConColor::Dim;    break;
    case LogLevel::Info:  col = ConColor::Output; break;
    case LogLevel::Warn:  col = ConColor::Warn;   break;
    case LogLevel::Error: col = ConColor::Error;  break;
    default:              col = ConColor::Output; break;
    }

    // Short, fixed-width prefixes — all 5 chars so log body aligns.
    // [WRN] [ERR] [DBG] and [OK ] (space-padded) give a consistent gutter.
    char tagged[1100];
    switch (level)
    {
    case LogLevel::Warn:  snprintf(tagged, sizeof(tagged), "[WRN] %s", msg); break;
    case LogLevel::Error: snprintf(tagged, sizeof(tagged), "[ERR] %s", msg); break;
    case LogLevel::Debug: snprintf(tagged, sizeof(tagged), "[DBG] %s", msg); break;
    default:              snprintf(tagged, sizeof(tagged), "%s", msg);         break;
    }

    const int cw      = Text2D::charWidth();
    // Account for new constants when computing wrap width.
    const int maxCols = (1280 - kPadX * 2 - kScrollW - kScrollGap) / cw;
    wrapAndAdd(tagged, col, maxCols > 10 ? maxCols : 78);
}

// ---- wrapAndAdd / addLine --------------------------------------------------
void Console::wrapAndAdd(const std::string& text, ConColor color, int maxCols)
{
    if (text.empty()) { addLine("", color); return; }
    if ((int)text.size() <= maxCols) { addLine(text, color); return; }

    int  start = 0;
    bool first = true;
    while (start < (int)text.size())
    {
        int cap   = first ? maxCols : maxCols - 2;
        int avail = (int)text.size() - start;
        if (avail <= cap)
        {
            addLine(first ? text.substr(start) : "  " + text.substr(start), color);
            break;
        }
        int breakAt = start + cap;
        int spaceAt = -1;
        for (int i = breakAt; i > start; --i)
            if (text[i] == ' ') { spaceAt = i; break; }
        if (spaceAt < 0) spaceAt = breakAt;
        addLine(first ? text.substr(start, spaceAt - start)
                      : "  " + text.substr(start, spaceAt - start), color);
        start = spaceAt + (text[spaceAt] == ' ' ? 1 : 0);
        first = false;
    }
}

void Console::addLine(const std::string& line, ConColor color)
{
    m_scrollback.push_back({ line, color });
    if ((int)m_scrollback.size() > kMaxLines)
        m_scrollback.erase(m_scrollback.begin());
}

// ---- submit ----------------------------------------------------------------
void Console::submit(const char* line)
{
    if (!line || !*line) return;

    // Echo the command in cyan with ">" prefix — matches the input prompt style.
    addLine(std::string(">  ") + line, ConColor::Command);

    // History dedup
    if (m_historyCount == 0 || m_history[0] != line)
    {
        for (int i = kMaxHistory - 1; i > 0; --i)
            m_history[i] = m_history[i - 1];
        m_history[0] = line;
        if (m_historyCount < kMaxHistory) ++m_historyCount;
    }
    m_historyIdx = -1;

    CVarSystem::instance().exec(line);
    m_scroll = 0;
}

// ---- handleInput -----------------------------------------------------------
void Console::handleInput(const InputState& cur, const InputState& prev)
{
    const bool grave = cur.keys[SDL_SCANCODE_GRAVE];
    if (grave && !m_prevGrave)
    {
        toggle();
        if (m_mouseGrabCallback) m_mouseGrabCallback(!m_open);
    }
    m_prevGrave = grave;

    if (!m_open) return;

    if (cur.keys[SDL_SCANCODE_ESCAPE] && !prev.keys[SDL_SCANCODE_ESCAPE])
    {
        close();
        if (m_mouseGrabCallback) m_mouseGrabCallback(true);
        return;
    }
    if (cur.keys[SDL_SCANCODE_RETURN] && !prev.keys[SDL_SCANCODE_RETURN])
    {
        if (!m_inputBuf.empty()) submit(m_inputBuf.c_str());
        m_inputBuf.clear();
        m_inputPos = 0;
        return;
    }
    if (cur.keys[SDL_SCANCODE_UP] && !prev.keys[SDL_SCANCODE_UP])
    {
        if (m_historyIdx < m_historyCount - 1)
        { m_inputBuf = m_history[++m_historyIdx]; m_inputPos = (int)m_inputBuf.size(); }
        return;
    }
    if (cur.keys[SDL_SCANCODE_DOWN] && !prev.keys[SDL_SCANCODE_DOWN])
    {
        if (m_historyIdx > 0)
        { m_inputBuf = m_history[--m_historyIdx]; m_inputPos = (int)m_inputBuf.size(); }
        else if (m_historyIdx == 0)
        { m_historyIdx = -1; m_inputBuf.clear(); m_inputPos = 0; }
        return;
    }

    const int ch2    = Text2D::charHeight();
    const int lineH  = ch2 + 2;
    const int textH  = kConH - kHeaderH - 1 - kInputH - kPadY * 2;
    const int lpp    = std::max(1, textH / lineH);

    if (cur.keys[SDL_SCANCODE_PAGEUP]   && !prev.keys[SDL_SCANCODE_PAGEUP])
    { m_scroll = std::min(m_scroll + lpp, (int)m_scrollback.size()); return; }
    if (cur.keys[SDL_SCANCODE_PAGEDOWN] && !prev.keys[SDL_SCANCODE_PAGEDOWN])
    { m_scroll = std::max(m_scroll - lpp, 0); return; }
    if (cur.keys[SDL_SCANCODE_BACKSPACE] && !prev.keys[SDL_SCANCODE_BACKSPACE])
    { if (m_inputPos > 0) { m_inputBuf.erase(m_inputPos - 1, 1); --m_inputPos; } return; }
    if (cur.keys[SDL_SCANCODE_DELETE] && !prev.keys[SDL_SCANCODE_DELETE])
    {
        if (m_inputPos < (int)m_inputBuf.size()) m_inputBuf.erase(m_inputPos, 1);
        return;
    }
    if (cur.keys[SDL_SCANCODE_HOME]  && !prev.keys[SDL_SCANCODE_HOME])  { m_inputPos = 0; return; }
    if (cur.keys[SDL_SCANCODE_END]   && !prev.keys[SDL_SCANCODE_END])   { m_inputPos = (int)m_inputBuf.size(); return; }
    if (cur.keys[SDL_SCANCODE_LEFT]  && !prev.keys[SDL_SCANCODE_LEFT])  { if (m_inputPos > 0) --m_inputPos; return; }
    if (cur.keys[SDL_SCANCODE_RIGHT] && !prev.keys[SDL_SCANCODE_RIGHT]) { if (m_inputPos < (int)m_inputBuf.size()) ++m_inputPos; return; }
    if (cur.keys[SDL_SCANCODE_TAB]   && !prev.keys[SDL_SCANCODE_TAB])   return;

    const bool shift = cur.keys[SDL_SCANCODE_LSHIFT] || cur.keys[SDL_SCANCODE_RSHIFT];
    struct KM { int sc; char p, s; };
    static const KM kMap[] = {
        {SDL_SCANCODE_A,'a','A'},{SDL_SCANCODE_B,'b','B'},{SDL_SCANCODE_C,'c','C'},
        {SDL_SCANCODE_D,'d','D'},{SDL_SCANCODE_E,'e','E'},{SDL_SCANCODE_F,'f','F'},
        {SDL_SCANCODE_G,'g','G'},{SDL_SCANCODE_H,'h','H'},{SDL_SCANCODE_I,'i','I'},
        {SDL_SCANCODE_J,'j','J'},{SDL_SCANCODE_K,'k','K'},{SDL_SCANCODE_L,'l','L'},
        {SDL_SCANCODE_M,'m','M'},{SDL_SCANCODE_N,'n','N'},{SDL_SCANCODE_O,'o','O'},
        {SDL_SCANCODE_P,'p','P'},{SDL_SCANCODE_Q,'q','Q'},{SDL_SCANCODE_R,'r','R'},
        {SDL_SCANCODE_S,'s','S'},{SDL_SCANCODE_T,'t','T'},{SDL_SCANCODE_U,'u','U'},
        {SDL_SCANCODE_V,'v','V'},{SDL_SCANCODE_W,'w','W'},{SDL_SCANCODE_X,'x','X'},
        {SDL_SCANCODE_Y,'y','Y'},{SDL_SCANCODE_Z,'z','Z'},
        {SDL_SCANCODE_0,'0',')'},{SDL_SCANCODE_1,'1','!'},{SDL_SCANCODE_2,'2','@'},
        {SDL_SCANCODE_3,'3','#'},{SDL_SCANCODE_4,'4','$'},{SDL_SCANCODE_5,'5','%'},
        {SDL_SCANCODE_6,'6','^'},{SDL_SCANCODE_7,'7','&'},{SDL_SCANCODE_8,'8','*'},
        {SDL_SCANCODE_9,'9','('},
        {SDL_SCANCODE_SPACE,' ',' '},{SDL_SCANCODE_MINUS,'-','_'},
        {SDL_SCANCODE_EQUALS,'=','+'},{SDL_SCANCODE_LEFTBRACKET,'[','{'},
        {SDL_SCANCODE_RIGHTBRACKET,']','}'},{SDL_SCANCODE_SEMICOLON,';',':'},
        {SDL_SCANCODE_APOSTROPHE,'\'','"'},{SDL_SCANCODE_COMMA,',','<'},
        {SDL_SCANCODE_PERIOD,'.','>'},{SDL_SCANCODE_SLASH,'/','?'},
        {SDL_SCANCODE_BACKSLASH,'\\','|'},
    };
    for (const auto& m : kMap)
    {
        if (cur.keys[m.sc] && !prev.keys[m.sc] && (int)m_inputBuf.size() < 255)
        {
            char c = shift ? m.s : m.p;
            m_inputBuf.insert(m_inputPos, 1, c);
            ++m_inputPos;
        }
    }
}

// ---- render ----------------------------------------------------------------
void Console::render(int screenW, int screenH)
{
    // ---- Delta time for animation ------------------------------------------
    static uint64_t s_lastTime = 0;
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (s_lastTime == 0)
               ? 0.016f
               : (float)((double)(now - s_lastTime) / SDL_GetPerformanceFrequency());
    if (dt > 0.1f) dt = 0.1f;
    s_lastTime = now;

    // ---- Slide animation ---------------------------------------------------
    float target = m_open ? 1.0f : 0.0f;
    float diff   = target - m_slideT;
    if (std::abs(diff) > 0.001f)
        m_slideT += diff * kSlideSpeed * dt;
    else
        m_slideT = target;

    if (m_slideT <= 0.001f) return;

    // ---- Layout geometry ---------------------------------------------------
    const int consoleH = (int)(kConH * m_slideT);
    const int headerH  = (int)(kHeaderH * m_slideT);
    const int inputY   = consoleH - kInputH;
    const int cw       = Text2D::charWidth();
    const int ch       = Text2D::charHeight();

    Text2D::init();
    Text2D::begin(screenW, screenH);

    // ================================================================
    // PASS 1 — BACKGROUNDS
    // Draw from back to front so later layers composite correctly.
    // ================================================================

    // Main console body — near-black
    Text2D::drawFill(0, 0, screenW, consoleH, kBgMain);

    // Header zone — even darker, establishes visual separation
    if (headerH > 0)
        Text2D::drawFillGradientH(0, 0, screenW, headerH, kBgHeader, kBgHeaderMid);

    // Input zone — deepest
    if (inputY > 0 && inputY < consoleH)
        Text2D::drawFill(0, inputY, screenW, kInputH, kBgInput);

    // ================================================================
    // PASS 2 — SCANLINES
    // Fine horizontal lines every 3 pixels across the text area only.
    // Only rendered when mostly open (avoids visual noise during slide).
    // ================================================================
    if (m_slideT > 0.65f)
    {
        const int scanStart = headerH + 3;
        const int scanEnd   = inputY > 0 ? inputY : consoleH;
        for (int sy = scanStart; sy < scanEnd; sy += 3)
            Text2D::drawFill(0, sy, screenW - kScrollW - kScrollGap, 1, kBgScanL);
    }

    // ================================================================
    // PASS 3 — STRUCTURAL LINES (glowing)
    // Header separator and input separator are the primary visual
    // anchors. Each uses 3 layers (outer halo, inner halo, core).
    // ================================================================

    // Header separator — glow spreads downward
    if (headerH > 0)
        drawGlowLineH(0, headerH, screenW, kBorderCyan, false);

    // Input separator — glow spreads upward
    if (inputY > 0)
        drawGlowLineH(0, inputY, screenW, kBorderCyan, true);

    // Bottom border — glow spreads upward
    drawGlowLineH(0, consoleH - 2, screenW, kBorderCyan, true);

    // ================================================================
    // PASS 4 — LEFT + RIGHT EDGE ACCENTS
    // Thin cyan strips that frame the text area vertically.
    // Provides a visual anchor and reinforces the HUD aesthetic.
    // ================================================================
    if (inputY > headerH)
    {
        const int edgeTop = headerH + 3;
        const int edgeBot = inputY - 1;
        const int edgeH   = edgeBot - edgeTop;
        if (edgeH > 0)
        {
            // Left — 2px wide, slightly transparent
            Text2D::drawFill(0, edgeTop, 2, edgeH, Vec4{ 0.000f, 0.816f, 1.0f, 0.38f });
            Text2D::drawFill(2, edgeTop, 1, edgeH, Vec4{ 0.000f, 0.816f, 1.0f, 0.08f });

            // Right — mirror, sits just left of scrollbar gap
            const int rX = screenW - kScrollW - kScrollGap - 1;
            Text2D::drawFill(rX, edgeTop, 1, edgeH, Vec4{ 0.000f, 0.816f, 1.0f, 0.08f });
            Text2D::drawFill(rX + 1, edgeTop, 2, edgeH, Vec4{ 0.000f, 0.816f, 1.0f, 0.38f });
        }
    }

    // ================================================================
    // PASS 5 — CORNER BRACKET ORNAMENTS
    // L-shaped corner marks at all four corners of the console.
    // These are pure HUD vocabulary — cheap, effective, iconic.
    // Outer glow + core for the classic neon-border look.
    // ================================================================
    {
        // Helper: draw one L-shaped corner bracket
        // ox, oy = corner origin; hSign = +1 right, -1 left; vSign = +1 down, -1 up
        auto corner = [&](int ox, int oy, int hSign, int vSign)
        {
            // Glow layer (1px wider/taller, low alpha)
            Text2D::drawFill(ox - (hSign < 0 ? 1 : 0),
                             oy - (vSign < 0 ? 1 : 0),
                             kCornerLen + 1,
                             kBorderW + 1,
                             Vec4{ 0.f, 0.816f, 1.f, 0.20f });
            Text2D::drawFill(ox - (hSign < 0 ? 1 : 0),
                             oy - (vSign < 0 ? 1 : 0),
                             kBorderW + 1,
                             kCornerLen + 1,
                             Vec4{ 0.f, 0.816f, 1.f, 0.20f });
            // Core
            Text2D::drawFill(hSign > 0 ? ox : ox - kCornerLen + kBorderW,
                             vSign > 0 ? oy : oy,
                             kCornerLen, kBorderW, kBorderCyan);
            Text2D::drawFill(hSign > 0 ? ox : ox - kCornerLen + kBorderW,
                             vSign > 0 ? oy : oy - kCornerLen + kBorderW,
                             kBorderW, kCornerLen, kBorderCyan);
        };

        // Top-left
        corner(0, 0, 1, 1);
        // Top-right
        Text2D::drawFill(screenW - kCornerLen, 0, kCornerLen, kBorderW, kBorderCyan);
        Text2D::drawFill(screenW - kBorderW,  0, kBorderW, kCornerLen, kBorderCyan);
        Text2D::drawFill(screenW - kCornerLen - 1, 0, kCornerLen + 1, kBorderW + 1,
                         Vec4{ 0.f, 0.816f, 1.f, 0.20f });
        Text2D::drawFill(screenW - kBorderW - 1, 0, kBorderW + 1, kCornerLen + 1,
                         Vec4{ 0.f, 0.816f, 1.f, 0.20f });

        // Bottom corners only when the console is tall enough to show them
        if (consoleH >= kCornerLen * 2 + 4)
        {
            // Bottom-left
            Text2D::drawFill(0, consoleH - kBorderW, kCornerLen, kBorderW, kBorderCyan);
            Text2D::drawFill(0, consoleH - kCornerLen, kBorderW, kCornerLen, kBorderCyan);
            Text2D::drawFill(0, consoleH - kBorderW - 1, kCornerLen + 1, kBorderW + 1,
                             Vec4{ 0.f, 0.816f, 1.f, 0.20f });
            Text2D::drawFill(0, consoleH - kCornerLen - 1, kBorderW + 1, kCornerLen + 1,
                             Vec4{ 0.f, 0.816f, 1.f, 0.20f });
            // Bottom-right
            Text2D::drawFill(screenW - kCornerLen, consoleH - kBorderW, kCornerLen, kBorderW, kBorderCyan);
            Text2D::drawFill(screenW - kBorderW,  consoleH - kCornerLen, kBorderW, kCornerLen, kBorderCyan);
            Text2D::drawFill(screenW - kCornerLen - 1, consoleH - kBorderW - 1, kCornerLen + 1, kBorderW + 1,
                             Vec4{ 0.f, 0.816f, 1.f, 0.20f });
            Text2D::drawFill(screenW - kBorderW - 1, consoleH - kCornerLen - 1, kBorderW + 1, kCornerLen + 1,
                             Vec4{ 0.f, 0.816f, 1.f, 0.20f });
        }
    }

    // ================================================================
    // PASS 6 — HEADER CONTENT
    // Status dot | engine name | vertical rule | version (right-aligned)
    // Status dot blinks in sync with the cursor — the console "breathes".
    // ================================================================
    if (headerH > 0 && m_slideT > 0.4f)
    {
        const int ty = (headerH - ch) / 2;
        if (ty >= 0 && ty + ch <= headerH)
        {
            // Status dot — 7×7, with outer glow
            const int dotSz = 7;
            const int dotX  = kPadX;
            const int dotY  = ty + (ch - dotSz) / 2;
            // Outer glow (always, constant size)
            Text2D::drawFill(dotX - 2, dotY - 2, dotSz + 4, dotSz + 4,
                             Vec4{ 0.f, 0.816f, 1.f, 0.12f });
            // Dot body — blinks between bright and dim
            const Vec4 dotCol = m_cursorVis ? kBorderCyan : kBorderCyanDim;
            Text2D::drawFill(dotX, dotY, dotSz, dotSz, dotCol);

            // Engine name with drop shadow (cyan-tinted shadow)
            const int nameX = kPadX + dotSz + 9;
            Text2D::drawStringShadow(nameX, ty, "NOVA ENGINE", kColHeader, kShadow, 1, 1);

            // Thin vertical separator between name and version
            const int nameEnd = nameX + Text2D::stringWidth("NOVA ENGINE");
            const int sepX    = nameEnd + 10;
            Text2D::drawFill(sepX, ty + 2,     1, ch - 4, Vec4{ 0.1f, 0.18f, 0.26f, 0.7f });
            Text2D::drawFill(sepX + 1, ty + 2, 1, ch - 4, Vec4{ 0.05f, 0.10f, 0.15f, 0.3f });

            // Version — right-aligned, muted
            const char* ver = "v0.1.0  ·  OpenGL 4.5";
            const int verW  = Text2D::stringWidth(ver);
            Text2D::drawString(screenW - verW - kPadX, ty, ver, kColVersion);
        }
    }

    // ================================================================
    // PASS 7 — SCROLLBACK TEXT
    // Lines are drawn oldest-to-newest, bottom-up within the text area.
    // Shadow is drawn first, then the colored text on top.
    // ================================================================
    {
        const int lineH     = ch + 2;
        const int textTop   = headerH + 4 + kPadY;
        const int textBot   = inputY > 0 ? inputY - kPadY : consoleH - kPadY;
        const int textH2    = textBot - textTop;
        const int maxVis    = std::max(0, textH2 / lineH);

        const int total   = (int)m_scrollback.size();
        int endIdx        = total - m_scroll;
        int startIdx      = endIdx - maxVis;
        if (startIdx < 0) startIdx = 0;
        if (endIdx > total) endIdx = total;

        // Available width for text (left pad + scrollbar + right scrollbar gap)
        const int textAreaW = screenW - kPadX * 2 - kScrollW - kScrollGap * 2 - 4;

        for (int i = startIdx; i < endIdx; ++i)
        {
            const ConLine& entry = m_scrollback[i];
            const int lineY = textTop + (i - startIdx) * lineH;
            if (lineY + ch > textBot) break;

            const Vec4& color   = kColTable[(int)entry.color];
            const int maxChars  = textAreaW / cw;

            const char* txt = entry.text.c_str();
            std::string trunc;
            if ((int)entry.text.size() > maxChars && maxChars > 3)
            {
                trunc = entry.text.substr(0, maxChars - 1) + "~";
                txt   = trunc.c_str();
            }

            // Shadow then color
            Text2D::drawString(kPadX + 1, lineY + 1, txt,
                               Vec4{ kShadow.x, kShadow.y, kShadow.z, color.w * 0.55f });
            Text2D::drawString(kPadX,     lineY,     txt, color);
        }

        // ================================================================
        // PASS 8 — SCROLLBAR
        // Slim 3-pixel track. Thumb has a subtle glow.
        // ================================================================
        if (total > maxVis)
        {
            const int trackX = screenW - kScrollW - 3;
            const int trackY = textTop;
            const int trackH = textH2;

            // Track
            Text2D::drawFill(trackX, trackY, kScrollW, trackH, kScrollTrack);
            // Left edge of track — subtle separator
            Text2D::drawFill(trackX - 1, trackY, 1, trackH, kBorderDark);

            const float visFrac    = (float)maxVis / (float)total;
            const int   thumbH     = std::max(14, (int)(visFrac * (float)trackH));
            const float scrollFrac = (total > maxVis)
                                     ? (float)m_scroll / (float)(total - maxVis)
                                     : 0.f;
            int thumbY = trackY + (int)((1.f - scrollFrac) * (float)(trackH - thumbH));
            thumbY = std::max(trackY, std::min(thumbY, trackY + trackH - thumbH));

            // Thumb glow (1px wider each side, low alpha)
            Text2D::drawFill(trackX - 1, thumbY, kScrollW + 2, thumbH,
                             Vec4{ 0.f, 0.66f, 0.84f, 0.22f });
            // Thumb core
            Text2D::drawFill(trackX, thumbY, kScrollW, thumbH, kScrollThumb);
        }

        // ================================================================
        // PASS 9 — SCROLL HINT BANNER
        // Shown when the user has scrolled up. Styled as a small pill
        // with cyan border lines above and below — minimal, not loud.
        // ================================================================
        if (m_scroll > 0 && textH2 > lineH * 3)
        {
            const char* hint  = "  PageUp / PageDn  ";
            const int hintW   = Text2D::stringWidth(hint);
            const int hintX   = (screenW - hintW) / 2;
            const int hintY   = textBot - lineH - 3;
            if (hintY > textTop + lineH * 2)
            {
                // Pill background (dark teal)
                Text2D::drawFill(hintX - 8, hintY - 4, hintW + 16, lineH + 6,
                                 Vec4{ 0.0f, 0.14f, 0.20f, 0.88f });
                // Top / bottom border lines on the pill
                Text2D::drawFill(hintX - 8, hintY - 4,             hintW + 16, 1,
                                 Vec4{ 0.f, 0.816f, 1.f, 0.50f });
                Text2D::drawFill(hintX - 8, hintY + lineH + 2,     hintW + 16, 1,
                                 Vec4{ 0.f, 0.816f, 1.f, 0.50f });
                // Text
                Text2D::drawString(hintX, hintY, hint,
                                   Vec4{ 0.f, 0.840f, 1.f, 0.85f });
            }
        }
    }

    // ================================================================
    // PASS 10 — INPUT AREA
    // Prompt ">" in cyan, input text in near-white.
    // Cursor:  ON phase  → filled block with outer glow, char inverted
    //          OFF phase → thin 1px line (not invisible — just resting)
    // ================================================================
    if (inputY > 0)
    {
        const int padV = (kInputH - ch) / 2;
        const int iy   = inputY + padV;

        // Prompt
        const char* prompt = ">  ";
        Text2D::drawStringShadow(kPadX, iy, prompt, kColPrompt, kShadow, 1, 1);
        const int promptW   = Text2D::stringWidth(prompt);
        const int cursorBaseX = kPadX + promptW;

        // Input text split at cursor position
        if (!m_inputBuf.empty())
        {
            const std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
            const std::string after  = m_inputBuf.substr((size_t)m_inputPos);

            Text2D::drawString(cursorBaseX + 1, iy + 1, before.c_str(),
                               Vec4{ 0.f, 0.f, 0.f, 0.45f });
            Text2D::drawString(cursorBaseX,     iy,     before.c_str(), kColInput);

            const int afterX = cursorBaseX + Text2D::stringWidth(before.c_str());
            Text2D::drawString(afterX + 1, iy + 1, after.c_str(),
                               Vec4{ 0.f, 0.f, 0.f, 0.45f });
            Text2D::drawString(afterX,     iy,     after.c_str(), kColInput);
        }

        // Cursor blink
        m_blinkAccum += dt;
        if (m_blinkAccum > 0.53f) { m_blinkAccum = 0.f; m_cursorVis = !m_cursorVis; }

        {
            const std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
            const int cx = cursorBaseX + Text2D::stringWidth(before.c_str());

            if (m_cursorVis)
            {
                // ON phase: solid block with outer glow + inverted character
                Text2D::drawFill(cx - 1, iy - 1, cw + 2, ch + 2,
                                 Vec4{ 0.f, 0.816f, 1.f, 0.20f });
                Text2D::drawFill(cx, iy, cw, ch,
                                 Vec4{ 0.f, 0.816f, 1.f, 0.84f });
                if (m_inputPos < (int)m_inputBuf.size())
                {
                    char buf[2] = { m_inputBuf[m_inputPos], '\0' };
                    // Character rendered dark so it reads against cyan block
                    Text2D::drawString(cx, iy, buf, Vec4{ 0.02f, 0.04f, 0.06f, 1.0f });
                }
            }
            else
            {
                // OFF phase: thin 1px line — cursor is resting, not gone
                Text2D::drawFill(cx, iy, 1, ch,
                                 Vec4{ 0.f, 0.816f, 1.f, 0.42f });
            }
        }
    }

    Text2D::end();
}

} // namespace nova