#ifndef __TUI_RENDERER_HPP__
#define __TUI_RENDERER_HPP__ 1

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <tui/terminal.hpp>

namespace tui {

// ============================================================================
// Style
// ============================================================================

/**
 * @brief Compact per-cell style descriptor.
 *
 * Stores foreground/background colour indices and boolean attributes.
 * These are encoded into full ANSI SGR sequences only at flush time.
 *
 * Colour indices correspond to the existing ANSI_FG_* / ANSI_BG_* macros
 * in utils.hpp (0‑7 for standard 3‑/4‑bit colours).
 *
 * RENDERER_NONE (0xFF) means "inherit / transparent".
 */
enum ColourIndex : uint8_t {
    kColourBlack   = 0,
    kColourRed     = 1,
    kColourGreen   = 2,
    kColourYellow  = 3,
    kColourBlue    = 4,
    kColourMagenta = 5,
    kColourCyan    = 6,
    kColourWhite   = 7,
    kColourNone    = 0xFF
};

struct Style {
    ColourIndex fg       : 8 = kColourNone;
    ColourIndex bg       : 8 = kColourNone;
    bool        bold     : 1 = false;
    bool        reverse  : 1 = false;
    bool        underline: 1 = false;

    constexpr bool operator==(const Style &o) const {
        return fg == o.fg && bg == o.bg && bold == o.bold
            && reverse == o.reverse && underline == o.underline;
    }
    constexpr bool operator!=(const Style &o) const { return !(*this == o); }

    /** Return true if this style has any non-default attribute set. */
    constexpr bool hasAttributes() const {
        return fg != kColourNone || bg != kColourNone
            || bold || reverse || underline;
    }
};

// ============================================================================
// Cell — one character cell on the terminal grid
// ============================================================================

struct Cell {
    char  ch[4];   ///< UTF‑8 codepoint (up to 3 bytes + NUL); empty → ' '
    Style style;

    Cell() noexcept { ch[0] = ' '; ch[1] = '\0'; }
};

// ============================================================================
// Canvas — a rectangular character-cell buffer
// ============================================================================

/**
 * @brief A 2-D grid of styled cells that panels can draw into.
 *
 * Panels receive a sub‑region canvas whose coordinate origin (0,0)
 * is local to the panel.  The renderer composites all sub‑canvases
 * into one full‑screen canvas before flushing to the terminal.
 */
class Canvas {
public:
    friend class ViewportGuard;

    Canvas(uint16_t rows, uint16_t cols);
    Canvas(const Canvas &) = delete;
    Canvas &operator=(const Canvas &) = delete;

    class ViewportGuard {
    public:
        ViewportGuard() = default;
        ViewportGuard(
            Canvas &canvas,
            uint16_t row, uint16_t col, uint16_t h, uint16_t w,
            size_t scrollRow = 0, size_t scrollCol = 0
        );
        ~ViewportGuard();

        ViewportGuard(const ViewportGuard &) = delete;
        ViewportGuard &operator=(const ViewportGuard &) = delete;

        ViewportGuard(ViewportGuard &&other) noexcept;
        ViewportGuard &operator=(ViewportGuard &&other) noexcept;

    private:
        Canvas *m_canvas = nullptr;
    };

    uint16_t rows() const { return m_rows; }
    uint16_t cols() const { return m_cols; }

    // ---- Mutators ----

    /** Set a single cell.  Clipped to canvas bounds. */
    void put(uint16_t row, uint16_t col, char ch, Style style = {});

    /** Write a null‑terminated C string starting at (row, col). */
    void write(uint16_t row, uint16_t col, const char *text, Style style = {});

    /** Write a std::string. */
    void writeStr(
        uint16_t row, uint16_t col, const std::string &text, Style style = {}
    );

    /** Fill a rectangular region with a single character and style. */
    void fill(uint16_t row, uint16_t col, uint16_t h, uint16_t w,
              char ch = ' ', Style style = {});

