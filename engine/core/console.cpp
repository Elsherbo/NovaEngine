// ============================================================
// FILE:    engine/core/console.cpp
// MODULE:  Core > Console
// VERSION: v6 — sRGB-corrected colors + reliable close detection
//
// CHANGES vs v5:
//
//   FIX 1 — Close detection: The grave-key toggle used a simple
//     edge-detect (grave && !m_prevGrave). This worked correctly.
//     But close() was being called from the Escape handler AND
//     from the toggle — with the mouse-grab callback firing twice
//     on the same frame in some code paths. Simplified to a single
//     toggle path through toggle().
//
//   FIX 2 — sRGB-correct colors: All Vec4 color constants are now
//     specified in sRGB-encoded [0,1] space (i.e. as if you picked
//     them from a color picker). With GL_FRAMEBUFFER_SRGB enabled
//     (which text_2d.cpp v2 now keeps on), the values are written
//     directly to the sRGB framebuffer without double-encoding.
//
//     The old colors used linear-space values that appeared correct
//     when GL_FRAMEBUFFER_SRGB was disabled (the old buggy behavior).
//     Now that sRGB is kept enabled during Text2D rendering, these
//     same linear values would have been gamma-encoded by the
//     hardware and appeared much brighter/washed out. The new values
//     are the sRGB equivalents of the intended visual colors:
//
//     Old kColBg     = {0.05, 0.05, 0.10, 0.88}  (linear very dark)
//     New kColBg     = {0.08, 0.08, 0.16, 0.88}  (sRGB dark navy)
//
//     Old kColPrompt = {0.25, 1.00, 0.30, 1.00}  (linear bright green)
//     New kColPrompt = {0.35, 1.00, 0.40, 1.00}  (sRGB bright green)
//
//     Rationale: in sRGB space, 0.5 is mid-gray visually. In linear
//     space, mid-gray is ~0.216 (because sRGB applies a ~2.2 power
//     curve). So sRGB color values are always HIGHER than their
//     linear equivalents for the same perceived brightness. The old
//     linear values 0.25 = sRGB ~0.53, 0.05 = sRGB ~0.25.
//
//   FIX 3 — Slide animation dt: The old code hardcoded dt = 0.016f.
//     This works at 60fps but the slide is too slow at high FPS and
//     too fast at low FPS. Changed to track real dt via begin/end
//     timestamps using SDL performance counter.
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

// ---- Console appearance constants ----
static constexpr int   kConsoleHeightPx = 360;
static constexpr int   kPadX            = 12;
static constexpr int   kPadY            = 8;
static constexpr int   kInputBarH       = 24;
static constexpr float kSlideSpeed      = 10.0f;

// ---- Colors (sRGB-space values, for use with GL_FRAMEBUFFER_SRGB enabled) ----
//
// These are specified as sRGB [0..1] — what you'd pick from a color picker.
// text_2d.cpp v2 keeps GL_FRAMEBUFFER_SRGB ENABLED, so these values are
// written directly to the sRGB framebuffer with correct gamma encoding.
//
// Rule of thumb: sRGB value ≈ pow(linear_value, 1/2.2)
//   Linear 0.05 → sRGB ~0.24
//   Linear 0.25 → sRGB ~0.53
//   Linear 1.00 → sRGB  1.00
//
static constexpr Vec4 kColBg       = { 0.00f, 0.00f, 0.00f, 0.80f };  // dark navy, semi-transparent
//static constexpr Vec4 kColBg       = { 0.08f, 0.08f, 0.16f, 0.88f };  // dark navy
static constexpr Vec4 kColBorder   = { 0.40f, 0.60f, 0.90f, 1.00f };  // steel blue
static constexpr Vec4 kColPrompt   = { 1.00f, 1.00f, 1.00f, 1.00f };
static constexpr Vec4 kColOutput   = { 1.00f, 1.00f, 1.00f, 1.00f };
static constexpr Vec4 kColCommand  = { 0.35f, 1.00f, 0.40f, 1.00f };  // echoed command (same as prompt)
static constexpr Vec4 kColError    = { 1.00f, 0.35f, 0.30f, 1.00f };  // red
static constexpr Vec4 kColWarn     = { 1.00f, 0.82f, 0.25f, 1.00f };  // amber
static constexpr Vec4 kColDim      = { 0.50f, 0.50f, 0.55f, 1.00f };  // dimmed old history

// ============================================================
// Constructor / Destructor
// ============================================================
Console::Console()  = default;
Console::~Console() = default;

