#pragma once

#include "engine/core/math/vec.h"
#include "engine/core/log.h"

#include <string>
#include <vector>
#include <functional>
#include <array>

namespace nova
{

struct InputState;

// ---- Color categories ----
// Note: underlying type removed — GCC 15 -Wpedantic rejects
// "enum class X : T" in some elaborated-type-specifier contexts.
enum class ConColor {
    Output  = 0,  // default text — warm off-white
    Command = 1,  // echoed commands — amber
    Error   = 2,  // errors — red-orange
    Warn    = 3,  // warnings — yellow
    System  = 4,  // engine messages — cyan
    Success = 5,  // success — green
    Dim     = 6,  // old lines / hints — dark gray
    Accent  = 7,  // highlighted / important — bright amber
};

struct ConLine {
    std::string text;
    ConColor    color = ConColor::Output;
};

class Console
{
public:
    Console();
    ~Console();

    void handleInput(const InputState& cur, const InputState& prev);
    void render(int screenW, int screenH);

    using GrabCallback = std::function<void(bool grab)>;
    void setMouseGrabCallback(GrabCallback cb) { m_mouseGrabCallback = cb; }

    bool isOpen()  const { return m_open; }
    void open()          { m_open = true; }
    void close()         { m_open = false; m_inputBuf.clear(); m_historyIdx = -1; }
    void toggle()        { m_open ? close() : open(); }

    void addLine(const std::string& line, ConColor color = ConColor::Output);
    void addLogLine(LogLevel level, const char* rawMsg);

private:
    void submit(const char* line);
    void wrapAndAdd(const std::string& text, ConColor color, int maxCols);

    bool        m_open      = false;
    std::string m_inputBuf;
    int         m_inputPos  = 0;
    bool        m_prevGrave = false;

    float       m_slideT    = 0.0f;

    static constexpr int kMaxHistory = 64;
    std::array<std::string, kMaxHistory> m_history;
    int m_historyCount = 0;
    int m_historyIdx   = -1;

    static constexpr int kMaxLines = 2048;
    std::vector<ConLine> m_scrollback;
    int m_scroll = 0;

    GrabCallback m_mouseGrabCallback;

    double m_blinkAccum = 0.0;
    bool   m_cursorVis  = true;
};

} // namespace nova