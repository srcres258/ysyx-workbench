#include <tui/tui_overlay.hpp>
#include <tui/tui_control.hpp>
#include <sdb.hpp>
#include <utils.hpp>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <sstream>

namespace tui {

Overlay::Overlay() {}

void Overlay::toggle() {
    m_active = !m_active;
    if (m_active) {
        m_input.clear();
        m_cursorPos = 0;
        m_historyIdx = -1;
    }
    if (!m_active) {
        m_input.clear();
        m_cursorPos = 0;
        m_historyIdx = -1;
    }
}

void Overlay::open() {
    m_active = true;
    m_input.clear();
    m_cursorPos = 0;
    m_historyIdx = -1;
}

void Overlay::close() {
    m_active = false;
    m_input.clear();
    m_cursorPos = 0;
    m_historyIdx = -1;
}

std::string Overlay::execute() {
    std::string cmd = m_input;
    if (!cmd.empty()) {
        m_history.push_back(cmd);
        m_output.push_back("> " + cmd);
    }
    m_input.clear();
    m_cursorPos = 0;
    m_historyIdx = -1;
    return cmd;
}

void Overlay::appendOutput(const std::string &line) {
    m_output.push_back(line);
}

void Overlay::insertChar(char ch) {
    if (ch < 32 || ch > 126)
        return;
    m_input.insert(m_cursorPos, 1, ch);
    ++m_cursorPos;
}

void Overlay::backspace() {
    if (m_cursorPos > 0) {
        --m_cursorPos;
        m_input.erase(m_cursorPos, 1);
    }
}

void Overlay::moveCursorLeft() {
    if (m_cursorPos > 0)
        --m_cursorPos;
}

void Overlay::moveCursorRight() {
    if (m_cursorPos < m_input.size())
        ++m_cursorPos;
}

void Overlay::moveCursorHome() {
    m_cursorPos = 0;
}

void Overlay::moveCursorEnd() {
    m_cursorPos = m_input.size();
}

void Overlay::historyPrev() {
    if (m_history.empty())
        return;
    if (m_historyIdx == -1) {
        m_historyIdx = static_cast<int>(m_history.size()) - 1;
    } else if (m_historyIdx > 0) {
        --m_historyIdx;
    }
    m_input = m_history[m_historyIdx];
    m_cursorPos = m_input.size();
}

void Overlay::historyNext() {
    if (m_historyIdx == -1)
        return;
    if (m_historyIdx < static_cast<int>(m_history.size()) - 1) {
        ++m_historyIdx;
        m_input = m_history[m_historyIdx];
    } else {
        m_historyIdx = -1;
        m_input.clear();
    }
    m_cursorPos = m_input.size();
}

void Overlay::dispatchCommand(const std::string &cmd) {
    if (cmd.empty())
        return;

    std::istringstream iss(cmd);
    std::string token;
    iss >> token;
    if (token.empty())
        return;

    std::string rest;
    std::getline(iss, rest);
    size_t pos = rest.find_first_not_of(" \t");
    const char *args = (pos != std::string::npos) ? rest.c_str() + pos : "";

    if (token == "q" || token == "quit") {
        m_quitRequested = true;
        m_output.push_back("Quitting...");
    } else if (token == "c" || token == "continue") {
        m_output.push_back("Continuing simulation...");
        requestSimContinue();
    } else if (token == "si") {
        int n = (args && *args) ? std::atoi(args) : 1;
        if (n <= 0)
            n = 1;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Stepping %d instruction(s)...", n);
        m_output.push_back(buf);
        requestSimStepInst(static_cast<uint64_t>(n));
    } else if (token == "sic") {
        int n = (args && *args) ? std::atoi(args) : 1;
        if (n <= 0)
            n = 1;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Stepping %d clock period(s)...", n);
        m_output.push_back(buf);
        requestSimStepClock(static_cast<uint64_t>(n));
    } else if (token == "reset") {
        m_output.push_back("Resetting simulation...");
        requestSimReset();
    } else if (token == "pause") {
        m_output.push_back("Pausing simulation...");
        requestSimPause();
    } else if (token == "help") {
        m_output.push_back("Commands: c(continue), si [N], sic [N], reset, pause, q(quit)");
        m_output.push_back("  info r/w, x N EXPR, p EXPR, w EXPR, d N, help");
    } else if (token == "info") {
        std::string sub;
        std::istringstream riss(args ? args : "");
        riss >> sub;
        if (sub == "r") {
            std::string out = sdb_cmdInfoRegs();
            std::istringstream lines(out);
            std::string line;
            while (std::getline(lines, line)) {
                if (!line.empty())
                    m_output.push_back(line);
            }
        } else if (sub == "w") {
            std::string out = sdb_cmdInfoWatchpoints();
            std::istringstream lines(out);
            std::string line;
            while (std::getline(lines, line)) {
                if (!line.empty())
                    m_output.push_back(line);
            }
        } else {
            m_output.push_back("Usage: info r | info w");
        }
    } else if (token == "x") {
        std::string nStr, exprStr;
        std::istringstream xiss(args ? args : "");
        xiss >> nStr;
        std::getline(xiss, exprStr);
        size_t xp = exprStr.find_first_not_of(" \t");
        const char *xexpr = (xp != std::string::npos) ? exprStr.c_str() + xp : "";
        int n = nStr.empty() ? 1 : std::atoi(nStr.c_str());
        if (n <= 0)
            n = 1;
        std::string out = sdb_cmdX(n, xexpr);
        std::istringstream lines(out);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty())
                m_output.push_back(line);
        }
    } else if (token == "p") {
        std::string out = sdb_cmdP(args);
        m_output.push_back(out);
    } else if (token == "w") {
        std::string out = sdb_cmdW(args);
        m_output.push_back(out);
    } else if (token == "d") {
        int no = (args && *args) ? std::atoi(args) : -1;
        std::string out = sdb_cmdD(no);
        m_output.push_back(out);
    } else {
        m_output.push_back(std::string("Unknown command: ") + token);
        m_output.push_back("Type 'help' for available commands.");
    }
}

