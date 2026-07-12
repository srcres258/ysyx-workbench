#ifndef __TUI_PANEL_HPP__
#define __TUI_PANEL_HPP__ 1

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <tui/renderer.hpp>
#include <tui/layout.hpp>
#include <tui/npc_snapshot.hpp>

namespace tui {

// ============================================================================
// Panel — abstract base class for every TUI panel
// ============================================================================

class Panel {
public:
    virtual ~Panel() = default;

    /** Unique string identifier used for layout references and registry lookup. */
    virtual const char *id() const = 0;

    /** Human‑readable name shown in tab bars and panel picker. */
    virtual const char *displayName() const = 0;

    /**
     * @brief Render panel content into a canvas region.
     *
     * @param canvas  Target canvas (already positioned — draw at rect‑relative coords).
     * @param rect    Pre‑computed absolute region for this panel.
     * @param fm      Current frame model (all panel data sources).
     * @param focused True when this panel has keyboard focus.
     */
    virtual void render(Canvas &canvas, const Rect &rect,
                        const TuiFrameModel &fm, bool focused) = 0;

protected:
    static void writeClipped(Canvas &canvas, uint16_t row, uint16_t col,
                             uint16_t clipStartCol, uint16_t clipWidth,
                             const char *text, Style style) {
        if (!text || clipWidth == 0 || col < clipStartCol) return;

        size_t offset = static_cast<size_t>(col - clipStartCol);
        size_t maxLen = static_cast<size_t>(clipWidth);
        if (offset >= maxLen) return;

        size_t len = std::min(std::strlen(text), maxLen - offset);
        if (len == 0) return;

        canvas.writeStr(row, col, std::string(text, len), style);
    }

    template <typename... Args>
    static void writeClippedF(Canvas &canvas, uint16_t row, uint16_t col,
                              uint16_t clipStartCol, uint16_t clipWidth,
                              Style style, const char *fmt, Args... args) {
        if (clipWidth == 0 || col < clipStartCol) return;

        size_t offset = static_cast<size_t>(col - clipStartCol);
        size_t maxLen = static_cast<size_t>(clipWidth);
        if (offset >= maxLen) return;

        char buf[512];
        std::snprintf(buf, sizeof(buf), fmt, args...);

        size_t len = std::min(std::strlen(buf), maxLen - offset);
        if (len == 0) return;

        canvas.writeStr(row, col, std::string(buf, len), style);
    }
};

// ============================================================================
// PanelRegistry — singleton that owns all Panel instances keyed by string ID
// ============================================================================

class PanelRegistry {
public:
    static PanelRegistry &instance();

    /** Register a panel (takes ownership).  Returns false if id already registered. */
    bool registerPanel(std::unique_ptr<Panel> panel);

    /** Look up a panel by id; nullptr if not found. */
    Panel       *get(const std::string &id);
    const Panel *get(const std::string &id) const;

    /** True if a panel with the given id has been registered. */
    bool has(const std::string &id) const;

    /** All registered panel IDs in registration order. */
    std::vector<std::string> allIds() const;

    /** Display name for a panel id, or id itself if not found. */
    std::string displayNameFor(const std::string &id) const;

    /** Create and register all built‑in placeholder panels. */
    void registerBuiltins();

    /**
     * @brief Validate that every leaf in the layout tree references a registered panel.
     *
     * Prints a diagnostic for each unknown panel ID and returns false if any are missing.
     */
    bool validateLayout(const LayoutTree &tree) const;

private:
    PanelRegistry() = default;
    std::map<std::string, std::unique_ptr<Panel>> m_panels;
};

} // namespace tui

#endif /* __TUI_PANEL_HPP__ */