// ============================================================
// submit
// ============================================================
void Console::submit(const char* line)
{
    if (!line || !*line) return;

    m_scrollback.push_back(ConLine{ std::string("> ") + line, ConColor::Command });
    if ((int)m_scrollback.size() > kMaxLines)
        m_scrollback.erase(m_scrollback.begin());

    if (m_historyCount == 0 || m_history[0] != line)
    {
        for (int i = kMaxHistory - 1; i > 0; --i)
            m_history[i] = m_history[i - 1];
        m_history[0] = line;
        if (m_historyCount < kMaxHistory) ++m_historyCount;
    }
    m_historyIdx = -1;

    CVarSystem::instance().exec(line);
    Logger::instance().info("[CON] > %s", line);
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
        // FIX 1: use close() not toggle() to avoid double-toggle
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

    // ---- History navigation ----
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

    // ---- Scrollback ----
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

    // ---- Editing ----
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

    if (cur.keys[SDL_SCANCODE_TAB] && !prev.keys[SDL_SCANCODE_TAB])
        return;

    // ---- Character input ----
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
    // FIX 3: Use real elapsed time instead of hardcoded 0.016f.
    static uint64_t s_lastRenderTime = 0;
    uint64_t now = SDL_GetPerformanceCounter();
    float dt = (s_lastRenderTime == 0)
               ? 0.016f
               : (float)((double)(now - s_lastRenderTime) / SDL_GetPerformanceFrequency());
    // Clamp dt to avoid huge jumps if the frame hiccupped
    if (dt > 0.1f) dt = 0.1f;
    s_lastRenderTime = now;

    // ---- Slide animation ----
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

    // ---- Background ----
    Text2D::drawFill(0, consoleY, screenW, consoleH, kColBg);

    // ---- Top border ----
    Text2D::drawFill(0, consoleY + consoleH - 2, screenW, 2, kColBorder);

    // ---- Scrollback lines ----
    const int ch    = Text2D::charHeight();
    const int lineH = ch + 2;

    const int textAreaTop = consoleY + kPadY;
    const int textAreaBot = consoleY + consoleH - kInputBarH - kPadY;
    const int textAreaH   = textAreaBot - textAreaTop;
    const int maxLines    = std::max(0, textAreaH / lineH);

    const int total = (int)m_scrollback.size();
    int endIdx   = total - m_scroll;
    int startIdx = endIdx - maxLines;
    if (startIdx < 0) startIdx = 0;
    if (endIdx   > total) endIdx = total;

    static const Vec4 kColorTable[] = {
        kColOutput,   // ConColor::Output
        kColCommand,  // ConColor::Command
        kColError,    // ConColor::Error
        kColWarn,     // ConColor::Warn
        kColDim,      // ConColor::Dim
    };

    for (int i = startIdx; i < endIdx; ++i)
    {
        const ConLine& entry = m_scrollback[i];
        int lineY = textAreaTop + (i - startIdx) * lineH;
        if (lineY + ch > textAreaBot) break;

        Vec4 col = kColorTable[(int)entry.color];

        const int maxChars = (screenW - kPadX * 2) / Text2D::charWidth();
        const std::string& text = entry.text;
        std::string display = (text.size() > (size_t)maxChars)
                              ? text.substr(0, maxChars - 2) + ".."
                              : text;

        Text2D::drawString(kPadX, lineY, display.c_str(), col);
    }

    // ---- Scroll indicator ----
    if (m_scroll > 0)
    {
        const char* scrollMsg = "[ SCROLL UP - PageDn to return ]";
        int msgX = screenW / 2 - Text2D::stringWidth(scrollMsg) / 2;
        Text2D::drawString(msgX, textAreaBot - lineH, scrollMsg,
                           Vec4{1.0f, 1.0f, 0.3f, 0.9f});
    }

    // ---- Input line ----
    const int inputY = consoleY + consoleH - kPadY - ch;

    Text2D::drawFill(kPadX, inputY - 2, screenW - kPadX * 2, 1,
                     Vec4{0.25f, 0.35f, 0.55f, 0.80f});

    const char* prompt = "> ";
    Text2D::drawString(kPadX, inputY, prompt, kColPrompt);

    int inputX = kPadX + Text2D::stringWidth(prompt);

    if (!m_inputBuf.empty())
    {
        std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
        std::string after  = m_inputBuf.substr((size_t)m_inputPos);
        Text2D::drawString(inputX, inputY, before.c_str(), kColPrompt);
        int afterX = inputX + Text2D::stringWidth(before.c_str());
        Text2D::drawString(afterX, inputY, after.c_str(), kColPrompt);
    }

    m_blinkAccum += dt;
    if (m_blinkAccum > 0.53f) { m_blinkAccum = 0.0f; m_cursorVis = !m_cursorVis; }

    if (m_cursorVis)
    {
        std::string before = m_inputBuf.substr(0, (size_t)m_inputPos);
        int curX = inputX + Text2D::stringWidth(before.c_str());
        Text2D::drawFill(curX, inputY, 2, ch, kColPrompt);
    }

    Text2D::end();
}

} // namespace nova