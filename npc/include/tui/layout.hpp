#ifndef __TUI_LAYOUT_HPP__
#define __TUI_LAYOUT_HPP__ 1

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace tui {

class Canvas;

// ============================================================================
// Rect — absolute terminal-cell rectangle
// ============================================================================

struct Rect {
    uint16_t row;
    uint16_t col;
    uint16_t h;
    uint16_t w;

    Rect() : row(0), col(0), h(0), w(0) {}
    Rect(uint16_t r, uint16_t c, uint16_t hh, uint16_t ww)
        : row(r), col(c), h(hh), w(ww) {}

    bool valid() const { return h > 0 && w > 0; }
};

// ============================================================================
// LayoutNode — recursive layout tree node
// ============================================================================

enum class SplitDir : uint8_t {
    Horizontal,   ///< children stacked top‑to‑bottom
    Vertical      ///< children stacked left‑to‑right
};

struct LayoutNode {
    enum Type : uint8_t {
        Leaf   = 0,  ///< contains exactly one panel
        Split  = 1,  ///< arranges children by ratio in one direction
        Tabbed = 2   ///< shows one child at a time with a tab bar
    };

    Type type = Leaf;

    // ── Leaf fields ──
    std::string panelId;   ///< panel ID looked up via PanelRegistry

    // ── Split fields ──
    SplitDir splitDir = SplitDir::Vertical;
    float    ratio    = 0.5f;      ///< proportion for first child (0.0‑1.0)
    std::vector<LayoutNode> children;

    // ── Tabbed fields ──
    size_t activeTab = 0;          ///< index of visible child

    // ── Shared ──
    uint16_t minH = 3;             ///< minimum rows for this node
    uint16_t minW = 10;            ///< minimum cols for this node

    // ── Factory helpers ──

    static LayoutNode leaf(
        const std::string &id, uint16_t minH_ = 3, uint16_t minW_ = 10
    ) {
        LayoutNode n;
        n.type   = Leaf;
        n.panelId = id;
        n.minH   = minH_;
        n.minW   = minW_;
        return n;
    }

    static LayoutNode splitV(float r) {
        LayoutNode n;
        n.type    = Split;
        n.splitDir = SplitDir::Vertical;
        n.ratio   = r;
        return n;
    }

    static LayoutNode splitH(float r) {
        LayoutNode n;
        n.type    = Split;
        n.splitDir = SplitDir::Horizontal;
        n.ratio   = r;
        return n;
    }

    static LayoutNode tabbed() {
        LayoutNode n;
        n.type = Tabbed;
        return n;
    }
};

// ============================================================================
// LayoutSlot — computed placement + focus info for a leaf panel
// ============================================================================

struct LayoutSlot {
    Rect   rect;
    bool   focused    = false;
    size_t tabIndex   = 0;       ///< 0‑based index within parent tabbed node
    size_t tabCount   = 0;       ///< total tabs in parent (0 = not in a tab)
};

// ============================================================================
// LayoutTree — owns the layout tree and computes absolute screen placement
// ============================================================================

/**
 * @brief Builds and manages a configurable layout tree of leaf/split/tabbed nodes.
 *
 * The tree is built once from a preset name (or user‑supplied tree),
 * and then `compute()` is called every frame (after resize) to produce
 * absolute Rect placements for every visible leaf panel.
 *
 * Focus traversal, maximise/unmaximise, and tab switching are all built
 * on top of the computed slot map so panel code never touches absolute
 * coordinates directly.
 */
class LayoutTree {
public:
    LayoutTree();
    ~LayoutTree();

    LayoutTree(const LayoutTree &) = delete;
    LayoutTree &operator=(const LayoutTree &) = delete;
    LayoutTree(LayoutTree &&) = default;
    LayoutTree &operator=(LayoutTree &&) = default;

    // ── Building ──

    /** Construct the tree for a named preset. Returns false for unknown presets. */
    bool buildFromPreset(const std::string &preset);

    /** Build the default 3‑pane layout. */
    void buildDefault();

    /** Build the wide 2‑pane horizontal‑split layout. */
    void buildWide();

    /** Build the tall 2‑pane vertical‑split layout. */
    void buildTall();

    /** Build the minimal single‑pane tabbed layout. */
    void buildMinimal();

    /** Replace the tree with a manually‑built root node. */
    void setRoot(LayoutNode root);

    // ── Layout computation ──

    /**
     * @brief Compute absolute Rect for every leaf, subtracting reserved rows.
     *
     * @param rows       Total terminal rows.
     * @param cols       Total terminal cols.
     * @param statusRows Rows reserved for a status bar at the bottom.
     */
    void compute(uint16_t rows, uint16_t cols, uint16_t statusRows);

    // ── Query ──

    /** Absolute rect for @p panelId, or nullptr if not found. */
    const Rect *rectFor(const std::string &panelId) const;

    /** Ordered list of every leaf panel ID in this tree (for registration checks). */
    std::vector<std::string> panelIds() const;

    /** True when the current terminal size is too small for the minimum constraints. */
    bool terminalTooSmall() const {
        return !m_sizedOk;
    }

    // ── Focus ──

    /** ID of the currently‑focused panel (empty if none). */
    std::string focusedPanel() const {
        return m_focusedPanel;
    }

    /** Advance focus to the next visible leaf panel. */
    void focusNext();

    /** Retreat focus to the previous visible leaf panel. */
    void focusPrev();

    /** Move focus to a specific panel.  Returns false if the ID is unknown. */
    bool setFocus(const std::string &panelId);

    // ── Maximise ──

    /** Maximise the focused panel (or un‑maximise if already maximised). */
    void toggleMaximize();

    /** True if a panel is currently maximised. */
    bool isMaximized() const {
        return m_maximized;
    }

    /** Panel ID that is currently maximised (empty if not maximised). */
    std::string maximizedPanel() const {
        return m_maximizedPanel;
    }

    // ── Tab navigation ──

    /** Switch to the next tab in the tabbed parent of the focused panel.
     *  No‑op if the focused panel is not inside a tabbed node. */
    void focusTabNext();

    /** True if the focused panel lives inside a tabbed node. */
    bool isFocusedInTabbed() const;

    // ── Iteration ──

    /** Visit every visible leaf panel with its computed rect and focus flag. */
    void forEachLeaf(std::function<void(
        const std::string &panelId,
        const Rect &rect,
        bool focused
    )> fn) const;

private:
    LayoutNode  m_root;
    std::string m_preset;

    // ── Computed state ──
    std::map<std::string, LayoutSlot> m_slots;
    std::string        m_focusedPanel;
    std::vector<std::string> m_focusOrder;   ///< visual traversal order of leaves

    bool m_maximized      = false;
    std::string m_maximizedPanel;

    uint16_t m_termRows   = 0;
    uint16_t m_termCols   = 0;
    uint16_t m_statusRows = 0;
    bool     m_sizedOk    = false;

    // ── Internal helpers ──

    /** Recursively compute rects.  Returns true if every subtree fits. */
    bool computeRect(
        const LayoutNode &node,
        uint16_t row, uint16_t col,
        uint16_t h, uint16_t w
    );

    /** Collect all leaf panel IDs in visual order into @p ids. */
    void collectLeafOrder(
        const LayoutNode &node, std::vector<std::string> &ids
    ) const;

    /** Collect every leaf panel ID regardless of tab state (for validation). */
    void collectAllLeafIds(
        const LayoutNode &node, std::vector<std::string> &ids
    ) const;

    /** Rebuild m_focusOrder from m_slots by collecting leaves in visual order. */
    void rebuildFocusOrder();

    /** Clear the focused flag on all slots. */
    void clearFocusInSlots();

    /** Advance activeTab in the tabbed node containing panelId. */
    bool advanceTabInNode(
        LayoutNode &node,
        const std::string &panelId,
        size_t tabCount
    );

    /** Find the first leaf in the active tab of the tabbed node containing panelId. */
    std::string getFirstLeafInActiveTab(
        const LayoutNode &node,
        const std::string &panelId
    ) const;

    /** True if panelId lives anywhere under @p node. */
    bool isPanelInSubtree(
        const LayoutNode &node, const std::string &panelId
    ) const;

    /** Draw tab bars for all tabbed nodes in the tree. */
    void drawTabBarsRecursive(Canvas &canvas, const LayoutNode &node) const;

public:
    /** Draw tab bars for all tabbed nodes onto the canvas. */
    void drawTabBars(Canvas &canvas) const;
};

} // namespace tui

#endif /* __TUI_LAYOUT_HPP__ */
