// ============================================================
// FILE:    engine/core/console.cpp
// MODULE:  Core > Console
// VERSION: v8 — Retro Shooter Aesthetic
//
// VISUAL DESIGN — "Military Terminal"
// ─────────────────────────────────────────────────────────────
//
//  ┌──────────────────────────────────────────────────────────┐
//  │  ████ NOVA ENGINE ████████████████████ v0.1.0  FPS:xx  │  ← Header (gradient, amber accent)
//  ├──────────────────────────────────────────────────────────┤  ← 1px amber border
//  │                                                          │
//  │  [INFO]  Nova Engine Phase 1 initializing...            │  ← Scrollback (dim tint background)
//  │  [INFO]  BSP loaded, spawn at (...)                     │
//  │  [WARN]  CVar 'r_debugview' set to 1                    │
//  │                                                   ╏     │  ← 4px scrollbar (right edge)
//  │                                                   ╏     │
//  ├──────────────────────────────────────────────────────────┤  ← 1px amber border
//  │ ]  type command here_                                    │  ← Input bar (darker bg)
//  └──────────────────────────────────────────────────────────┘
//
// Color palette (linear light values, sRGB framebuffer handles encode):
//   Background:  #0A0C10  → {0.039, 0.047, 0.063, 0.93}  very dark navy-black
//   Header bg:   #0F1318  → {0.059, 0.075, 0.094, 1.00}  slightly lighter
//   Input bg:    #060709  → {0.024, 0.027, 0.035, 1.00}  darker than main
//   Border:      #C87820  → {0.784, 0.471, 0.125, 0.90}  warm amber
//   Scrollbar:   #1A1E26  → track, #C87820 → thumb
//   Text white:  #D8D0C0  → {0.847, 0.816, 0.753, 1.00}  warm off-white (CRT)
//   Command:     #E8940A  → {0.910, 0.580, 0.039, 1.00}  amber
//   Error:       #E83020  → {0.910, 0.188, 0.125, 1.00}  red-orange
//   Warn:        #E8C020  → {0.910, 0.753, 0.125, 1.00}  yellow
//   System:      #20C8C8  → {0.125, 0.784, 0.784, 1.00}  cyan
//   Success:     #40D840  → {0.251, 0.847, 0.251, 1.00}  green
//   Dim:         #484440  → {0.282, 0.267, 0.251, 1.00}  warm dark gray
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

// ============================================================
// Layout constants
// ============================================================
static constexpr int   kConH        = 420;   // console open height (px)
static constexpr int   kHeaderH     = 24;    // title bar height
static constexpr int   kInputH      = 30;    // input bar height
static constexpr int   kPadX        = 10;    // left/right text padding
static constexpr int   kPadY        = 5;     // top/bottom text padding within text area
static constexpr int   kScrollW     = 5;     // scrollbar width
static constexpr int   kScrollGap   = 3;     // gap between text and scrollbar
static constexpr float kSlideSpeed  = 14.0f; // slide-in speed multiplier

// ============================================================
// Color palette — all linear values
// ============================================================

// Backgrounds
static constexpr Vec4 kBgMain    = { 0.039f, 0.047f, 0.063f, 0.93f }; // very dark navy-black
static constexpr Vec4 kBgHeader  = { 0.059f, 0.075f, 0.094f, 1.00f }; // slightly lighter
static constexpr Vec4 kBgHeaderR = { 0.094f, 0.063f, 0.020f, 1.00f }; // gradient right (amber tint)
static constexpr Vec4 kBgInput   = { 0.020f, 0.024f, 0.031f, 1.00f }; // darkest — input area
static constexpr Vec4 kBgScanL   = { 0.000f, 0.000f, 0.000f, 0.06f }; // scanline even rows
static constexpr Vec4 kBgScanD   = { 0.000f, 0.000f, 0.000f, 0.00f }; // scanline odd rows (transparent)

// Borders / decorators
static constexpr Vec4 kBorderAmber = { 0.784f, 0.471f, 0.125f, 0.90f }; // amber accent border
static constexpr Vec4 kBorderDim   = { 0.200f, 0.220f, 0.260f, 0.60f }; // dim blue-gray divider

// Scrollbar
static constexpr Vec4 kScrollTrack = { 0.067f, 0.078f, 0.098f, 1.00f };
static constexpr Vec4 kScrollThumb = { 0.620f, 0.373f, 0.098f, 0.90f }; // amber thumb

