// ============================================================
// FILE:    engine/core/console.h  (v4 — Text2D + history + scrollback)
// MODULE:  Core > Console
// ============================================================
#pragma once

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

private:
    void submit(const char* line);

    bool        m_open      = false;
    std::string m_inputBuf;
    int         m_inputPos  = 0;  // cursor position in inputBuf
    bool        m_prevGrave = false;

    // Command history
    static constexpr int kMaxHistory = 64;
    std::array<std::string, kMaxHistory> m_history;
    int m_historyCount = 0;   // number of stored commands
    int m_historyIdx   = -1;  // current browse position (-1 = typing new)

    // Scrollback buffer (previous command outputs)
    static constexpr int kMaxLines = 512;
    std::vector<std::string> m_scrollback;

    // Display scroll position (0 = bottom/most recent)
    int m_scroll = 0;

    GrabCallback m_mouseGrabCallback;

    // Blink
    double m_blinkAccum = 0.0;
    bool   m_cursorVis  = true;
};

} // namespace nova