    /** Clear entire canvas (fill with space + default style). */
    void clear(Style style = {});

    // ---- Style helpers ----

    /** Set the style of every cell in a region without changing the glyph. */
    void applyStyle(
        uint16_t row, uint16_t col, uint16_t h, uint16_t w, Style style
    );

    /** Write a formatted (printf‑style) string. */
    void writeF(uint16_t row, uint16_t col, Style style, const char *fmt, ...);

    /** Push a panel-local viewport.  Coordinates become viewport-local. */
    ViewportGuard pushViewport(
        uint16_t row, uint16_t col, uint16_t h, uint16_t w,
        size_t scrollRow = 0, size_t scrollCol = 0
    );

    struct Viewport {
        uint16_t row;
        uint16_t col;
        uint16_t h;
        uint16_t w;
        size_t scrollRow;
        size_t scrollCol;
    };

    void pushViewportEntry(const Viewport &vp);
    void popViewport();
    bool hasViewport() const;
    const Viewport &currentViewport() const;

    // ---- Compositing ----

    /**
     * @brief Copy a rectangular region from @p src into this canvas.
     *
     * Non‑space cells in @p src overwrite the destination cell.
     * Space cells in @p src are transparent (destination unchanged).
     */
    void blit(
        uint16_t dstRow, uint16_t dstCol,
        const Canvas &src,
        uint16_t srcRow, uint16_t srcCol,
        uint16_t h, uint16_t w
    );

    /** Blit entire @p src canvas anchored at (dstRow, dstCol). */
    void blitAll(uint16_t dstRow, uint16_t dstCol, const Canvas &src);

    // ---- Access ----

    const Cell &cell(uint16_t row, uint16_t col) const {
        return m_cells[row * m_cols + col];
    }

    Cell &cell(uint16_t row, uint16_t col) {
        return m_cells[row * m_cols + col];
    }

    /** True if every cell in the region is ' ' (space). */
    bool regionIsEmpty(
        uint16_t row, uint16_t col, uint16_t h, uint16_t w
    ) const;

private:
    uint16_t          m_rows;
    uint16_t          m_cols;
    std::vector<Cell> m_cells;
    std::vector<Viewport> m_viewports;
};

// ============================================================================
// Renderer — owns the full‑screen canvas and drives terminal output
// ============================================================================

/**
 * @brief Frame‑oriented terminal renderer.
 *
 * Usage (per frame):
 *   renderer.beginFrame();
 *   renderer.drawBox(...);
 *   renderer.drawStatusBar(...);
 *   // or pass canvas to panels:
 *   myPanel.render(renderer.canvas());
 *   renderer.endFrame();
 *   renderer.flush();
 *
 * The renderer owns a `last_frame` token that future diff‑render
 * tasks can use.  v1 emits the full frame every time.
 */
class Renderer {
public:
    Renderer();
    ~Renderer();

    // Non‑copyable, non‑movable
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    Renderer(Renderer &&) = delete;
    Renderer &operator=(Renderer &&) = delete;

    // ---- Lifecycle ----

    /** Resize internal buffers to match terminal dimensions. */
    void resize(uint16_t rows, uint16_t cols);

    /** Access the full‑screen canvas for direct drawing or panel delegation. */
    Canvas &canvas() { return *m_canvas; }
    const Canvas &canvas() const { return *m_canvas; }

    // ---- Frame lifecycle ----

    /** Clear canvas for a new frame. */
    void beginFrame();

    /** Call after all panel drawing is complete. */
    void endFrame();

    /**
     * @brief Flush the current frame to the terminal.
     *
     * Produces ANSI‑encoded output from the internal canvas.
     * Every cell with content is written; empty regions produce
     * no overhead beyond cursor positioning.
     */
    void flush();

    // ---- Drawing primitives (convenience wrappers that delegate to free functions) ----

    /** Draw a box with optional border and title. */
    void drawBox(
        uint16_t row, uint16_t col, uint16_t h, uint16_t w,
        const char *title = nullptr,
        Style titleStyle = {},
        Style borderStyle = {}
    );