// Text colors by semantic category
// These are LINEAR values (sRGB framebuffer encodes automatically)
static constexpr Vec4 kColTable[] = {
    { 0.847f, 0.816f, 0.753f, 1.00f }, // Output  — warm off-white (CRT phosphor)
    { 0.910f, 0.580f, 0.039f, 1.00f }, // Command — amber
    { 0.910f, 0.188f, 0.125f, 1.00f }, // Error   — red-orange
    { 0.910f, 0.753f, 0.125f, 1.00f }, // Warn    — yellow
    { 0.125f, 0.784f, 0.784f, 1.00f }, // System  — cyan
    { 0.251f, 0.847f, 0.251f, 1.00f }, // Success — green
    { 0.282f, 0.267f, 0.251f, 1.00f }, // Dim     — warm gray
    { 1.000f, 0.720f, 0.200f, 1.00f }, // Accent  — bright amber
};

// Header text / prompt
static constexpr Vec4 kColHeader  = { 0.910f, 0.580f, 0.039f, 1.00f }; // amber
static constexpr Vec4 kColVersion = { 0.400f, 0.440f, 0.520f, 1.00f }; // cool gray
static constexpr Vec4 kColPrompt  = { 0.910f, 0.580f, 0.039f, 1.00f }; // amber prompt
static constexpr Vec4 kColCursor  = { 0.910f, 0.720f, 0.200f, 1.00f }; // bright amber cursor
static constexpr Vec4 kColInput   = { 0.847f, 0.816f, 0.753f, 1.00f }; // warm white input text

// Shadow for drop-shadow text
static constexpr Vec4 kShadow     = { 0.000f, 0.000f, 0.000f, 0.70f };

// ============================================================
// Constructor / Destructor
// ============================================================
Console::Console()  = default;
Console::~Console() = default;

// ============================================================
// addLogLine — strip timestamp, classify, wrap, add
// ============================================================
void Console::addLogLine(LogLevel level, const char* rawMsg)
{
    if (!rawMsg) return;

    // Logger format: "[YYYY-MM-DD HH:MM:SS] [LEVEL] message"
    // Find second "] " to skip both bracketed groups
    const char* msg = rawMsg;
    const char* p   = rawMsg;
    int brackets    = 0;
    while (*p) {
        if (*p == ']' && *(p+1) == ' ') {
            if (++brackets == 2) { msg = p + 2; break; }
        }
        ++p;
    }

    ConColor col;
    switch (level) {
        case LogLevel::Debug: col = ConColor::Dim;     break;
        case LogLevel::Info:  col = ConColor::Output;  break;
        case LogLevel::Warn:  col = ConColor::Warn;    break;
        case LogLevel::Error: col = ConColor::Error;   break;
        default:              col = ConColor::Output;  break;
    }

    // Prefix with a short tag for scannability (Q2/Source style)
    char tagged[1100];
    switch (level) {
        case LogLevel::Warn:  snprintf(tagged, sizeof(tagged), "[WARN]  %s", msg); break;
        case LogLevel::Error: snprintf(tagged, sizeof(tagged), "[ERROR] %s", msg); break;
        case LogLevel::Debug: snprintf(tagged, sizeof(tagged), "[DBG]   %s", msg); break;
        default:              snprintf(tagged, sizeof(tagged), "%s", msg);          break;
    }

    const int cw     = Text2D::charWidth();
    const int maxCols = (1280 - kPadX * 2 - kScrollW - kScrollGap) / cw;
    wrapAndAdd(tagged, col, maxCols > 10 ? maxCols : 78);
}

