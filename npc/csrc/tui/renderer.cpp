#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <tui/renderer.hpp>

namespace tui {

static constexpr uint16_t minU16(uint16_t a, uint16_t b) {
    return a < b ? a : b;
}

static int ansiFgCode(ColourIndex c) {
    switch (c) {
        case kColourBlack:   return 30;
        case kColourRed:     return 31;
        case kColourGreen:   return 32;
        case kColourYellow:  return 33;
        case kColourBlue:    return 34;
        case kColourMagenta: return 35;
        case kColourCyan:    return 36;
        case kColourWhite:   return 37;
        default:             return 0;
    }
}

static int ansiBgCode(ColourIndex c) {
    switch (c) {
        case kColourBlack:   return 40;
        case kColourRed:     return 41;
        case kColourGreen:   return 42;
        case kColourYellow:  return 43;
        case kColourBlue:    return 44;
        case kColourMagenta: return 45;
        case kColourCyan:    return 46;
        case kColourWhite:   return 47;
        default:             return 0;
    }
}

static bool appendSGR(std::string &buf, Style style) {
    if (!style.hasAttributes()) {
        buf += "\033[0m";
        return true;
    }

    std::string seq = "\033[";
    bool first = true;

    if (style.bold) {
        seq += "1";
        first = false;
    }
    if (style.underline) {
        if (!first)
            seq += ';';
        seq += "4";
        first = false;
    }
    if (style.reverse) {
        if (!first)
            seq += ';';
        seq += "7";
        first = false;
    }
    if (style.fg != kColourNone) {
        if (!first)
            seq += ';';
        seq += std::to_string(ansiFgCode(style.fg));
        first = false;
    }
    if (style.bg != kColourNone) {
        if (!first)
            seq += ';';
        seq += std::to_string(ansiBgCode(style.bg));
        first = false;
    }

    seq += 'm';
    buf += seq;
    return true;
}

// ============================================================================
// Canvas
// ============================================================================

Canvas::Canvas(uint16_t rows, uint16_t cols)
    : m_rows(rows), m_cols(cols), m_cells(rows * cols) {}

void Canvas::put(uint16_t row, uint16_t col, char ch, Style style) {
    if (row >= m_rows || col >= m_cols)
        return;
    Cell &cell = m_cells[row * m_cols + col];
    cell.ch[0] = ch;
    cell.ch[1] = '\0';
    cell.style = style;
}

void Canvas::write(uint16_t row, uint16_t col, const char *text, Style style) {
    if (!text || row >= m_rows)
        return;
    const char *p = text;
    size_t off = row * m_cols;
    while (*p && col < m_cols) {
        Cell &cell = m_cells[off + col];
        unsigned char ch = static_cast<unsigned char>(*p);
        if (ch < 0x20 || ch == 0x7f) {
            ch = ' ';
        }
        cell.ch[0] = static_cast<char>(ch);
        cell.ch[1] = '\0';
        cell.style = style;
        p++;
        col++;
    }
}

void Canvas::writeStr(
    uint16_t row, uint16_t col, const std::string &text, Style style
) {
    write(row, col, text.c_str(), style);
}

void Canvas::fill(
    uint16_t row, uint16_t col, uint16_t h, uint16_t w,
    char ch, Style style
) {
    if (row >= m_rows || col >= m_cols)
        return;
    uint16_t endRow = minU16(row + h, m_rows);
    uint16_t endCol = minU16(col + w, m_cols);
    for (uint16_t r = row; r < endRow; r++) {
        for (uint16_t c = col; c < endCol; c++) {
            Cell &cell = m_cells[r * m_cols + c];
            cell.ch[0] = ch;
            cell.ch[1] = '\0';
            cell.style = style;
        }
    }
}

void Canvas::clear(Style style) {
    fill(0, 0, m_rows, m_cols, ' ', style);
}

void Canvas::applyStyle(
    uint16_t row, uint16_t col, uint16_t h, uint16_t w, Style style
) {
    if (row >= m_rows || col >= m_cols)
        return;
    uint16_t endRow = minU16(row + h, m_rows);
    uint16_t endCol = minU16(col + w, m_cols);
    for (uint16_t r = row; r < endRow; r++) {
        for (uint16_t c = col; c < endCol; c++) {
            m_cells[r * m_cols + c].style = style;
        }
    }
}

void Canvas::writeF(
    uint16_t row, uint16_t col, Style style, const char *fmt, ...
) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    write(row, col, buf, style);
}