void Overlay::render(Canvas &canvas, uint16_t rows, uint16_t cols) {
    if (!m_active)
        return;

    static constexpr uint16_t kOverlayRows = 12;
    uint16_t oh = (rows > kOverlayRows) ? kOverlayRows : rows;
    uint16_t orow = rows - oh;

    Style bgStyle {
        ColourIndex::kColourBlack, ColourIndex::kColourBlack,
        false, false, false
    };
    Style cmdStyle {
        ColourIndex::kColourWhite, ColourIndex::kColourBlack,
        true, false, false
    };
    Style promptStyle {
        ColourIndex::kColourGreen, ColourIndex::kColourBlack,
        true, false, false
    };
    Style outputStyle {
        ColourIndex::kColourWhite, ColourIndex::kColourBlack,
        false, false, false
    };
    Style hintStyle {
        ColourIndex::kColourCyan, ColourIndex::kColourBlack,
        true, false, false
    };
    Style borderFg {
        ColourIndex::kColourCyan, ColourIndex::kColourBlack,
        false, false, false
    };

    canvas.fill(orow, 0, oh, cols, ' ', bgStyle);

    for (uint16_t c = 0; c < cols; ++c) {
        canvas.put(orow, c, '\xC4', borderFg);
    }

    uint16_t outRows = oh - 3;
    if (outRows > 0) {
        size_t totalOut = m_output.size();
        size_t start = (totalOut > outRows) ? totalOut - outRows : 0;
        for (uint16_t i = 0; i < outRows && (start + i) < totalOut; ++i) {
            canvas.writeStr(orow + 1 + i, 1, m_output[start + i], outputStyle);
        }
    }

    canvas.put(orow + oh - 2, 1, '>', promptStyle);
    canvas.put(orow + oh - 2, 2, ' ', promptStyle);

    std::string visible = m_input;
    if (visible.empty()) {
        canvas.writeStr(orow + oh - 2, 3, "(type command...)", hintStyle);
    } else {
        canvas.writeStr(orow + oh - 2, 3, visible, cmdStyle);
        uint16_t cursorVisual = 3 + static_cast<uint16_t>(m_cursorPos);
        if (cursorVisual < cols && m_cursorPos < visible.size()) {
            Style cursorStyle {
                ColourIndex::kColourBlack, ColourIndex::kColourWhite,
                false, false, false
            };
            canvas.put(
                orow + oh - 2, cursorVisual, visible[m_cursorPos], cursorStyle
            );
        }
    }

    static const char *footer = " ESC:close  Enter:execute  Up/Down:history";
    uint16_t footerLen = static_cast<uint16_t>(std::strlen(footer));
    uint16_t footerCol = (cols > footerLen) ? (cols - footerLen) : 0;
    canvas.writeStr(orow + oh - 1, footerCol, footer, hintStyle);
}

} // namespace tui
