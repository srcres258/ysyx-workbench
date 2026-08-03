#ifndef __TUI_OVERLAY_HPP__
#define __TUI_OVERLAY_HPP__ 1

#include <cstdint>
#include <string>
#include <vector>
#include <tui/renderer.hpp>

namespace tui {

class Overlay {
public:
    Overlay();

    bool active() const {
        return m_active;
    }

    void toggle();
    void open();
    void close();

    std::string execute();

    std::string inputBuffer() const {
        return m_input;
    }

    void appendOutput(const std::string &line);

    void insertChar(char ch);
    void backspace();
    void moveCursorLeft();
    void moveCursorRight();
    void moveCursorHome();
    void moveCursorEnd();
    void historyPrev();
    void historyNext();

    void render(Canvas &canvas, uint16_t rows, uint16_t cols);

    bool quitRequested() const {
        return m_quitRequested;
    }
    void setQuitRequested(bool v) {
        m_quitRequested = v;
    }
    void dispatchCommand(const std::string &cmd);

private:
    bool        m_active       = false;
    std::string m_input;
    size_t      m_cursorPos    = 0;

    std::vector<std::string> m_history;
    std::vector<std::string> m_output;
    int         m_historyIdx = -1;
    bool        m_quitRequested = false;
};

} // namespace tui

#endif /* __TUI_OVERLAY_HPP__ */
