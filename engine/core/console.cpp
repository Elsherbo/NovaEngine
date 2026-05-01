// ============================================================
// FILE:    engine/core/console.cpp  (v4 — Text2D + history + scrollback)
// KEY FEATURES:
//   - Quake2-style overlay using Text2D rendering
//   - Command history (up/down arrows)
//   - Scrollback buffer (page up/down)
//   - Proper sRGB handling via Text2D
// ============================================================

#include "engine/core/console.h"
#include "engine/core/text_2d.h"
#include "engine/core/cvar.h"
#include "engine/core/log.h"
#include "engine/platform/iplatform.h"

#include <SDL3/SDL_scancode.h>
#include <cstring>

namespace nova
{

static const int kBarH = 200;       // console bar height in pixels
static const int kPadX = 8;         // left/right padding
static const int kLineGap = 2;      // gap between text lines

Console::Console() = default;

Console::~Console()
{
}

// ---------------------------------------------------------------------------
void Console::submit(const char* line)
{
    if (!line || !*line) return;

    // Echo to scrollback
    m_scrollback.push_back(std::string("> ") + line);

    // Add to history
    for (int i = kMaxHistory - 1; i > 0; --i)
        m_history[i] = m_history[i - 1];
    m_history[0] = line;
    if (m_historyCount < kMaxHistory) ++m_historyCount;
    m_historyIdx = -1;  // reset browse to typing

    // Execute command
    CVarSystem::instance().exec(line);

    // Also log to Logger for console output capture
    Logger::instance().info("> %s", line);

    // Reset scroll to bottom
    m_scroll = 0;
}

// ---------------------------------------------------------------------------
void Console::handleInput(const InputState& cur, const InputState& prev)
{
    const bool grave = cur.keys[SDL_SCANCODE_GRAVE];
    if (grave && !m_prevGrave) {
        toggle();
        if (m_mouseGrabCallback) m_mouseGrabCallback(!m_open);
    }
    m_prevGrave = grave;
    if (!m_open) return;

    // ---- Escape: close console ----
    if (cur.keys[SDL_SCANCODE_ESCAPE] && !prev.keys[SDL_SCANCODE_ESCAPE]) {
        close();
        if (m_mouseGrabCallback) m_mouseGrabCallback(true);
        return;
    }

    // ---- Enter: submit ----
    if (cur.keys[SDL_SCANCODE_RETURN] && !prev.keys[SDL_SCANCODE_RETURN]) {
        if (!m_inputBuf.empty())
            submit(m_inputBuf.c_str());
        m_inputBuf.clear();
        m_inputPos = 0;
        return;
    }

    // ---- Up arrow: history ----
    if (cur.keys[SDL_SCANCODE_UP] && !prev.keys[SDL_SCANCODE_UP]) {
        if (m_historyIdx < m_historyCount - 1) {
            m_inputBuf = m_history[++m_historyIdx];
            m_inputPos = (int)m_inputBuf.size();
        }
        return;
    }

    // ---- Down arrow: history ----
    if (cur.keys[SDL_SCANCODE_DOWN] && !prev.keys[SDL_SCANCODE_DOWN]) {
        if (m_historyIdx > 0) {
            m_inputBuf = m_history[--m_historyIdx];
            m_inputPos = (int)m_inputBuf.size();
        } else if (m_historyIdx == 0) {
            m_historyIdx = -1;
            m_inputBuf.clear();
            m_inputPos = 0;
        }
        return;
    }

    // ---- Page Up / Page Down: scrollback ----
    const int linesPerPage = (kBarH / (Text2D::charHeight() + kLineGap)) - 1;
    if (cur.keys[SDL_SCANCODE_PAGEUP] && !prev.keys[SDL_SCANCODE_PAGEUP]) {
        m_scroll += linesPerPage;
        if (m_scroll > (int)m_scrollback.size())
            m_scroll = (int)m_scrollback.size();
        return;
    }
    if (cur.keys[SDL_SCANCODE_PAGEDOWN] && !prev.keys[SDL_SCANCODE_PAGEDOWN]) {
        m_scroll -= linesPerPage;
        if (m_scroll < 0) m_scroll = 0;
        return;
    }

    // ---- Backspace ----
    if (cur.keys[SDL_SCANCODE_BACKSPACE] && !prev.keys[SDL_SCANCODE_BACKSPACE]) {
        if (m_inputPos > 0 && !m_inputBuf.empty()) {
            m_inputBuf.erase(m_inputPos - 1, 1);
            --m_inputPos;
        }
        return;
    }

    // ---- Delete ----
    if (cur.keys[SDL_SCANCODE_DELETE] && !prev.keys[SDL_SCANCODE_DELETE]) {
        if (m_inputPos < (int)m_inputBuf.size()) {
            m_inputBuf.erase(m_inputPos, 1);
        }
        return;
    }

    // ---- Home / End ----
    if (cur.keys[SDL_SCANCODE_HOME] && !prev.keys[SDL_SCANCODE_HOME]) {
        m_inputPos = 0;
        return;
    }
    if (cur.keys[SDL_SCANCODE_END] && !prev.keys[SDL_SCANCODE_END]) {
        m_inputPos = (int)m_inputBuf.size();
        return;
    }

    // ---- Left / Right arrow ----
    if (cur.keys[SDL_SCANCODE_LEFT] && !prev.keys[SDL_SCANCODE_LEFT]) {
        if (m_inputPos > 0) --m_inputPos;
        return;
    }
    if (cur.keys[SDL_SCANCODE_RIGHT] && !prev.keys[SDL_SCANCODE_RIGHT]) {
        if (m_inputPos < (int)m_inputBuf.size()) ++m_inputPos;
        return;
    }

    // ---- Tab: completion (stub) ----
    if (cur.keys[SDL_SCANCODE_TAB] && !prev.keys[SDL_SCANCODE_TAB]) {
        // TODO: implement tab completion
        return;
    }

    // ---- Character input ----
    struct Map { int sc; char plain; char shifted; };
    static const Map kMap[] = {
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
    const bool sh = cur.keys[SDL_SCANCODE_LSHIFT] || cur.keys[SDL_SCANCODE_RSHIFT];
    for (const auto& m : kMap)
        if (cur.keys[m.sc] && !prev.keys[m.sc] && m_inputBuf.size() < 255) {
            char ch = sh ? m.shifted : m.plain;
            m_inputBuf.insert(m_inputPos, 1, ch);
            ++m_inputPos;
        }
}

// ---------------------------------------------------------------------------
void Console::render(int screenW, int screenH)
{
    if (!m_open) return;

    Text2D::init();
    Text2D::begin(screenW, screenH);

    const int ch = Text2D::charHeight();
    const int lineH = ch + kLineGap;
    const int maxLines = (kBarH - 8) / lineH;
    const int promptY = screenH - kBarH + kPadX;

    // Background bar
    Text2D::drawFill(0, screenH - kBarH, screenW, kBarH, Vec4(0.02f, 0.02f, 0.06f, 0.92f));

    // Scrollback lines (top to bottom, newest at bottom)
    const Vec4 textColor = Vec4(0.75f, 0.75f, 0.80f, 1.0f);  // bright gray, not too bright
    const Vec4 dimColor  = Vec4(0.40f, 0.40f, 0.45f, 1.0f);  // dim text for old lines

    int totalScrollLines = (int)m_scrollback.size();
    int startIdx = totalScrollLines - maxLines + m_scroll;
    if (startIdx < 0) startIdx = 0;
    int endIdx = startIdx + maxLines;
    if (endIdx > totalScrollLines) endIdx = totalScrollLines;

    int textY = screenH - kBarH + kPadX;
    for (int i = startIdx; i < endIdx && textY + ch < promptY; ++i)
    {
        const std::string& line = m_scrollback[i];
        bool isCommand = (line.size() > 2 && line[0] == '>' && line[1] == ' ');
        Text2D::drawString(kPadX, textY, line.c_str(), isCommand ? textColor : dimColor);
        textY += lineH;
    }

    // Prompt line: "> input with cursor"
    m_blinkAccum += 0.016;
    if (m_blinkAccum > 0.5) { m_blinkAccum = 0.0; m_cursorVis = !m_cursorVis; }

    Vec4 promptColor = Vec4(0.15f, 1.0f, 0.25f, 1.0f);
    std::string prompt = "> ";
    Text2D::drawString(kPadX, promptY, prompt.c_str(), promptColor);

    int inputX = kPadX + Text2D::stringWidth(prompt.c_str());
    Text2D::drawString(inputX, promptY, m_inputBuf.c_str(), promptColor);

    // Cursor
    if (m_cursorVis)
    {
        int cursorX = inputX + Text2D::stringWidth(m_inputBuf.substr(0, m_inputPos).c_str());
        Text2D::drawChar(cursorX, promptY, '_', promptColor);
    }

    Text2D::end();
}

} // namespace nova