void Canvas::blit(
    uint16_t dstRow, uint16_t dstCol,
    const Canvas &src,
    uint16_t srcRow, uint16_t srcCol,
    uint16_t h, uint16_t w
) {
    if (dstRow >= m_rows || dstCol >= m_cols)
        return;
    uint16_t copyRows = minU16(h, minU16(m_rows - dstRow, src.m_rows - srcRow));
    uint16_t copyCols = minU16(w, minU16(m_cols - dstCol, src.m_cols - srcCol));

    for (uint16_t r = 0; r < copyRows; r++) {
        size_t dstOff = (dstRow + r) * m_cols + dstCol;
        size_t srcOff = (srcRow + r) * src.m_cols + srcCol;
        for (uint16_t c = 0; c < copyCols; c++) {
            const Cell &sc = src.m_cells[srcOff + c];
            if (sc.ch[0] == ' ' && sc.ch[1] == '\0')
                continue;
            m_cells[dstOff + c] = sc;
        }
    }
}

void Canvas::blitAll(uint16_t dstRow, uint16_t dstCol, const Canvas &src) {
    blit(dstRow, dstCol, src, 0, 0, src.m_rows, src.m_cols);
}

bool Canvas::regionIsEmpty(uint16_t row, uint16_t col, uint16_t h, uint16_t w) const {
    if (row >= m_rows || col >= m_cols)
        return true;
    uint16_t endRow = minU16(row + h, m_rows);
    uint16_t endCol = minU16(col + w, m_cols);
    for (uint16_t r = row; r < endRow; r++) {
        size_t off = r * m_cols;
        for (uint16_t c = col; c < endCol; c++) {
            const Cell &cell = m_cells[off + c];
            if (cell.ch[0] != ' ' || cell.ch[1] != '\0')
                return false;
        }
    }
    return true;
}

// ============================================================================
// Renderer
// ============================================================================

Renderer::Renderer() = default;

Renderer::~Renderer() {
    delete m_canvas;
}

void Renderer::resize(uint16_t rows, uint16_t cols) {
    m_termRows = rows;
    m_termCols = cols;
    delete m_canvas;
    m_canvas = new Canvas(rows, cols);
    m_tooSmall = (rows < kMinRows || cols < kMinCols);
}

void Renderer::beginFrame() {
    if (!m_canvas)
        return;
    m_inFrame = true;
    m_canvas->clear();
}

void Renderer::endFrame() {
    m_inFrame = false;
}

void Renderer::flush() {
    if (!m_canvas)
        return;

    if (m_tooSmall) {
        std::fputs("\033[H\033[2J", stdout);
        std::fprintf(stdout,
            "\033[1;31mTerminal too small (%ux%u). Need at least %ux%u."
                "\033[0m\r\n",
            m_termCols, m_termRows, kMinCols, kMinRows);
        std::fflush(stdout);
        m_lastFrame.clear();
        return;
    }

    m_lastFrame = encodeFrame();
    std::fputs(m_lastFrame.c_str(), stdout);
    std::fflush(stdout);
}

std::string Renderer::encodeFrame() const {
    std::string out;
    out.reserve(static_cast<size_t>(m_termRows) * m_termCols * 8);
    out += "\033[H\033[2J";

    Style currentStyle{};

    for (uint16_t r = 0; r < m_termRows; r++) {
        size_t rowOff = r * static_cast<size_t>(m_canvas->cols());

        int16_t lastNonSpace = -1;
        for (int16_t c = static_cast<int16_t>(m_termCols) - 1; c >= 0; c--) {
            const Cell &cell = m_canvas->cell(r, static_cast<uint16_t>(c));
            if (cell.ch[0] != ' ' || cell.ch[1] != '\0') {
                lastNonSpace = c;
                break;
            }
        }

        if (lastNonSpace < 0) {
            out += "\033[K\r\n";
            currentStyle = {};
            continue;
        }

        out += '\r';

        for (uint16_t c = 0; c < m_termCols; c++) {
            const Cell &cell = m_canvas->cell(r, c);

            if (cell.style != currentStyle) {
                currentStyle = cell.style;
                if (!currentStyle.hasAttributes()) {
                    out += "\033[0m";
                } else {
                    appendSGR(out, currentStyle);
                }
            }

            out += cell.ch;

            if (c == static_cast<uint16_t>(lastNonSpace)) {
                if (currentStyle.hasAttributes()) {
                    out += "\033[0m";
                    currentStyle = {};
                }
                out += "\033[K";
                break;
            }
        }

        if (r + 1 < m_termRows) {
            out += "\r\n";
        }
    }

    return out;
}

// ============================================================================
// Renderer convenience wrappers
// ============================================================================

void Renderer::drawBox(
    uint16_t row, uint16_t col, uint16_t h, uint16_t w,
    const char *title, Style titleStyle, Style borderStyle
) {
    primDrawBox(*m_canvas, row, col, h, w, title, titleStyle, borderStyle);
}

void Renderer::drawStatusBar(
    uint16_t row, const char *left, const char *right,
    Style barStyle, Style textStyle
) {
    primDrawStatusBar(*m_canvas, row, left, right, barStyle, textStyle);
}

