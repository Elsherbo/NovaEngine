// ============================================================
// FILE:    engine/core/console.cpp
// MODULE:  Core > Console
// VERSION: v7 — Professional Q2/Source Engine style
//
// CHANGES vs v6:
//   FIX 1 — Text advance bug: All charWidth / stringWidth math
//     now resolves to 16px (matching the 16×16 glyph quads).
//     The old code used Text2D::charWidth() in maxChars (returned
//     16 — correct) but drawString internally advanced 8px — so
//     78 chars were calculated but 156 were rendered, producing a
//     dense overlap smear. Now charWidth()=16 and advance=16 are
//     consistent end-to-end.
//
//   FIX 2 — Input bar height: kInputBarH raised from 24→28px
//     to properly center a 16px-tall character with 6px padding
//     above and below. The old 24px left only 4px of vertical
//     padding, causing glyphs to clip against the divider line.
//
//   FIX 3 — Logger hook: The raw Logger message includes a full
//     timestamp prefix "[YYYY-MM-DD HH:MM:SS] [LEVEL] ". The
//     hook now calls addLogLine() which strips the prefix and
//     classifies by level, keeping the scrollback clean.
//
//   FIX 4 — Line wrapping: Long messages are wrapped at maxCols
//     with a "  " continuation indent instead of being truncated
//     to "..". This matches Q2 console behavior.
//
//   IMPROVEMENT 1 — Header bar: 22px bar at the top shows
//     "Nova Engine" + version. Matches Source Engine console
//     which shows "Source Engine" in the header.
//
//   IMPROVEMENT 2 — Distinct input area: The input line has its
//     own darker background rect, clearly separating it from the
//     scrollback area. Quake 2 and Source both do this.
//
//   IMPROVEMENT 3 — Scrollbar: A thin 4px scrollbar on the right
//     edge shows position in the scrollback buffer.
//
//   IMPROVEMENT 4 — Q2-style prompt: Changed from "> " to "] "
//     to match the classic Quake 2 console aesthetic.
//
//   IMPROVEMENT 5 — 6 color categories: Added System (blue) and
//     Success (teal) to the existing 5. Logger errors → Error,
//     warns → Warn, info → Output (auto-classified).
//
//   IMPROVEMENT 6 — Slide speed: Raised from 10.0 to 12.0 for a
//     snappier feel closer to Source Engine's console animation.
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