    /** Draw a horizontal status bar at the given row. */
    void drawStatusBar(
        uint16_t row, const char *left, const char *right,
        Style barStyle = {}, Style textStyle = {}
    );

    /** Draw a key‑hint strip (e.g. "q:quit  space:pause  tab:focus"). */
    void drawKeyHints(
        uint16_t row, uint16_t col,
        const char *hints, Style style = {}
    );

    // ---- Last‑frame hook for future diff rendering ----

    const std::string &lastFrame() const {
        return m_lastFrame;
    }

    // ---- Terminal‑too‑small fallback ----

    static constexpr uint16_t kMinCols = 30;
    static constexpr uint16_t kMinRows = 6;

    /** True if the current terminal is too small to render properly. */
    bool terminalTooSmall() const {
        return m_tooSmall;
    }

private:
    /**
     * @brief Encode the canvas as a single ANSI string.
     *
     * Uses cursor‑absolute positioning, SGR style transitions,
     * and efficient row‑based output.
     */
    std::string encodeFrame() const;

    Canvas     *m_canvas = nullptr;
    std::string m_lastFrame;
    bool        m_tooSmall = false;
    bool        m_inFrame  = false;
    uint16_t    m_termRows = 0;
    uint16_t    m_termCols = 0;
};

// ============================================================================
// Free drawing‑primitive functions
// ============================================================================

/** Draw a single‑line border box (corner and edge glyphs). */
void primDrawBox(
    Canvas &canvas,
    uint16_t row, uint16_t col, uint16_t h, uint16_t w,
    const char *title = nullptr,
    Style titleStyle = {},
    Style borderStyle = {}
);

/** Draw a horizontal status bar spanning the full canvas width. */
void primDrawStatusBar(
    Canvas &canvas, uint16_t row,
    const char *left, const char *right,
    Style barStyle = {}, Style textStyle = {}
);

/**
 * @brief Draw a simple table.
 *
 * @p headers  Null‑terminated array of header strings.
 * @p rows     Flattened row×col strings (row‑major).
 * @p headerStyle  Style for the header row.
 * @p cellStyle    Style for body cells.
 */
void primDrawTable(
    Canvas &canvas,
    uint16_t row, uint16_t col,
    const char *const headers[], size_t nCols,
    const char *const rows[], size_t nDataRows,
    Style headerStyle = {}, Style cellStyle = {},
    Style borderStyle = {}
);

/** Draw a vertical list of items (one per line). */
void primDrawList(
    Canvas &canvas,
    uint16_t row, uint16_t col,
    const char *const items[], size_t nItems,
    Style itemStyle = {}
);

/**
 * @brief Draw a small inline badge (e.g. "[RUNNING]").
 *
 * The badge width is computed from the text length + 2 brackets.
 */
void primDrawBadge(
    Canvas &canvas,
    uint16_t row, uint16_t col,
    const char *text, Style badgeStyle = {}
);

/** Draw a key‑hint strip — usually near the bottom of the screen. */
void primDrawKeyHints(
    Canvas &canvas,
    uint16_t row, uint16_t col,
    const char *hints, Style style = {}
);

// ============================================================================
// Style constructors (inline convenience)
// ============================================================================

inline constexpr Style styleFg(ColourIndex c) {
    return {c, kColourNone, false, false, false};
}

inline constexpr Style styleBg(ColourIndex c) {
    return {kColourNone, c, false, false, false};
}

inline constexpr Style styleBold() {
    return {kColourNone, kColourNone, true, false, false};
}

inline constexpr Style styleReverse() {
    return {kColourNone, kColourNone, false, true, false};
}

inline constexpr Style styleFgBold(ColourIndex c) {
    // Use existing ANSI constants' bold‑by‑default convention
    return {c, kColourNone, true, false, false};
}

} // namespace tui

#endif /* __TUI_RENDERER_HPP__ */