void Renderer::drawKeyHints(
    uint16_t row, uint16_t col,
    const char *hints, Style style
) {
    primDrawKeyHints(*m_canvas, row, col, hints, style);
}

// ============================================================================
// Free drawing primitives
// ============================================================================

void primDrawBox(
    Canvas &canvas,
    uint16_t row, uint16_t col, uint16_t h, uint16_t w,
    const char *title, Style titleStyle, Style borderStyle
) {
    if (h < 2 || w < 2)
        return;

    canvas.put(row, col, '+', borderStyle);
    canvas.fill(row, col + 1, 1, w - 2, '-', borderStyle);
    canvas.put(row, col + w - 1, '+', borderStyle);

    if (title && title[0]) {
        size_t titleLen = std::strlen(title);
        if (titleLen < static_cast<size_t>(w - 2)) {
            uint16_t titleCol = col + (w - static_cast<uint16_t>(titleLen)) / 2;
            canvas.write(row, titleCol, title, titleStyle);
        }
    }

    for (uint16_t r = row + 1; r < row + h - 1; r++) {
        canvas.put(r, col, '|', borderStyle);
        canvas.put(r, col + w - 1, '|', borderStyle);
    }

    canvas.put(row + h - 1, col, '+', borderStyle);
    canvas.fill(row + h - 1, col + 1, 1, w - 2, '-', borderStyle);
    canvas.put(row + h - 1, col + w - 1, '+', borderStyle);
}

void primDrawStatusBar(
    Canvas &canvas, uint16_t row,
    const char *left, const char *right,
    Style barStyle, Style textStyle
) {
    uint16_t cols = canvas.cols();
    if (row >= canvas.rows() || cols == 0)
        return;

    canvas.fill(row, 0, 1, cols, ' ', barStyle);

    if (left && left[0]) {
        canvas.write(row, 1, left, textStyle);
    }

    if (right && right[0]) {
        size_t rlen = std::strlen(right);
        if (rlen + 2 <= cols) {
            canvas.write(
                row, cols - static_cast<uint16_t>(rlen) - 1, right, textStyle
            );
        }
    }
}

void primDrawTable(
    Canvas &canvas,
    uint16_t row, uint16_t col,
    const char *const headers[], size_t nCols,
    const char *const rows[], size_t nDataRows,
    Style headerStyle, Style cellStyle,
    Style borderStyle
) {
    if (nCols == 0) return;

    uint16_t r = row;

    if (headers && headers[0]) {
        for (size_t c = 0; c < nCols; c++) {
            const char *hdr = headers[c] ? headers[c] : "";
            size_t hdrLen = std::strlen(hdr);
            uint16_t cellW = (canvas.cols() - col) /
                static_cast<uint16_t>(nCols);
            uint16_t x = col + static_cast<uint16_t>(c) * cellW;
            canvas.write(r, x, hdr, headerStyle);
            if (c < nCols - 1) {
                canvas.put(r, x + cellW - 1, '|', borderStyle);
            }
        }
        r++;
    }

    for (size_t i = 0; i < nDataRows; i++) {
        if (r >= canvas.rows())
            break;
        for (size_t c = 0; c < nCols; c++) {
            const char *cellText = rows[i * nCols + c];
            if (!cellText)
                cellText = "";
            size_t textLen = std::strlen(cellText);
            uint16_t cellW = (canvas.cols() - col) /
                static_cast<uint16_t>(nCols);
            uint16_t x = col + static_cast<uint16_t>(c) * cellW;
            size_t toWrite = textLen;
            if (toWrite >= cellW)
                toWrite = cellW - 1;
            char tmp[64];
            std::memcpy(tmp, cellText, toWrite);
            tmp[toWrite] = '\0';
            canvas.write(r, x, tmp, cellStyle);
        }
        r++;
    }
}

void primDrawList(
    Canvas &canvas,
    uint16_t row, uint16_t col,
    const char *const items[], size_t nItems,
    Style itemStyle
) {
    for (size_t i = 0; i < nItems; i++) {
        if (row + i >= canvas.rows())
            break;
        if (items[i])
            canvas.write(
                row + static_cast<uint16_t>(i), col, items[i], itemStyle
            );
    }
}

void primDrawBadge(
    Canvas &canvas,
    uint16_t row, uint16_t col,
    const char *text, Style badgeStyle
) {
    if (!text || !text[0])
        return;

    size_t tlen = std::strlen(text);
    canvas.put(row, col, '[', badgeStyle);
    canvas.write(row, col + 1, text, badgeStyle);
    canvas.put(row, col + static_cast<uint16_t>(tlen) + 1, ']', badgeStyle);
}

void primDrawKeyHints(
    Canvas &canvas,
    uint16_t row, uint16_t col,
    const char *hints, Style style
) {
    if (!hints)
        return;

    canvas.write(row, col, hints, style);
}

} // namespace tui
