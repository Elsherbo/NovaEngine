// ============================================================
// FILE:    engine/core/console.cpp
// MODULE:  Core > Console
// VERSION: v5 — Quake 2 / Source Engine style
//
// CHANGES vs v4:
//   - Input line is NOW AT THE BOTTOM of the console bar
//     (Quake 2 style: output scrolls above, "> " at bottom).
//   - Slide-down animation with smooth easing.
//   - Semi-transparent background bar with a bright top border.
//   - Output lines color-coded: command echo = bright green,
//     error lines (prefix "ERROR:") = red, others = light grey.
//   - Cursor position displayed as a solid block '|'.
//   - Tab-completion stub calls exec("help") as placeholder.
//   - Depends only on Text2D (no other render APIs).
// ============================================================

#include "engine/core/console.h"
#include "engine/core/text_2d.h"
#include "engine/core/cvar.h"
#include "engine/core/log.h"
#include "engine/platform/iplatform.h"

#include <SDL3/SDL_scancode.h>
#include <cstring>
#include <algorithm>
#include <cmath>

namespace nova
{

// ---- Console appearance constants ----
static constexpr int   kConsoleHeightPx = 240;    // fully-open height
static constexpr int   kPadX            = 8;       // left margin
static constexpr int   kPadY            = 6;       // top margin
static constexpr int   kInputBarH       = 18;      // pixels reserved for input line
static constexpr float kSlideSpeed      = 10.0f;   // open/close speed (larger = faster)

// ---- Colors ----
static constexpr Vec4 kColBg       = { 0.05f, 0.05f, 0.10f, 0.88f };
static constexpr Vec4 kColBorder   = { 0.30f, 0.50f, 0.80f, 1.00f };
static constexpr Vec4 kColPrompt   = { 0.25f, 1.00f, 0.30f, 1.00f };  // bright green
static constexpr Vec4 kColCommand  = { 0.25f, 1.00f, 0.30f, 1.00f };  // echoed command
static constexpr Vec4 kColOutput   = { 0.80f, 0.80f, 0.85f, 1.00f };  // normal output
static constexpr Vec4 kColError    = { 1.00f, 0.30f, 0.25f, 1.00f };  // error lines
static constexpr Vec4 kColWarn     = { 1.00f, 0.80f, 0.20f, 1.00f };  // warning lines
static constexpr Vec4 kColDim      = { 0.45f, 0.45f, 0.50f, 1.00f };  // old history

// ============================================================
// Constructor / Destructor
// ============================================================
Console::Console()  = default;
Console::~Console() = default;

// ============================================================
// submit — execute a command and echo it to the scrollback
// ============================================================
void Console::submit(const char* line)
{
    if (!line || !*line) return;

    // Echo the command
    m_scrollback.push_back(std::string("> ") + line);
    if ((int)m_scrollback.size() > kMaxLines)
        m_scrollback.erase(m_scrollback.begin());

    // Push to history (ignore duplicates at top)
    if (m_historyCount == 0 || m_history[0] != line)
    {
        for (int i = kMaxHistory - 1; i > 0; --i)
            m_history[i] = m_history[i - 1];
        m_history[0] = line;
        if (m_historyCount < kMaxHistory) ++m_historyCount;
    }
    m_historyIdx = -1;

    // Execute
    CVarSystem::instance().exec(line);

    // Also capture to Logger
    Logger::instance().info("[CON] > %s", line);

    m_scroll = 0;  // jump to bottom after submit
}

// ============================================================
// addLine — add a line to scrollback from outside the console
// (called by a Logger hook if you wire one up)
// ============================================================
void Console::addLine(const std::string& line)
{
    m_scrollback.push_back(line);
    if ((int)m_scrollback.size() > kMaxLines)
        m_scrollback.erase(m_scrollback.begin());
}

// ============================================================
// handleInput
// ============================================================
void Console::handleInput(const InputState& cur, const InputState& prev)
{
    // ---- Tilde toggle (edge detect) ----
    const bool grave = cur.keys[SDL_SCANCODE_GRAVE];
    if (grave && !m_prevGrave)
    {
        toggle();
        if (m_mouseGrabCallback)
            m_mouseGrabCallback(!m_open);
    }
    m_prevGrave = grave;

    if (!m_open) return;

    // ---- Escape: close ----
    if (cur.keys[SDL_SCANCODE_ESCAPE] && !prev.keys[SDL_SCANCODE_ESCAPE])
    {
        close();
        if (m_mouseGrabCallback) m_mouseGrabCallback(true);
        return;
    }

    // ---- Enter: submit ----
    if (cur.keys[SDL_SCANCODE_RETURN] && !prev.keys[SDL_SCANCODE_RETURN])
    {
        if (!m_inputBuf.empty())
            submit(m_inputBuf.c_str());
        m_inputBuf.clear();
        m_inputPos = 0;
        return;
    }

    // ---- History navigation (up / down) ----
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

    // ---- Scrollback (Page Up / Page Down) ----
    const int ch       = Text2D::charHeight();
    const int lineH    = ch + 2;
    const int textArea = kConsoleHeightPx - kInputBarH - kPadY * 2;
    const int linesPerPage = std::max(1, textArea / lineH);

    if (cur.keys[SDL_SCANCODE_PAGEUP] && !prev.keys[SDL_SCANCODE_PAGEUP])
    {
        m_scroll += linesPerPage;
        m_scroll  = std::min(m_scroll, (int)m_scrollback.size());
        return;
    }
    if (cur.keys[SDL_SCANCODE_PAGEDOWN] && !prev.keys[SDL_SCANCODE_PAGEDOWN])
    {
        m_scroll -= linesPerPage;
        m_scroll  = std::max(m_scroll, 0);
        return;
    }

    // ---- Editing: Backspace, Delete, Home, End, Arrows ----
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

    // ---- Tab: completion stub ----
    if (cur.keys[SDL_SCANCODE_TAB] && !prev.keys[SDL_SCANCODE_TAB])
    {
        // TODO: implement proper tab completion against CVar/command registry
        return;
    }

    // ---- Character input: scancode → ASCII mapping ----
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
        {SDL_SCANCODE_SPACE,     ' ', ' '}, {SDL_SCANCODE_MINUS,  '-','_'},
        {SDL_SCANCODE_EQUALS,    '=','+'}, {SDL_SCANCODE_LEFTBRACKET, '[','{'},
        {SDL_SCANCODE_RIGHTBRACKET,']','}'},{SDL_SCANCODE_SEMICOLON,  ';',':'},
        {SDL_SCANCODE_APOSTROPHE,'\'','"'},{SDL_SCANCODE_COMMA,       ',','<'},
        {SDL_SCANCODE_PERIOD,    '.','>'},{SDL_SCANCODE_SLASH,        '/','?'},
        {SDL_SCANCODE_BACKSLASH, '\\','|'},
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
// render
// ============================================================
void Console::render(int screenW, int screenH)
{
    const float dt = 0.016f; // assume ~60 fps; good enough for slide animation

    // ---- Slide animation ----
    // m_slideT: 0.0 = fully closed, 1.0 = fully open
    float target = m_open ? 1.0f : 0.0f;
    float diff   = target - m_slideT;
    if (std::abs(diff) > 0.001f)
        m_slideT += diff * kSlideSpeed * dt;
    else
        m_slideT = target;

    if (m_slideT <= 0.001f) return;  // fully closed — nothing to draw

    Text2D::init();

    const int consoleH = (int)(kConsoleHeightPx * m_slideT);
    const int consoleY = 0;  // anchored to top of screen

    Text2D::begin(screenW, screenH);

    // ---- Background ----
    Text2D::drawFill(0, consoleY, screenW, consoleH, kColBg);

    // ---- Top border — 2px bright line ----
    // (Draw as a thin filled rectangle in border color)
    Text2D::drawFill(0, consoleY + consoleH - 2, screenW, 2, kColBorder);

    // ---- Scrollback lines ----
    const int ch    = Text2D::charHeight();
    const int lineH = ch + 2;

    // Available height: full bar minus bottom input area and padding
    const int textAreaTop = consoleY + kPadY;
    const int textAreaBot = consoleY + consoleH - kInputBarH - kPadY;
    const int textAreaH   = textAreaBot - textAreaTop;
    const int maxLines    = std::max(0, textAreaH / lineH);

    const int total = (int)m_scrollback.size();
    // Index of the BOTTOM-MOST visible line (most recent, adjusted for scroll)
    int endIdx   = total - m_scroll;
    int startIdx = endIdx - maxLines;
    if (startIdx < 0) startIdx = 0;
    if (endIdx   > total) endIdx = total;

    // Draw top-to-bottom
    for (int i = startIdx; i < endIdx; ++i)
    {
        const std::string& line = m_scrollback[i];
        int lineY = textAreaTop + (i - startIdx) * lineH;
        if (lineY + ch > textAreaBot) break;

        // Color-code by prefix
        Vec4 col = kColOutput;
        if (line.size() >= 2 && line[0] == '>' && line[1] == ' ')
            col = kColCommand;
        else if (line.size() >= 6 && line.substr(0, 6) == "ERROR:")
            col = kColError;
        else if (line.size() >= 5 && line.substr(0, 5) == "WARN:")
            col = kColWarn;
        else if (i < endIdx - 8)
            col = kColDim;  // dim older lines

        // Clip text to console width (leave right margin)
        const int maxChars = (screenW - kPadX * 2) / Text2D::charWidth();
        std::string display = (line.size() > (size_t)maxChars)
                              ? line.substr(0, maxChars - 2) + ".."
                              : line;

        Text2D::drawString(kPadX, lineY, display.c_str(), col);
    }

    // ---- Scroll indicator ----
    if (m_scroll > 0)
    {
        const char* scrollMsg = "[ SCROLL UP - PageDn to return ]";
        int msgX = screenW / 2 - Text2D::stringWidth(scrollMsg) / 2;
        Text2D::drawString(msgX, textAreaBot - lineH, scrollMsg,
                           Vec4{1.0f, 1.0f, 0.0f, 0.8f});
    }

    // ---- Input line ----
    // Drawn at the very bottom of the console bar, Quake-style.
    const int inputY = consoleY + consoleH - kPadY - ch;

    // Separator line above input
    Text2D::drawFill(kPadX, inputY - 2, screenW - kPadX * 2, 1,
                     Vec4{0.20f, 0.30f, 0.45f, 0.80f});

    // Prompt character
    const char* prompt = "> ";
    Text2D::drawString(kPadX, inputY, prompt, kColPrompt);

    int inputX = kPadX + Text2D::stringWidth(prompt);

    // Input text — draw chars before and after cursor separately
    if (!m_inputBuf.empty())
    {
        std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
        std::string after  = m_inputBuf.substr((size_t)m_inputPos);
        Text2D::drawString(inputX, inputY, before.c_str(), kColPrompt);
        int afterX = inputX + Text2D::stringWidth(before.c_str());
        Text2D::drawString(afterX + 8, inputY, after.c_str(), kColPrompt);
    }

    // Cursor: blinking block drawn AT the cursor position
    m_blinkAccum += dt;
    if (m_blinkAccum > 0.53f) { m_blinkAccum = 0.0f; m_cursorVis = !m_cursorVis; }

    if (m_cursorVis)
    {
        std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
        int curX = inputX + Text2D::stringWidth(before.c_str());
        // Draw cursor as filled rectangle (2px wide, full char height)
        Text2D::drawFill(curX, inputY, 2, ch, kColPrompt);
    }

    Text2D::end();
}

} // namespace nova