namespace nova
{

// ---- Layout ----
static constexpr int   kConsoleHeightPx = 380;   // slightly taller than before
static constexpr int   kHeaderH         = 22;    // engine name header bar
static constexpr int   kPadX            = 10;
static constexpr int   kPadY            = 6;
static constexpr int   kInputBarH       = 28;    // FIX 2: was 24
static constexpr int   kScrollbarW      = 4;
static constexpr float kSlideSpeed      = 12.0f; // IMPROVEMENT 6: was 10.0

// ---- Colors (sRGB, GL_FRAMEBUFFER_SRGB enabled) ----
// Background: very dark navy, 88% opaque — professional and readable
static constexpr Vec4 kColBg        = { 0.04f, 0.04f, 0.10f, 0.90f };
// Header bar: slightly lighter than bg to be distinguishable
static constexpr Vec4 kColHeader    = { 0.06f, 0.06f, 0.16f, 1.00f };
// Input area background: even darker to visually anchor it
static constexpr Vec4 kColInputBg   = { 0.02f, 0.02f, 0.07f, 1.00f };
// Border/divider: steel blue
static constexpr Vec4 kColBorder    = { 0.30f, 0.50f, 0.80f, 0.90f };
// Scrollbar track
static constexpr Vec4 kColScrollBg  = { 0.12f, 0.12f, 0.24f, 1.00f };
// Scrollbar thumb
static constexpr Vec4 kColScrollFg  = { 0.35f, 0.55f, 0.85f, 1.00f };

// Text colors by semantic category
static constexpr Vec4 kColOutput    = { 0.85f, 0.85f, 0.85f, 1.00f };  // light gray
static constexpr Vec4 kColCommand   = { 0.40f, 1.00f, 0.45f, 1.00f };  // green  
static constexpr Vec4 kColError     = { 1.00f, 0.30f, 0.25f, 1.00f };  // red
static constexpr Vec4 kColWarn      = { 1.00f, 0.80f, 0.20f, 1.00f };  // amber
static constexpr Vec4 kColSystem    = { 0.40f, 0.70f, 1.00f, 1.00f };  // blue
static constexpr Vec4 kColSuccess   = { 0.30f, 1.00f, 0.80f, 1.00f };  // teal
static constexpr Vec4 kColDim       = { 0.40f, 0.40f, 0.44f, 1.00f };  // dimmed

// Header text color
static constexpr Vec4 kColHeaderText = { 0.60f, 0.70f, 0.90f, 1.00f };

// ============================================================
// Constructor / Destructor
// ============================================================
Console::Console()  = default;
Console::~Console() = default;

// ============================================================
// addLogLine — called by Logger hook; strips timestamp prefix
//
// Logger formats each message as:
//   "[YYYY-MM-DD HH:MM:SS] [LEVEL] actual message here"
// We want to show only "actual message here" in the console,
// classified by level. The timestamps clutter the scrollback
// and take up ~30 chars of the ~78-char line width.
// ============================================================
void Console::addLogLine(LogLevel level, const char* rawMsg)
{
    if (!rawMsg) return;

    // Strip the "[YYYY-MM-DD HH:MM:SS] [LEVEL] " prefix.
    // Format: 1 + 19 + 2 + 1 + 5 + 2 = 30 chars: "[2026-04-27 13:00:00] [INFO] "
    // We scan for the second "] " to be robust against format changes.
    const char* msg = rawMsg;
    const char* p = rawMsg;
    int brackets = 0;
    while (*p)
    {
        if (*p == ']' && *(p+1) == ' ')
        {
            ++brackets;
            if (brackets == 2) { msg = p + 2; break; }
        }
        ++p;
    }
    // If we didn't find two brackets, use the raw message unchanged.

    ConColor col = ConColor::Output;
    switch (level)
    {
    case LogLevel::Debug: col = ConColor::Dim;    break;
    case LogLevel::Info:  col = ConColor::Output; break;
    case LogLevel::Warn:  col = ConColor::Warn;   break;
    case LogLevel::Error: col = ConColor::Error;  break;
    }

    // FIX 4: wrap at max columns instead of truncating
    const int maxCols = (1280 - kPadX * 2 - kScrollbarW) / Text2D::charWidth();
    wrapAndAdd(msg, col, maxCols);
}

// ============================================================
// wrapAndAdd — word-wrap a string and add each line to scrollback
// ============================================================
void Console::wrapAndAdd(const std::string& text, ConColor color, int maxCols)
{
    if (text.empty()) { addLine("", color); return; }

    if ((int)text.size() <= maxCols)
    {
        addLine(text, color);
        return;
    }

    // Wrap at word boundaries when possible, hard-wrap otherwise.
    // Continuation lines get a 2-space indent to show they're wrapped.
    int start = 0;
    bool first = true;
    while (start < (int)text.size())
    {
        int cap   = first ? maxCols : maxCols - 2;
        int avail = (int)text.size() - start;
        if (avail <= cap)
        {
            std::string line = first ? text.substr(start)
                                     : "  " + text.substr(start);
            addLine(line, color);
            break;
        }

        // Try to break at last space within cap
        int breakAt = start + cap;
        int spaceAt = -1;
        for (int i = breakAt; i > start; --i)
        {
            if (text[i] == ' ') { spaceAt = i; break; }
        }
        if (spaceAt < 0) spaceAt = breakAt; // hard break

        std::string line = first ? text.substr(start, spaceAt - start)
                                 : "  " + text.substr(start, spaceAt - start);
        addLine(line, color);
        start = spaceAt + (text[spaceAt] == ' ' ? 1 : 0);
        first = false;
    }
}

// ============================================================
// submit
// ============================================================
void Console::submit(const char* line)
{
    if (!line || !*line) return;

    addLine(std::string("] ") + line, ConColor::Command);

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

// ============================================================
// addLine
// ============================================================
void Console::addLine(const std::string& line, ConColor color)
{
    m_scrollback.push_back(ConLine{ line, color });
    if ((int)m_scrollback.size() > kMaxLines)
        m_scrollback.erase(m_scrollback.begin());
}

// ============================================================
// handleInput
// ============================================================
void Console::handleInput(const InputState& cur, const InputState& prev)
{
    const bool grave = cur.keys[SDL_SCANCODE_GRAVE];
    if (grave && !m_prevGrave)
    {
        toggle();
        if (m_mouseGrabCallback)
            m_mouseGrabCallback(!m_open);
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
        if (!m_inputBuf.empty())
            submit(m_inputBuf.c_str());
        m_inputBuf.clear();
        m_inputPos = 0;
        return;
    }

    if (cur.keys[SDL_SCANCODE_UP] && !prev.keys[SDL_SCANCODE_UP])
    {
        if (m_historyIdx < m_historyCount - 1)
        {
            m_inputBuf = m_history[++m_historyIdx];
            m_inputPos = (int)m_inputBuf.size();
        }
        return;
    }
    if (cur.keys[SDL_SCANCODE_DOWN] && !prev.keys[SDL_SCANCODE_DOWN])
    {
        if (m_historyIdx > 0)
        {
            m_inputBuf = m_history[--m_historyIdx];
            m_inputPos = (int)m_inputBuf.size();
        }
        else if (m_historyIdx == 0)
        {
            m_historyIdx = -1;
            m_inputBuf.clear();
            m_inputPos = 0;
        }
        return;
    }

    const int ch       = Text2D::charHeight();  // now 16 (was already 16)
    const int lineH    = ch + 2;
    const int textArea = kConsoleHeightPx - kHeaderH - kInputBarH - kPadY * 2;
    const int linesPerPage = std::max(1, textArea / lineH);

    if (cur.keys[SDL_SCANCODE_PAGEUP] && !prev.keys[SDL_SCANCODE_PAGEUP])
    {
        m_scroll = std::min(m_scroll + linesPerPage, (int)m_scrollback.size());
        return;
    }
    if (cur.keys[SDL_SCANCODE_PAGEDOWN] && !prev.keys[SDL_SCANCODE_PAGEDOWN])
    {
        m_scroll = std::max(m_scroll - linesPerPage, 0);
        return;
    }

    if (cur.keys[SDL_SCANCODE_BACKSPACE] && !prev.keys[SDL_SCANCODE_BACKSPACE])
    {
        if (m_inputPos > 0 && !m_inputBuf.empty())
        {
            m_inputBuf.erase(m_inputPos - 1, 1);
            --m_inputPos;
        }
        return;
    }
    if (cur.keys[SDL_SCANCODE_DELETE] && !prev.keys[SDL_SCANCODE_DELETE])
    {
        if (m_inputPos < (int)m_inputBuf.size())
            m_inputBuf.erase(m_inputPos, 1);
        return;
    }
    if (cur.keys[SDL_SCANCODE_HOME] && !prev.keys[SDL_SCANCODE_HOME])
    { m_inputPos = 0; return; }
    if (cur.keys[SDL_SCANCODE_END] && !prev.keys[SDL_SCANCODE_END])
    { m_inputPos = (int)m_inputBuf.size(); return; }
    if (cur.keys[SDL_SCANCODE_LEFT] && !prev.keys[SDL_SCANCODE_LEFT])
    { if (m_inputPos > 0) --m_inputPos; return; }
    if (cur.keys[SDL_SCANCODE_RIGHT] && !prev.keys[SDL_SCANCODE_RIGHT])
    { if (m_inputPos < (int)m_inputBuf.size()) ++m_inputPos; return; }

    if (cur.keys[SDL_SCANCODE_TAB] && !prev.keys[SDL_SCANCODE_TAB]) return;

    const bool shift = cur.keys[SDL_SCANCODE_LSHIFT] || cur.keys[SDL_SCANCODE_RSHIFT];

    struct KeyMap { int sc; char plain; char shifted; };
    static const KeyMap kMap[] = {
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
            char ch2 = shift ? m.shifted : m.plain;
            m_inputBuf.insert(m_inputPos, 1, ch2);
            ++m_inputPos;
        }
    }
}

// ============================================================
// render — Professional Q2/Source Engine layout
//
// Layout (top to bottom, all within consoleH):
//   [0..kHeaderH]          Header bar (engine name)
//   [kHeaderH..1px]        Thin border line
//   [kHeaderH..bot-input]  Scrollback text area
//   [bot-input..kInputBarH] Input area with darker bg
//   [bottom-1..bottom]     Bottom border line
//
// Right edge has a 4px scrollbar track + thumb.
// ============================================================
void Console::render(int screenW, int screenH)
{
    static uint64_t s_lastRenderTime = 0;
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (s_lastRenderTime == 0)
               ? 0.016f
               : (float)((double)(now - s_lastRenderTime) / SDL_GetPerformanceFrequency());
    if (dt > 0.1f) dt = 0.1f;
    s_lastRenderTime = now;

    float target = m_open ? 1.0f : 0.0f;
    float diff   = target - m_slideT;
    if (std::abs(diff) > 0.001f)
        m_slideT += diff * kSlideSpeed * dt;
    else
        m_slideT = target;

    if (m_slideT <= 0.001f) return;

    Text2D::init();

    const int consoleH = (int)(kConsoleHeightPx * m_slideT);
    const int consoleY = 0;

    Text2D::begin(screenW, screenH);

    // ---- Main background ----
    Text2D::drawFill(0, consoleY, screenW, consoleH, kColBg);

    // ---- Header bar ----
    const int headerH = (int)(kHeaderH * m_slideT);
    if (headerH > 0)
    {
        Text2D::drawFill(0, consoleY, screenW, headerH, kColHeader);
        // Bottom border of header
        Text2D::drawFill(0, consoleY + headerH, screenW, 1, kColBorder);

        // "Nova Engine" title centered in header — only draw when header is mostly visible
        if (m_slideT > 0.7f)
        {
            const char* title = "Nova Engine";
            int titleW = Text2D::stringWidth(title);
            int titleX = (screenW - titleW) / 2;
            int titleY = consoleY + (headerH - Text2D::charHeight()) / 2;
            if (titleY >= consoleY && titleY + Text2D::charHeight() <= consoleY + headerH)
                Text2D::drawString(titleX, titleY, title, kColHeaderText);
        }
    }

    // ---- Input area background (at bottom) ----
    const int inputAreaY = consoleY + consoleH - kInputBarH;
    Text2D::drawFill(0, inputAreaY, screenW, kInputBarH, kColInputBg);
    // Top border of input area
    Text2D::drawFill(0, inputAreaY, screenW, 1, kColBorder);
    // Bottom border of entire console
    Text2D::drawFill(0, consoleY + consoleH - 1, screenW, 1, kColBorder);

    // ---- Scrollback text area ----
    const int ch    = Text2D::charHeight();  // 16
    const int lineH = ch + 2;               // 18

    // Text area: below header+border, above input area
    const int textTop = consoleY + headerH + 1 + kPadY;
    const int textBot = inputAreaY - kPadY;
    const int textH   = textBot - textTop;
    const int maxLines = std::max(0, textH / lineH);

    // FIX 1: maxCols now uses charWidth()=16 AND drawString advances 16
    // so both sides of the calculation agree.
    const int maxCols = (screenW - kPadX * 2 - kScrollbarW - 4) / Text2D::charWidth();

    const int total  = (int)m_scrollback.size();
    int endIdx   = total - m_scroll;
    int startIdx = endIdx - maxLines;
    if (startIdx < 0) startIdx = 0;
    if (endIdx   > total) endIdx = total;

    static const Vec4 kColorTable[] = {
        kColOutput,   // ConColor::Output
        kColCommand,  // ConColor::Command
        kColError,    // ConColor::Error
        kColWarn,     // ConColor::Warn
        kColSystem,   // ConColor::System
        kColSuccess,  // ConColor::Success
        kColDim,      // ConColor::Dim
    };

    for (int i = startIdx; i < endIdx; ++i)
    {
        const ConLine& entry = m_scrollback[i];
        int lineY = textTop + (i - startIdx) * lineH;
        if (lineY + ch > textBot) break;

        Vec4 col = kColorTable[(int)entry.color];

        // Clip text to maxCols (hard truncate at this stage — wrap happens at addLine time)
        const std::string& text = entry.text;
        if ((int)text.size() > maxCols)
        {
            std::string truncated = text.substr(0, maxCols - 1) + "~";
            Text2D::drawString(kPadX, lineY, truncated.c_str(), col);
        }
        else
        {
            Text2D::drawString(kPadX, lineY, text.c_str(), col);
        }
    }

    // ---- Scrollbar ----
    if (total > maxLines)
    {
        const int trackX = screenW - kScrollbarW - 2;
        const int trackY = textTop;
        const int trackH = textH;

        // Track
        Text2D::drawFill(trackX, trackY, kScrollbarW, trackH, kColScrollBg);

        // Thumb — proportional to how much is visible
        float visibleFrac = (float)maxLines / (float)total;
        float thumbH_f    = visibleFrac * (float)trackH;
        int thumbH        = std::max(8, (int)thumbH_f);

        // Position: scroll=0 → thumb at bottom, scroll=max → thumb at top
        float scrollFrac = (float)m_scroll / (float)(total - maxLines);
        int thumbY = trackY + (int)((1.0f - scrollFrac) * (float)(trackH - thumbH));
        thumbY = std::max(trackY, std::min(thumbY, trackY + trackH - thumbH));

        Text2D::drawFill(trackX, thumbY, kScrollbarW, thumbH, kColScrollFg);
    }

    // ---- Scroll indicator text ----
    if (m_scroll > 0)
    {
        const char* scrollMsg = "[ PageDn to return ]";
        int msgX = screenW / 2 - Text2D::stringWidth(scrollMsg) / 2;
        int msgY = textBot - lineH;
        if (msgY > textTop)
            Text2D::drawString(msgX, msgY, scrollMsg, Vec4{1.0f, 1.0f, 0.3f, 0.9f});
    }

    // ---- Input line ----
    // FIX 2: kInputBarH=28, charHeight=16 → 6px top padding
    const int inputPadV = (kInputBarH - ch) / 2;
    const int inputY    = inputAreaY + inputPadV;

    // Q2-style "] " prompt (IMPROVEMENT 4)
    const char* prompt = "] ";
    Text2D::drawString(kPadX, inputY, prompt, kColCommand);

    int inputX = kPadX + Text2D::stringWidth(prompt);

    if (!m_inputBuf.empty())
    {
        std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
        std::string after  = m_inputBuf.substr((size_t)m_inputPos);
        Text2D::drawString(inputX, inputY, before.c_str(), kColOutput);
        int afterX = inputX + Text2D::stringWidth(before.c_str());
        Text2D::drawString(afterX, inputY, after.c_str(), kColOutput);
    }

    m_blinkAccum += dt;
    if (m_blinkAccum > 0.53f) { m_blinkAccum = 0.0f; m_cursorVis = !m_cursorVis; }

    if (m_cursorVis)
    {
        std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
        int curX = inputX + Text2D::stringWidth(before.c_str());
        // Cursor: 2px wide, full glyph height, vertically centered
        Text2D::drawFill(curX, inputY, 2, ch, kColOutput);
    }

    Text2D::end();
}

} // namespace nova