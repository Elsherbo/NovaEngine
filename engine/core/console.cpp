// ============================================================
// FILE:    engine/core/console.cpp
// MODULE:  Core > Console
// VERSION: v5 — Layout fixed for kDisplayGlyphSize=16px characters
//
// LAYOUT FIX:
//   All layout constants are now derived from charHeight() at runtime
//   instead of being hardcoded. kConH, kHeaderH, kInputH use the
//   actual rendered glyph size so the console looks correct regardless
//   of font resolution.
//
//   With kDisplayGlyphSize=16:
//     charHeight() = 16px
//     lineH        = 18px  (16 + 2px spacing)
//     kHeaderH     = 24px  (16 + 4px top + 4px bottom padding)
//     kInputH      = 24px  (same)
//     kConH        = 320px (~16 scrollback lines + header + input)
//
// DESIGN: Military terminal aesthetic unchanged from v4.
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

// ---------------------------------------------------------------------------
// Layout helpers — derived at runtime from actual character size
// This ensures the console looks correct for any font resolution.
// ---------------------------------------------------------------------------
static int consoleCharH()  { return Text2D::charHeight(); }
static int consoleLineH()  { return Text2D::charHeight() + 2; }  // 2px line gap
static int consoleBarH()   { return Text2D::charHeight() + 8; }  // 4px top + 4px bottom pad

// Total console height: fixed number of visible lines + header + input + borders
static constexpr int kTargetLines   = 14;   // scrollback lines visible at once
static constexpr int kBorderTop     = 2;    // amber top border thickness
static constexpr int kBorderSep     = 1;    // internal separator thickness
static constexpr int kPadX          = 8;    // left/right text padding
static constexpr int kPadY          = 3;    // top/bottom padding within scrollback
static constexpr int kScrollW       = 3;    // scrollbar width
static constexpr int kScrollGap     = 4;    // gap between text and scrollbar
static constexpr float kSlideSpeed  = 18.0f;

// Computed at render time from font metrics:
//   kConH = kBorderTop + consoleBarH() + kBorderSep
//         + kPadY*2 + kTargetLines*consoleLineH()
//         + kBorderSep + consoleBarH()
static int computeConH()
{
    return kBorderTop
         + consoleBarH() + kBorderSep
         + kPadY * 2 + kTargetLines * consoleLineH()
         + kBorderSep + consoleBarH();
}

// ---------------------------------------------------------------------------
// Color palette — single source of truth
// ---------------------------------------------------------------------------
static constexpr Vec4 kClrBodyBg    = { 0.000f, 0.000f, 0.000f, 0.920f };
static constexpr Vec4 kClrBarBg     = { 0.000f, 0.000f, 0.000f, 1.000f };
static constexpr Vec4 kClrAmber     = { 1.000f, 0.690f, 0.000f, 1.000f };
static constexpr Vec4 kClrAmberDim  = { 1.000f, 0.690f, 0.000f, 0.600f };
static constexpr Vec4 kClrAmberFaint= { 1.000f, 0.690f, 0.000f, 0.030f };
static constexpr Vec4 kClrText      = { 0.784f, 0.784f, 0.765f, 1.000f };
static constexpr Vec4 kClrTextDim   = { 0.353f, 0.353f, 0.345f, 1.000f };
static constexpr Vec4 kClrTextSys   = { 0.392f, 0.627f, 0.784f, 1.000f };
static constexpr Vec4 kClrTextErr   = { 0.863f, 0.275f, 0.196f, 1.000f };
static constexpr Vec4 kClrTextOk    = { 0.353f, 0.784f, 0.392f, 1.000f };
static constexpr Vec4 kClrTextCmd   = kClrAmber;
static constexpr Vec4 kClrBorderHi  = kClrAmber;
static constexpr Vec4 kClrBorderLo  = { 0.157f, 0.157f, 0.157f, 1.000f };
static constexpr Vec4 kClrScrollTrk = { 0.059f, 0.059f, 0.059f, 1.000f };
static constexpr Vec4 kClrScrollThm = kClrAmberDim;
static constexpr Vec4 kClrVersion   = { 0.275f, 0.275f, 0.267f, 1.000f };
static constexpr Vec4 kClrShadow    = { 0.000f, 0.000f, 0.000f, 0.900f };

