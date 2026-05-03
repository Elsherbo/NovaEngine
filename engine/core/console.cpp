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

static constexpr int   kConH        = 420;
static constexpr int   kHeaderH     = 24;
static constexpr int   kInputH      = 30;
static constexpr int   kPadX        = 10;
static constexpr int   kPadY        = 5;
static constexpr int   kScrollW     = 5;
static constexpr int   kScrollGap   = 3;
static constexpr float kSlideSpeed  = 14.0f;

static constexpr Vec4 kBgMain    = { 0.039f, 0.047f, 0.063f, 0.93f };
static constexpr Vec4 kBgHeader  = { 0.059f, 0.075f, 0.094f, 1.00f };
static constexpr Vec4 kBgHeaderR = { 0.094f, 0.063f, 0.020f, 1.00f };
static constexpr Vec4 kBgInput   = { 0.020f, 0.024f, 0.031f, 1.00f };
static constexpr Vec4 kBgScanL   = { 0.000f, 0.000f, 0.000f, 0.06f };
static constexpr Vec4 kBgScanD   = { 0.000f, 0.000f, 0.000f, 0.00f };

static constexpr Vec4 kBorderAmber = { 0.784f, 0.471f, 0.125f, 0.90f };
static constexpr Vec4 kBorderDim   = { 0.200f, 0.220f, 0.260f, 0.60f };

static constexpr Vec4 kScrollTrack = { 0.067f, 0.078f, 0.098f, 1.00f };
static constexpr Vec4 kScrollThumb = { 0.620f, 0.373f, 0.098f, 0.90f };

static constexpr Vec4 kColTable[] = {
    { 0.847f, 0.816f, 0.753f, 1.00f }, // Output
    { 0.910f, 0.580f, 0.039f, 1.00f }, // Command
    { 0.910f, 0.188f, 0.125f, 1.00f }, // Error
    { 0.910f, 0.753f, 0.125f, 1.00f }, // Warn
    { 0.125f, 0.784f, 0.784f, 1.00f }, // System
    { 0.251f, 0.847f, 0.251f, 1.00f }, // Success
    { 0.282f, 0.267f, 0.251f, 1.00f }, // Dim
    { 1.000f, 0.720f, 0.200f, 1.00f }, // Accent
};

static constexpr Vec4 kColHeader  = { 0.910f, 0.580f, 0.039f, 1.00f };
static constexpr Vec4 kColVersion = { 0.400f, 0.440f, 0.520f, 1.00f };
static constexpr Vec4 kColPrompt  = { 0.910f, 0.580f, 0.039f, 1.00f };
static constexpr Vec4 kColCursor  = { 0.910f, 0.720f, 0.200f, 1.00f };
static constexpr Vec4 kColInput   = { 0.847f, 0.816f, 0.753f, 1.00f };
static constexpr Vec4 kShadow     = { 0.000f, 0.000f, 0.000f, 0.70f };

Console::Console()  = default;
Console::~Console() = default;

void Console::addLogLine(LogLevel level, const char* rawMsg)
{
    if (!rawMsg) return;

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

void Console::addLine(const std::string& line, ConColor color)
{
    m_scrollback.push_back({line, color});
    if ((int)m_scrollback.size() > kMaxLines)
        m_scrollback.erase(m_scrollback.begin());
}

void Console::submit(const char* line)
{
    if (!line || !*line) return;

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
    // FIX: split onto two lines to avoid -Wmisleading-indentation error
    if (cur.keys[SDL_SCANCODE_DELETE] && !prev.keys[SDL_SCANCODE_DELETE]) {
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
            char ch2 = shift ? m.s : m.p;
            m_inputBuf.insert(m_inputPos, 1, ch2);
            ++m_inputPos;
        }
    }
}

