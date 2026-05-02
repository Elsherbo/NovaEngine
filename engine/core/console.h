// ============================================================
// FILE:    engine/core/console.h
// MODULE:  Core > Console
// VERSION: v7 — Professional Q2/Source Engine style
// ============================================================
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

    // ---- 6 semantic color categories (up from 5) ----
    enum class ConColor {
        Output,   // light gray — general info
        Command,  // green — echoed commands
        Error,    // red — errors
        Warn,     // amber — warnings
        System,   // blue — engine/system messages
        Success,  // teal — success confirmations
        Dim,      // dimmed — old scrollback hint
    };

    struct ConLine {
        std::string text;
        ConColor    color = ConColor::Output;
    };

    void addLine(const std::string& line, ConColor color = ConColor::Output);

    // Strip Logger timestamp prefix "[YYYY-MM-DD HH:MM:SS] [LEVEL] "
    // and classify by level before adding to scrollback.
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

    static constexpr int kMaxLines = 1024;  // doubled
    std::vector<ConLine> m_scrollback;
    int m_scroll = 0;

    GrabCallback m_mouseGrabCallback;

    double m_blinkAccum = 0.0;
    bool   m_cursorVis  = true;
};

} // namespace nova