// ============================================================
// wrapAndAdd
// ============================================================
void Console::wrapAndAdd(const std::string& text, ConColor color, int maxCols)
{
    if (text.empty()) { addLine("", color); return; }
    if ((int)text.size() <= maxCols) { addLine(text, color); return; }

    int start = 0;
    bool first = true;
    while (start < (int)text.size())
    {
        int cap   = first ? maxCols : maxCols - 2;
        int avail = (int)text.size() - start;
        if (avail <= cap) {
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

// ============================================================
// addLine
// ============================================================
void Console::addLine(const std::string& line, ConColor color)
{
    m_scrollback.push_back({line, color});
    if ((int)m_scrollback.size() > kMaxLines)
        m_scrollback.erase(m_scrollback.begin());
}

// ============================================================
// submit
// ============================================================
void Console::submit(const char* line)
{
    if (!line || !*line) return;

    // Echo with amber prompt prefix
    addLine(std::string("] ") + line, ConColor::Command);

    if (m_historyCount == 0 || m_history[0] != line) {
        for (int i = kMaxHistory - 1; i > 0; --i)
            m_history[i] = m_history[i-1];
        m_history[0] = line;
        if (m_historyCount < kMaxHistory) ++m_historyCount;
    }
    m_historyIdx = -1;

    CVarSystem::instance().exec(line);
    m_scroll = 0;
}

// ============================================================
// handleInput
// ============================================================
void Console::handleInput(const InputState& cur, const InputState& prev)
{
    const bool grave = cur.keys[SDL_SCANCODE_GRAVE];
    if (grave && !m_prevGrave) {
        toggle();
        if (m_mouseGrabCallback) m_mouseGrabCallback(!m_open);
    }
    m_prevGrave = grave;

    if (!m_open) return;

    if (cur.keys[SDL_SCANCODE_ESCAPE] && !prev.keys[SDL_SCANCODE_ESCAPE]) {
        close();
        if (m_mouseGrabCallback) m_mouseGrabCallback(true);
        return;
    }
    if (cur.keys[SDL_SCANCODE_RETURN] && !prev.keys[SDL_SCANCODE_RETURN]) {
        if (!m_inputBuf.empty()) submit(m_inputBuf.c_str());
        m_inputBuf.clear(); m_inputPos = 0; return;
    }
    if (cur.keys[SDL_SCANCODE_UP] && !prev.keys[SDL_SCANCODE_UP]) {
        if (m_historyIdx < m_historyCount - 1)
        { m_inputBuf = m_history[++m_historyIdx]; m_inputPos = (int)m_inputBuf.size(); }
        return;
    }
    if (cur.keys[SDL_SCANCODE_DOWN] && !prev.keys[SDL_SCANCODE_DOWN]) {
        if (m_historyIdx > 0)
        { m_inputBuf = m_history[--m_historyIdx]; m_inputPos = (int)m_inputBuf.size(); }
        else if (m_historyIdx == 0)
        { m_historyIdx = -1; m_inputBuf.clear(); m_inputPos = 0; }
        return;
    }

    const int ch   = Text2D::charHeight();
    const int lineH = ch + 2;
    const int textAreaH = kConH - kHeaderH - 1 - kInputH - kPadY * 2;
    const int lpp  = std::max(1, textAreaH / lineH);

    if (cur.keys[SDL_SCANCODE_PAGEUP]   && !prev.keys[SDL_SCANCODE_PAGEUP])
    { m_scroll = std::min(m_scroll + lpp, (int)m_scrollback.size()); return; }
    if (cur.keys[SDL_SCANCODE_PAGEDOWN] && !prev.keys[SDL_SCANCODE_PAGEDOWN])
    { m_scroll = std::max(m_scroll - lpp, 0); return; }

    if (cur.keys[SDL_SCANCODE_BACKSPACE] && !prev.keys[SDL_SCANCODE_BACKSPACE]) {
        if (m_inputPos > 0) { m_inputBuf.erase(m_inputPos - 1, 1); --m_inputPos; } return;
    }
    if (cur.keys[SDL_SCANCODE_DELETE] && !prev.keys[SDL_SCANCODE_DELETE]) {
        if (m_inputPos < (int)m_inputBuf.size()) m_inputBuf.erase(m_inputPos, 1); return;
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
            char ch2 = shift ? m.s : m.p;
            m_inputBuf.insert(m_inputPos, 1, ch2);
            ++m_inputPos;
        }
    }
}

// ============================================================
// render
// ============================================================
void Console::render(int screenW, int screenH)
{
    // ---- Slide animation ----
    static uint64_t s_lastTime = 0;
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (s_lastTime == 0)
               ? 0.016f
               : (float)((double)(now - s_lastTime) / SDL_GetPerformanceFrequency());
    if (dt > 0.1f) dt = 0.1f;
    s_lastTime = now;

    float target = m_open ? 1.0f : 0.0f;
    float diff   = target - m_slideT;
    if (std::abs(diff) > 0.001f)
        m_slideT += diff * kSlideSpeed * dt;
    else
        m_slideT = target;

    if (m_slideT <= 0.001f) return;

    // ---- Scaled dimensions ----
    const int consoleH = (int)(kConH * m_slideT);
    const int headerH  = (int)(kHeaderH * m_slideT);
    const int inputY   = consoleH - kInputH;

    Text2D::init();
    Text2D::begin(screenW, screenH);

    // ================================================================
    // 1.  MAIN BACKGROUND
    // ================================================================
    Text2D::drawFill(0, 0, screenW, consoleH, kBgMain);

    // ================================================================
    // 2.  SCANLINES (subtle alternating rows — CRT effect)
    //     Every even row gets a very faint darkening overlay.
    //     Only visible when m_slideT is near 1 to avoid jitter.
    // ================================================================
    if (m_slideT > 0.85f)
    {
        const int scanStart = kHeaderH + 1;
        const int scanEnd   = inputY;
        // Draw every 2nd scanline as a 1px dark strip
        for (int sy = scanStart; sy < scanEnd; sy += 2)
            Text2D::drawFill(0, sy, screenW - kScrollW - kScrollGap, 1, kBgScanL);
    }

    // ================================================================
    // 3.  HEADER BAR — gradient left (dark navy) → right (amber tint)
    // ================================================================
    if (headerH > 0)
    {
        Text2D::drawFillGradientH(0, 0, screenW, headerH, kBgHeader, kBgHeaderR);

        // Bottom border of header — 2px amber line
        Text2D::drawFill(0, headerH, screenW, 2, kBorderAmber);

        // Header text: "NOVA ENGINE" left-aligned with padding
        if (m_slideT > 0.6f)
        {
            const int ch  = Text2D::charHeight();
            const int ty  = (headerH - ch) / 2;
            if (ty >= 0 && ty + ch <= headerH)
            {
                // Draw "NOVA ENGINE" as shadow + text
                Text2D::drawStringShadow(kPadX, ty, "NOVA ENGINE",
                                         kColHeader, kShadow, 1, 1);

                // Right side: version string in dim color
                const char* ver = "v0.1.0  |  OpenGL 4.5";
                int verW = Text2D::stringWidth(ver);
                Text2D::drawString(screenW - verW - kPadX, ty, ver, kColVersion);
            }
        }
    }

    // ================================================================
    // 4.  INPUT BAR
    // ================================================================
    // Background
    Text2D::drawFill(0, inputY, screenW, kInputH, kBgInput);
    // Top border — 1px amber
    Text2D::drawFill(0, inputY, screenW, 1, kBorderAmber);
    // Bottom edge — 2px amber (console bottom border)
    Text2D::drawFill(0, consoleH - 2, screenW, 2, kBorderAmber);

    // ================================================================
    // 5.  SCROLLBACK TEXT AREA
    // ================================================================
    const int cw    = Text2D::charWidth();
    const int ch    = Text2D::charHeight();
    const int lineH = ch + 2;

    const int textTop = headerH + 2 + kPadY;  // below header border
    const int textBot = inputY - kPadY;        // above input border
    const int textH   = textBot - textTop;
    const int maxVisible = std::max(0, textH / lineH);

    const int total   = (int)m_scrollback.size();
    int       endIdx  = total - m_scroll;
    int       startIdx = endIdx - maxVisible;
    if (startIdx < 0) startIdx = 0;
    if (endIdx   > total) endIdx = total;

    // Calculate available width accounting for scrollbar
    const int textAreaW = screenW - kPadX * 2 - kScrollW - kScrollGap * 2;

    for (int i = startIdx; i < endIdx; ++i)
    {
        const ConLine& entry = m_scrollback[i];
        int lineY = textTop + (i - startIdx) * lineH;
        if (lineY + ch > textBot) break;

        const Vec4& color = kColTable[(int)entry.color];

        // Truncate if line is wider than the text area
        int maxChars = textAreaW / cw;
        if ((int)entry.text.size() > maxChars && maxChars > 3)
        {
            std::string trunc = entry.text.substr(0, maxChars - 1) + "~";
            // Drop shadow for readability
            Text2D::drawString(kPadX + 1, lineY + 1, trunc.c_str(),
                               Vec4{0,0,0, color.w * 0.6f});
            Text2D::drawString(kPadX, lineY, trunc.c_str(), color);
        }
        else
        {
            Text2D::drawString(kPadX + 1, lineY + 1, entry.text.c_str(),
                               Vec4{0,0,0, color.w * 0.6f});
            Text2D::drawString(kPadX, lineY, entry.text.c_str(), color);
        }
    }

    // ================================================================
    // 6.  SCROLLBAR
    // ================================================================
    if (total > maxVisible)
    {
        const int trackX = screenW - kScrollW - 2;
        const int trackY = textTop;
        const int trackH = textH;

        // Track
        Text2D::drawFill(trackX, trackY, kScrollW, trackH, kScrollTrack);
        // Track border left — 1px dim
        Text2D::drawFill(trackX - 1, trackY, 1, trackH, kBorderDim);

        // Thumb — proportional
        float visFrac  = (float)maxVisible / (float)total;
        int   thumbH   = std::max(12, (int)(visFrac * (float)trackH));
        float scrollFrac = (total > maxVisible)
                           ? (float)m_scroll / (float)(total - maxVisible)
                           : 0.f;
        int thumbY = trackY + (int)((1.0f - scrollFrac) * (float)(trackH - thumbH));
        thumbY = std::max(trackY, std::min(thumbY, trackY + trackH - thumbH));

        Text2D::drawFill(trackX, thumbY, kScrollW, thumbH, kScrollThumb);
    }

    // ================================================================
    // 7.  SCROLL INDICATOR (PageDn hint when scrolled up)
    // ================================================================
    if (m_scroll > 0)
    {
        const char* hint = "[ SCROLL: PageUp/PageDn ]";
        int hintW = Text2D::stringWidth(hint);
        int hintX = (screenW - hintW) / 2;
        int hintY = textBot - lineH;
        if (hintY > textTop)
        {
            // Semi-transparent dark pill background
            Text2D::drawFill(hintX - 4, hintY - 2, hintW + 8, lineH + 2,
                             Vec4{0.1f, 0.08f, 0.04f, 0.85f});
            Text2D::drawString(hintX, hintY, hint,
                               Vec4{0.91f, 0.72f, 0.20f, 0.95f});
        }
    }

    // ================================================================
    // 8.  INPUT LINE
    // ================================================================
    {
        const int padV = (kInputH - ch) / 2;
        const int iy   = inputY + padV;

        // "] " prompt in amber
        const char* prompt = "] ";
        Text2D::drawStringShadow(kPadX, iy, prompt, kColPrompt, kShadow, 1, 1);
        int cursorBaseX = kPadX + Text2D::stringWidth(prompt);

        // Text before cursor
        if (!m_inputBuf.empty())
        {
            std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
            std::string after  = m_inputBuf.substr((size_t)m_inputPos);

            Text2D::drawString(cursorBaseX + 1, iy + 1, before.c_str(),
                               Vec4{0,0,0, 0.6f});
            Text2D::drawString(cursorBaseX, iy, before.c_str(), kColInput);

            int afterX = cursorBaseX + Text2D::stringWidth(before.c_str());
            Text2D::drawString(afterX + 1, iy + 1, after.c_str(),
                               Vec4{0,0,0, 0.6f});
            Text2D::drawString(afterX, iy, after.c_str(), kColInput);
        }

        // ---- Blinking block cursor ----
        m_blinkAccum += dt;
        if (m_blinkAccum > 0.50f) { m_blinkAccum = 0.f; m_cursorVis = !m_cursorVis; }

        if (m_cursorVis)
        {
            std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
            int cx = cursorBaseX + Text2D::stringWidth(before.c_str());

            // Bright amber block cursor (full character cell)
            Text2D::drawFill(cx, iy, cw, ch,
                             Vec4{0.91f, 0.58f, 0.039f, 0.75f});

            // If there's a character under cursor, redraw it in inverse color
            if (m_inputPos < (int)m_inputBuf.size())
            {
                char buf[2] = { m_inputBuf[m_inputPos], '\0' };
                Text2D::drawString(cx, iy, buf, Vec4{0.04f, 0.05f, 0.06f, 1.0f});
            }
        }
    }

    // ================================================================
    // 9.  THIN DECORATIVE LEFT EDGE — 3px amber bar
    // ================================================================
    Text2D::drawFillGradientH(0, headerH + 2, 3, consoleH - headerH - 2,
                               Vec4{0.784f, 0.471f, 0.125f, 0.7f},
                               Vec4{0.784f, 0.471f, 0.125f, 0.0f});

    Text2D::end();
}

} // namespace nova