void Console::render(int screenW, int screenH)
{
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

    const int consoleH = (int)(kConH * m_slideT);
    const int headerH  = (int)(kHeaderH * m_slideT);
    const int inputY   = consoleH - kInputH;

    Text2D::init();
    Text2D::begin(screenW, screenH);

    Text2D::drawFill(0, 0, screenW, consoleH, kBgMain);

    if (m_slideT > 0.85f)
    {
        const int scanStart = kHeaderH + 1;
        const int scanEnd   = inputY;
        for (int sy = scanStart; sy < scanEnd; sy += 2)
            Text2D::drawFill(0, sy, screenW - kScrollW - kScrollGap, 1, kBgScanL);
    }

    if (headerH > 0)
    {
        Text2D::drawFillGradientH(0, 0, screenW, headerH, kBgHeader, kBgHeaderR);
        Text2D::drawFill(0, headerH, screenW, 2, kBorderAmber);

        if (m_slideT > 0.6f)
        {
            const int ch  = Text2D::charHeight();
            const int ty  = (headerH - ch) / 2;
            if (ty >= 0 && ty + ch <= headerH)
            {
                Text2D::drawStringShadow(kPadX, ty, "NOVA ENGINE",
                                         kColHeader, kShadow, 1, 1);
                const char* ver = "v0.1.0  |  OpenGL 4.5";
                int verW = Text2D::stringWidth(ver);
                Text2D::drawString(screenW - verW - kPadX, ty, ver, kColVersion);
            }
        }
    }

    Text2D::drawFill(0, inputY, screenW, kInputH, kBgInput);
    Text2D::drawFill(0, inputY, screenW, 1, kBorderAmber);
    Text2D::drawFill(0, consoleH - 2, screenW, 2, kBorderAmber);

    const int cw    = Text2D::charWidth();
    const int ch    = Text2D::charHeight();
    const int lineH = ch + 2;

    const int textTop = headerH + 2 + kPadY;
    const int textBot = inputY - kPadY;
    const int textH   = textBot - textTop;
    const int maxVisible = std::max(0, textH / lineH);

    const int total   = (int)m_scrollback.size();
    int       endIdx  = total - m_scroll;
    int       startIdx = endIdx - maxVisible;
    if (startIdx < 0) startIdx = 0;
    if (endIdx   > total) endIdx = total;

    const int textAreaW = screenW - kPadX * 2 - kScrollW - kScrollGap * 2;

    for (int i = startIdx; i < endIdx; ++i)
    {
        const ConLine& entry = m_scrollback[i];
        int lineY = textTop + (i - startIdx) * lineH;
        if (lineY + ch > textBot) break;

        const Vec4& color = kColTable[(int)entry.color];

        int maxChars = textAreaW / cw;
        if ((int)entry.text.size() > maxChars && maxChars > 3)
        {
            std::string trunc = entry.text.substr(0, maxChars - 1) + "~";
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

    if (total > maxVisible)
    {
        const int trackX = screenW - kScrollW - 2;
        const int trackY = textTop;
        const int trackH = textH;

        Text2D::drawFill(trackX, trackY, kScrollW, trackH, kScrollTrack);
        Text2D::drawFill(trackX - 1, trackY, 1, trackH, kBorderDim);

        float visFrac  = (float)maxVisible / (float)total;
        int   thumbH   = std::max(12, (int)(visFrac * (float)trackH));
        float scrollFrac = (total > maxVisible)
                           ? (float)m_scroll / (float)(total - maxVisible)
                           : 0.f;
        int thumbY = trackY + (int)((1.0f - scrollFrac) * (float)(trackH - thumbH));
        thumbY = std::max(trackY, std::min(thumbY, trackY + trackH - thumbH));

        Text2D::drawFill(trackX, thumbY, kScrollW, thumbH, kScrollThumb);
    }

    if (m_scroll > 0)
    {
        const char* hint = "[ SCROLL: PageUp/PageDn ]";
        int hintW = Text2D::stringWidth(hint);
        int hintX = (screenW - hintW) / 2;
        int hintY = textBot - lineH;
        if (hintY > textTop)
        {
            Text2D::drawFill(hintX - 4, hintY - 2, hintW + 8, lineH + 2,
                             Vec4{0.1f, 0.08f, 0.04f, 0.85f});
            Text2D::drawString(hintX, hintY, hint,
                               Vec4{0.91f, 0.72f, 0.20f, 0.95f});
        }
    }

    {
        const int padV = (kInputH - ch) / 2;
        const int iy   = inputY + padV;

        const char* prompt = "] ";
        Text2D::drawStringShadow(kPadX, iy, prompt, kColPrompt, kShadow, 1, 1);
        int cursorBaseX = kPadX + Text2D::stringWidth(prompt);

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

        m_blinkAccum += dt;
        if (m_blinkAccum > 0.50f) { m_blinkAccum = 0.f; m_cursorVis = !m_cursorVis; }

        if (m_cursorVis)
        {
            std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
            int cx = cursorBaseX + Text2D::stringWidth(before.c_str());

            Text2D::drawFill(cx, iy, cw, ch,
                             Vec4{0.91f, 0.58f, 0.039f, 0.75f});

            if (m_inputPos < (int)m_inputBuf.size())
            {
                char buf[2] = { m_inputBuf[m_inputPos], '\0' };
                Text2D::drawString(cx, iy, buf, Vec4{0.04f, 0.05f, 0.06f, 1.0f});
            }
        }
    }

    Text2D::drawFillGradientH(0, headerH + 2, 3, consoleH - headerH - 2,
                               Vec4{0.784f, 0.471f, 0.125f, 0.7f},
                               Vec4{0.784f, 0.471f, 0.125f, 0.0f});

    Text2D::end();
}

} // namespace nova