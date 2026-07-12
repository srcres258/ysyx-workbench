#ifndef __TUI_TERMINAL_HPP__
#define __TUI_TERMINAL_HPP__ 1

#include <cstdint>
#include <termios.h>
#include <signal.h>

namespace tui {

/**
 * @brief Terminal dimensions in characters.
 */
struct TerminalSize {
    uint16_t rows;
    uint16_t cols;
};

/**
 * @brief RAII terminal session for ANSI/VT-only TUI.
 *
 * On construction (when valid): enters alternate screen, enables raw mode,
 * hides cursor, installs SIGWINCH handler, and – if supported – enables
 * synchronized output mode.
 *
 * On destruction: restores cursor visibility, raw mode, original signal
 * handlers, and exits alternate screen.  The destructor is safe to call
 * multiple times.
 *
 * If the process is not connected to a usable TTY, construction fails fast
 * (operator bool() returns false) after printing a clear diagnostic to
 * stderr.  The caller must check the session validity before use.
 *
 * Signal safety: SIGINT / SIGTERM / SIGQUIT trigger an emergency restore
 * before re-raising, so the shell is never left in raw mode even on
 * Ctrl‑C.  SIGWINCH is caught and sets an internal flag that the
 * application can poll via consumeResizeFlag().
 */
class TerminalSession {
public:
    TerminalSession();
    ~TerminalSession();

    // Non-copyable, non-movable
    TerminalSession(const TerminalSession &) = delete;
    TerminalSession &operator=(const TerminalSession &) = delete;
    TerminalSession(TerminalSession &&) = delete;
    TerminalSession &operator=(TerminalSession &&) = delete;

    /** True if the session was successfully initialised. */
    explicit operator bool() const { return m_valid; }

    /** The tty file descriptor (for polling or raw reads). */
    int fd() const { return m_fd; }

    /**
     * @brief Query current terminal dimensions.
     *
     * Uses ioctl(TIOCGWINSZ) internally.  Always returns the current
     * kernel-reported size – call after consumeResizeFlag() to pick up
     * the latest after a SIGWINCH.
     */
    TerminalSize querySize() const;

    /**
     * @brief Check and clear the SIGWINCH flag.
     *
     * Returns true if the terminal was resized since the last call.
     * Thread‑safe: the flag is volatile and only set by the signal
     * handler.
     */
    bool consumeResizeFlag();

    /** Emit the CSI that starts synchronized output mode. */
    void beginSync();

    /** Emit the CSI that ends synchronized output mode. */
    void endSync();

private:
    void enterAltScreen();
    void leaveAltScreen();
    bool enterRawMode();
    void leaveRawMode();
    void hideCursor();
    void showCursor();
    void installHandlers();
    void restoreHandlers();
    void emergencyRestore();

    // ---- Signal handlers (static – must be plain C-linkage compatible) ----
    static void sigwinchHandler(int);
    static void fatalHandler(int);

    // ---- Per-instance state ----
    int  m_fd;
    bool m_valid          : 1;
    bool m_altScreen      : 1;
    bool m_rawMode        : 1;
    bool m_cursorHidden   : 1;

    struct termios   m_savedTermios;
    struct sigaction m_oldSigwinch;
    struct sigaction m_oldSigint;
    struct sigaction m_oldSigterm;
    struct sigaction m_oldSigquit;

    // ---- Static state shared with signal handlers ----
    static TerminalSession *s_active;
    static volatile bool    s_resized;
};

} // namespace tui

#endif /* __TUI_TERMINAL_HPP__ */
