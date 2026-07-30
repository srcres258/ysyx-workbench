#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/ioctl.h>
#include <tui/terminal.hpp>

namespace tui {

// ---------------------------------------------------------------------------
// ANSI / VT escape sequences
// ---------------------------------------------------------------------------
static const char kAltScreenEnter[] = "\033[?1049h";
static const char kAltScreenLeave[] = "\033[?1049l";
static const char kCursorHide[]     = "\033[?25l";
static const char kCursorShow[]     = "\033[?25h";
static const char kSyncStart[]      = "\033[?2026h";
static const char kSyncEnd[]        = "\033[?2026l";

// ---------------------------------------------------------------------------
// Static state
// ---------------------------------------------------------------------------
TerminalSession *TerminalSession::s_active  = nullptr;
volatile bool    TerminalSession::s_resized = false;

// ---------------------------------------------------------------------------
// Signal handlers
// ---------------------------------------------------------------------------
void TerminalSession::sigwinchHandler(int /*signo*/) {
    s_resized = true;
}

void TerminalSession::fatalHandler(int signo) {
    if (s_active) {
        s_active->emergencyRestore();
    }
    // Re-raise with the default disposition so the shell sees the signal.
    signal(signo, SIG_DFL);
    raise(signo);
}

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------
TerminalSession::TerminalSession()
    : m_fd(STDOUT_FILENO)
    , m_valid(false)
    , m_altScreen(false)
    , m_rawMode(false)
    , m_cursorHidden(false)
{
    // --- TTY checks ---
    if (!isatty(m_fd)) {
        std::fprintf(
            stderr,
            "[tui] fatal: stdout is not a TTY. "
            "The TUI requires an interactive terminal.\n"
        );
        return;
    }
    if (!isatty(STDIN_FILENO)) {
        std::fprintf(
            stderr,
            "[tui] fatal: stdin is not a TTY. "
            "The TUI requires an interactive terminal.\n"
        );
        return;
    }

    // --- Install signal handlers BEFORE touching terminal state ---
    // This ensures that if a signal arrives mid-setup we can still restore.
    installHandlers();

    // --- Enter raw mode BEFORE any visible terminal mutation ---
    // If raw mode fails we return without having touched the screen,
    // so no rollback is needed.  The destructor (m_valid == false)
    // is a harmless no-op.
    if (!enterRawMode()) {
        std::fprintf(
            stderr,
            "[tui] fatal: failed to enter raw mode\n"
        );
        return;
    }

    // --- Enter alternate screen (hides scrollback) ---
    enterAltScreen();

    // --- Hide cursor ---
    hideCursor();

    // --- Synchronized output (best-effort; unsupported terminals ignore it) ---
    beginSync();

    s_active = this;
    m_valid  = true;
}

TerminalSession::~TerminalSession() {
    if (!m_valid)
        return;

    // Drain any pending synchronized output.
    endSync();

    showCursor();
    leaveRawMode();
    leaveAltScreen();

    restoreHandlers();

    s_active = nullptr;
    m_valid  = false;
}

// ---------------------------------------------------------------------------
// Alternate screen
// ---------------------------------------------------------------------------
void TerminalSession::enterAltScreen() {
    std::fputs(kAltScreenEnter, stdout);
    std::fflush(stdout);
    m_altScreen = true;
}

void TerminalSession::leaveAltScreen() {
    if (!m_altScreen)
        return;
    std::fputs(kAltScreenLeave, stdout);
    std::fflush(stdout);
    m_altScreen = false;
}

// ---------------------------------------------------------------------------
// Raw mode
// ---------------------------------------------------------------------------
bool TerminalSession::enterRawMode() {
    if (tcgetattr(STDIN_FILENO, &m_savedTermios) != 0) {
        std::fprintf(
            stderr, "[tui] tcgetattr failed: %s\n",
            std::strerror(errno)
        );
        return false;
    }

    struct termios raw = m_savedTermios;
    cfmakeraw(&raw);

    raw.c_cc[VMIN]  = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
        std::fprintf(
            stderr, "[tui] tcsetattr (raw) failed: %s\n",
            std::strerror(errno)
        );
        return false;
    }
    m_rawMode = true;
    return true;
}

void TerminalSession::leaveRawMode() {
    if (!m_rawMode)
        return;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &m_savedTermios);
    m_rawMode = false;
}

// ---------------------------------------------------------------------------
// Cursor visibility
// ---------------------------------------------------------------------------
void TerminalSession::hideCursor() {
    std::fputs(kCursorHide, stdout);
    std::fflush(stdout);
    m_cursorHidden = true;
}

void TerminalSession::showCursor() {
    if (!m_cursorHidden)
        return;
    std::fputs(kCursorShow, stdout);
    std::fflush(stdout);
    m_cursorHidden = false;
}

// ---------------------------------------------------------------------------
// Synchronized output
// ---------------------------------------------------------------------------
void TerminalSession::beginSync() {
    std::fputs(kSyncStart, stdout);
    std::fflush(stdout);
}

void TerminalSession::endSync() {
    std::fputs(kSyncEnd, stdout);
    std::fflush(stdout);
}

// ---------------------------------------------------------------------------
// Terminal size
// ---------------------------------------------------------------------------
TerminalSize TerminalSession::querySize() const {
    TerminalSize ts = { 0, 0 };
    struct winsize ws;
    if (ioctl(m_fd, TIOCGWINSZ, &ws) == 0) {
        ts.rows = ws.ws_row;
        ts.cols = ws.ws_col;
    }
    return ts;
}

bool TerminalSession::consumeResizeFlag() {
    bool was = s_resized;
    s_resized = false;
    return was;
}

// ---------------------------------------------------------------------------
// Signal handler management
// ---------------------------------------------------------------------------
void TerminalSession::installHandlers() {
    struct sigaction sa;

    // SIGWINCH — just set the flag, don't interrupt syscalls.
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigwinchHandler;
    sa.sa_flags   = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, &m_oldSigwinch);

    // Fatal signals — emergency restore, then re-raise.
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fatalHandler;
    sa.sa_flags   = SA_RESETHAND;  // one-shot: revert to default after first delivery
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, &m_oldSigint);
    sigaction(SIGTERM, &sa, &m_oldSigterm);
    sigaction(SIGQUIT, &sa, &m_oldSigquit);
}

void TerminalSession::restoreHandlers() {
    sigaction(SIGWINCH, &m_oldSigwinch, nullptr);
    sigaction(SIGINT,   &m_oldSigint,   nullptr);
    sigaction(SIGTERM,  &m_oldSigterm,  nullptr);
    sigaction(SIGQUIT,  &m_oldSigquit,  nullptr);
}

void TerminalSession::emergencyRestore() {
    // Called from a signal handler — only async-signal-safe operations.
    // We write directly via write(2) and use tcsetattr which is
    // async-signal-safe per POSIX.

    // Order matters: show cursor, leave raw mode, leave alt screen.
    write(STDOUT_FILENO, kCursorShow, sizeof(kCursorShow) - 1);
    if (m_rawMode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &m_savedTermios);
    }
    write(STDOUT_FILENO, kAltScreenLeave, sizeof(kAltScreenLeave) - 1);

    // Mark ourselves invalid so the destructor is a no-op (it will still
    // run via atexit / static destruction if the process lives long enough,
    // but our flags prevent double-restore).
    m_altScreen    = false;
    m_rawMode      = false;
    m_cursorHidden = false;
    m_valid        = false;
    s_active       = nullptr;
}

} // namespace tui
