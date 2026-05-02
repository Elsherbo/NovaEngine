// ============================================================
// FILE:    engine/core/console.h  (v5 — Quake 2 / Source Engine style)
// MODULE:  Core > Console
// ============================================================
#pragma once

#include "engine/core/math/vec.h"

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

    // Called each frame BEFORE gameplay input handling.
    void handleInput(const InputState& cur, const InputState& prev);

    // Call in render(), after all 3D geometry, before present().
    void render(int screenW, int screenH);

    // Set a callback so the console can release/re-grab the mouse
    // when it opens/closes.  Argument: true = grab, false = release.
    using GrabCallback = std::function<void(bool grab)>;
    void setMouseGrabCallback(GrabCallback cb) { m_mouseGrabCallback = cb; }

    bool isOpen() const { return m_open; }
    void open()         { m_open = true; }
    void close()        { m_open = false; m_inputBuf.clear(); m_historyIdx = -1; }
    void toggle()       { m_open ? close() : open(); }

    enum class ConColor { Output, Command, Error, Warn, Dim };
    struct ConLine {
        std::string text;
        ConColor    color = ConColor::Output;
    };

    // Add a line to scrollback from outside (e.g. Logger hook)
    void addLine(const std::string& line, ConColor color = ConColor::Output);

private:
    void submit(const char* line);

    bool        m_open      = false;
    std::string m_inputBuf;
    int         m_inputPos  = 0;  // cursor position in inputBuf
    bool        m_prevGrave = false;

    // Slide animation state (0.0 = closed, 1.0 = fully open)
    float       m_slideT    = 0.0f;

    // Command history
    static constexpr int kMaxHistory = 64;
    std::array<std::string, kMaxHistory> m_history;
    int m_historyCount = 0;   // number of stored commands
    int m_historyIdx   = -1;  // current browse position (-1 = typing new)

    // Scrollback buffer (previous command outputs)
    static constexpr int kMaxLines = 512;

    std::vector<ConLine> m_scrollback;  // was vector<string>

    // Display scroll position (0 = bottom/most recent)
    int m_scroll = 0;

    GrabCallback m_mouseGrabCallback;

    // Blink
    double m_blinkAccum = 0.0;
    bool   m_cursorVis  = true;
};

} // namespace nova