static const Vec4& colorFor(ConColor c)
{
    switch (c)
    {
        case ConColor::Output:  return kClrText;
        case ConColor::Command: return kClrTextCmd;
        case ConColor::Error:   return kClrTextErr;
        case ConColor::Warn:    return kClrAmber;
        case ConColor::System:  return kClrTextSys;
        case ConColor::Success: return kClrTextOk;
        case ConColor::Dim:     return kClrTextDim;
        case ConColor::Accent:  return kClrAmber;
        default:                return kClrText;
    }
}

static bool useAltSet(ConColor c)
{
    return c == ConColor::Command || c == ConColor::Warn || c == ConColor::Accent;
}

// ---------------------------------------------------------------------------
Console::Console()  = default;
Console::~Console() = default;

// ---------------------------------------------------------------------------
void Console::addLogLine(LogLevel level, const char* rawMsg)
{
    if (!rawMsg) return;

    // Strip "[YYYY-MM-DD HH:MM:SS] [LEVEL] " prefix
    const char* msg = rawMsg;
    const char* p   = rawMsg;
    int brackets    = 0;
    while (*p)
    {
        if (*p == ']' && *(p+1) == ' ')
            if (++brackets == 2) { msg = p + 2; break; }
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

    char tagged[1100];
    switch (level)
    {
        case LogLevel::Warn:  snprintf(tagged, sizeof(tagged), "WARN  %s", msg);  break;
        case LogLevel::Error: snprintf(tagged, sizeof(tagged), "ERROR %s", msg);  break;
        default:              snprintf(tagged, sizeof(tagged), "%s", msg);         break;
    }

    const int cw      = Text2D::charWidth();
    const int maxCols = (1280 - kPadX * 2 - kScrollW - kScrollGap) / (cw > 0 ? cw : 8);
    wrapAndAdd(tagged, col, maxCols > 10 ? maxCols : 72);
}

// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
void Console::addLine(const std::string& line, ConColor color)
{
    m_scrollback.push_back({line, color});
    if ((int)m_scrollback.size() > kMaxLines)
        m_scrollback.erase(m_scrollback.begin());
}

// ---------------------------------------------------------------------------
void Console::submit(const char* line)
{
    if (!line || !*line) return;

    addLine(std::string("> ") + line, ConColor::Command);

    if (m_historyCount == 0 || m_history[0] != line)
    {
        for (int i = kMaxHistory - 1; i > 0; --i)
            m_history[i] = m_history[i-1];
        m_history[0] = line;
        if (m_historyCount < kMaxHistory) ++m_historyCount;
    }
    m_historyIdx = -1;

    CVarSystem::instance().exec(line);
    m_scroll = 0;
}

// ---------------------------------------------------------------------------
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
        m_inputBuf.clear(); m_inputPos = 0;
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

    const int lineH   = consoleLineH();
    const int lpp     = std::max(1, kTargetLines);

    if (cur.keys[SDL_SCANCODE_PAGEUP]   && !prev.keys[SDL_SCANCODE_PAGEUP])
    { m_scroll = std::min(m_scroll + lpp, (int)m_scrollback.size()); return; }
    if (cur.keys[SDL_SCANCODE_PAGEDOWN] && !prev.keys[SDL_SCANCODE_PAGEDOWN])
    { m_scroll = std::max(m_scroll - lpp, 0); return; }
    (void)lineH;

    if (cur.keys[SDL_SCANCODE_BACKSPACE] && !prev.keys[SDL_SCANCODE_BACKSPACE])
    {
        if (m_inputPos > 0) { m_inputBuf.erase(m_inputPos - 1, 1); --m_inputPos; }
        return;
    }
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
            char c2 = shift ? m.s : m.p;
            m_inputBuf.insert(m_inputPos, 1, c2);
            ++m_inputPos;
        }
    }
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------
void Console::render(int screenW, int screenH)
{
    // ---- Delta time ----
    static uint64_t s_lastTime = 0;
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (s_lastTime == 0)
               ? 0.016f
               : (float)((double)(now - s_lastTime) / SDL_GetPerformanceFrequency());
    if (dt > 0.1f) dt = 0.1f;
    s_lastTime = now;

    // ---- Slide animation ----
    float target = m_open ? 1.0f : 0.0f;
    float diff   = target - m_slideT;
    if (std::abs(diff) > 0.001f)
        m_slideT += diff * kSlideSpeed * dt;
    else
        m_slideT = target;

    if (m_slideT <= 0.001f) return;

    // ---- Metrics derived from actual font size ----
    const int ch     = consoleCharH();   // e.g. 16
    const int lineH  = consoleLineH();   // e.g. 18
    const int barH   = consoleBarH();    // e.g. 24
    const int cw     = Text2D::charWidth(); // e.g. 16
    const int conH   = computeConH();    // total console height at slideT=1

    // ---- Derived pixel positions (scaled by slideT for animation) ----
    const int totalH     = (int)(conH * m_slideT);
    const int borderTopH = (int)(kBorderTop * m_slideT);
    const int headerH    = (int)(barH * m_slideT);
    const int headerY    = borderTopH;
    const int sep1Y      = headerY + headerH;
    const int bodyY      = sep1Y + kBorderSep;
    const int inputH     = (int)(barH * m_slideT);
    const int sep2Y      = totalH - inputH - kBorderSep;
    const int inputY     = sep2Y + kBorderSep;
    const int bodyH      = sep2Y - bodyY;

    // ---- Text column limits ----
    const int textAreaW     = screenW - kPadX * 2 - kScrollW - kScrollGap;
    const int maxCharsPerLine = (cw > 0) ? (textAreaW / cw) : 80;

    Text2D::init();
    Text2D::begin(screenW, screenH);

    // ================================================================
    //  LAYER 1: BODY BACKGROUND — pure black, near-opaque
    // ================================================================
    Text2D::drawFill(0, bodyY, screenW, bodyH, kClrBodyBg);

    // ================================================================
    //  LAYER 2: SCANLINE PASS — every other row, faint amber
    //  Only when fully open to avoid visual noise during slide
    // ================================================================
    if (m_slideT > 0.92f)
    {
        for (int sy = bodyY; sy < sep2Y; sy += 3)
            Text2D::drawFill(0, sy, screenW - kScrollW - kScrollGap, 1, kClrAmberFaint);
    }

    // ================================================================
    //  LAYER 3: HEADER BAR — solid black, title left, version right
    // ================================================================
    if (headerH > 0)
    {
        Text2D::drawFill(0, headerY, screenW, headerH, kClrBarBg);

        if (m_slideT > 0.5f && headerH >= ch)
        {
            // Vertically center text within the bar
            const int ty = headerY + (headerH - ch) / 2;

            Text2D::drawStringAlt(kPadX, ty, "NOVA ENGINE", kClrAmber);

            const char* ver = "v0.1.0  |  OpenGL 4.5";
            int verW = Text2D::stringWidth(ver);
            Text2D::drawString(screenW - verW - kPadX, ty, ver, kClrVersion);
        }
    }

    // ================================================================
    //  LAYER 4: BORDERS AND SEPARATORS
    // ================================================================
    // 2px amber top border
    Text2D::drawFill(0, 0, screenW, borderTopH, kClrBorderHi);

    // 1px dim separator below header
    Text2D::drawFill(0, sep1Y, screenW, kBorderSep, kClrBorderLo);

    // 1px amber separator above input
    Text2D::drawFill(0, sep2Y, screenW, kBorderSep, kClrBorderHi);

    // 2px amber left-edge accent strip
    Text2D::drawFill(0, bodyY, 2, bodyH, kClrAmberDim);

    // ================================================================
    //  LAYER 5: INPUT BAR BACKGROUND
    // ================================================================
    Text2D::drawFill(0, inputY, screenW, inputH, kClrBarBg);

    // ================================================================
    //  LAYER 6: SCROLLBACK TEXT
    // ================================================================
    const int textTop = bodyY + kPadY;
    const int textBot = sep2Y - kPadY;
    const int textH   = textBot - textTop;
    const int maxVisible = (lineH > 0) ? std::max(0, textH / lineH) : 0;

    const int total    = (int)m_scrollback.size();
    int       endIdx   = total - m_scroll;
    int       startIdx = endIdx - maxVisible;
    if (startIdx < 0) startIdx = 0;
    if (endIdx   > total) endIdx = total;

    for (int i = startIdx; i < endIdx; ++i)
    {
        const ConLine& entry = m_scrollback[i];
        int lineY = textTop + (i - startIdx) * lineH;
        if (lineY + ch > textBot) break;

        std::string displayText = entry.text;
        if ((int)displayText.size() > maxCharsPerLine && maxCharsPerLine > 3)
            displayText = displayText.substr(0, maxCharsPerLine - 1) + "~";

        const Vec4& col = colorFor(entry.color);

        if (useAltSet(entry.color))
            Text2D::drawStringShadowAlt(kPadX + 2, lineY, displayText.c_str(), col, kClrShadow, 1, 1);
        else
            Text2D::drawStringShadow(kPadX + 2, lineY, displayText.c_str(), col, kClrShadow, 1, 1);
    }

    // ================================================================
    //  LAYER 7: SCROLLBAR
    // ================================================================
    if (total > maxVisible && maxVisible > 0)
    {
        const int trackX = screenW - kScrollW - 2;
        const int trackY = textTop;
        const int trackH = textH;

        Text2D::drawFill(trackX, trackY, kScrollW, trackH, kClrScrollTrk);

        float visFrac    = (float)maxVisible / (float)total;
        int   thumbH     = std::max(8, (int)(visFrac * (float)trackH));
        float scrollFrac = (total > maxVisible)
                           ? (float)m_scroll / (float)(total - maxVisible)
                           : 0.f;
        int thumbY = trackY + (int)((1.0f - scrollFrac) * (float)(trackH - thumbH));
        thumbY = std::max(trackY, std::min(thumbY, trackY + trackH - thumbH));

        Text2D::drawFill(trackX, thumbY, kScrollW, thumbH, kClrScrollThm);

        if (m_scroll > 0 && m_slideT > 0.85f)
        {
            const char* hint = "[ scrolled ]";
            int hintW = Text2D::stringWidth(hint);
            int hintX = screenW - hintW - kScrollW - kScrollGap - kPadX;
            int hintY = textBot - lineH;
            if (hintY > textTop)
                Text2D::drawString(hintX, hintY, hint, kClrTextDim);
        }
    }

    // ================================================================
    //  LAYER 8: INPUT LINE
    //  Prompt ">" in amber, input text in off-white, blinking block cursor
    // ================================================================
    if (inputH >= ch)
    {
        const int padV = (inputH - ch) / 2;
        const int iy   = inputY + padV;

        const char* prompt = ">";
        Text2D::drawStringAlt(kPadX + 2, iy, prompt, kClrAmber);
        const int promptW   = Text2D::stringWidth(prompt) + cw / 2;
        const int textBaseX = kPadX + 2 + promptW;

        if (!m_inputBuf.empty())
        {
            std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
            std::string after  = m_inputBuf.substr((size_t)m_inputPos);

            Text2D::drawString(textBaseX, iy, before.c_str(), kClrText);
            int afterX = textBaseX + Text2D::stringWidth(before.c_str());
            Text2D::drawString(afterX, iy, after.c_str(), kClrText);
        }

        // Blinking cursor
        m_blinkAccum += dt;
        if (m_blinkAccum > 0.50f) { m_blinkAccum = 0.f; m_cursorVis = !m_cursorVis; }

        if (m_cursorVis)
        {
            std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
            int cx = textBaseX + Text2D::stringWidth(before.c_str());

            Text2D::drawFill(cx, iy, cw, ch, kClrAmber);

            if (m_inputPos < (int)m_inputBuf.size())
            {
                char buf[2] = { m_inputBuf[m_inputPos], '\0' };
                Text2D::drawString(cx, iy, buf, Vec4{0.02f, 0.02f, 0.02f, 1.0f});
            }
        }
    }

    Text2D::end();
}

} // namespace nova