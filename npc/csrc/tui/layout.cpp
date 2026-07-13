#include <tui/layout.hpp>
#include <tui/renderer.hpp>
#include <algorithm>
#include <iostream>

namespace tui {

// ============================================================================
// LayoutTree — construction
// ============================================================================

LayoutTree::LayoutTree() = default;
LayoutTree::~LayoutTree() = default;

void LayoutTree::setRoot(LayoutNode root) {
    m_root = std::move(root);
    // Default focus: first leaf
    m_focusOrder.clear();
    collectLeafOrder(m_root, m_focusOrder);
    if (!m_focusOrder.empty()) {
        m_focusedPanel = m_focusOrder[0];
    }
    m_maximized = false;
    m_maximizedPanel.clear();
}

// ============================================================================
// Presets
// ============================================================================

bool LayoutTree::buildFromPreset(const std::string &preset) {
    m_preset = preset;
    if (preset == "wide")           { buildWide();    return true; }
    else if (preset == "tall")      { buildTall();    return true; }
    else if (preset == "minimal")   { buildMinimal(); return true; }
    else if (preset == "default")   { buildDefault(); return true; }
    return false;
}

void LayoutTree::buildDefault() {
    // V-split 50/50: left = H-split 40/60, right = tabbed
    LayoutNode root = LayoutNode::splitV(0.50f);
    root.minH = 8;
    root.minW = 40;

    LayoutNode left = LayoutNode::splitH(0.40f);
    left.children.push_back(LayoutNode::leaf("core", 4, 20));
    LayoutNode left1 = LayoutNode::splitH(0.50f);
    left1.children.push_back(LayoutNode::leaf("func", 5, 20));
    left1.children.push_back(LayoutNode::leaf("regs", 5, 20));
    left.children.push_back(std::move(left1));

    LayoutNode right = LayoutNode::tabbed();
    right.children.push_back(LayoutNode::leaf("inst",   8, 25));
    right.children.push_back(LayoutNode::leaf("trace",  4, 20));
    right.children.push_back(LayoutNode::leaf("events", 4, 20));
    right.children.push_back(LayoutNode::leaf("csr",    6, 20));
    right.children.push_back(LayoutNode::leaf("perf",   6, 20));

    root.children.push_back(std::move(left));
    root.children.push_back(std::move(right));

    setRoot(std::move(root));
}

void LayoutTree::buildWide() {
    // H-split 40/60: top = tabbed, bottom = tabbed
    LayoutNode root = LayoutNode::splitH(0.40f);
    root.minH = 10;
    root.minW = 30;

    LayoutNode top = LayoutNode::tabbed();
    top.children.push_back(LayoutNode::leaf("inst",   8, 25));
    top.children.push_back(LayoutNode::leaf("func",   8, 25));
    top.children.push_back(LayoutNode::leaf("core", 4, 20));
    top.children.push_back(LayoutNode::leaf("regs", 6, 20));
    top.children.push_back(LayoutNode::leaf("csr",  6, 20));

    LayoutNode bottom = LayoutNode::tabbed();
    bottom.children.push_back(LayoutNode::leaf("trace",  4, 20));
    bottom.children.push_back(LayoutNode::leaf("events", 4, 20));
    bottom.children.push_back(LayoutNode::leaf("perf",   6, 20));

    root.children.push_back(std::move(top));
    root.children.push_back(std::move(bottom));

    setRoot(std::move(root));
}

void LayoutTree::buildTall() {
    // V-split 50/50: left = tabbed, right = tabbed
    LayoutNode root = LayoutNode::splitV(0.50f);
    root.minH = 8;
    root.minW = 30;

    LayoutNode left = LayoutNode::tabbed();
    left.children.push_back(LayoutNode::leaf("inst",   8, 25));
    left.children.push_back(LayoutNode::leaf("func",   8, 25));
    left.children.push_back(LayoutNode::leaf("core", 4, 15));
    left.children.push_back(LayoutNode::leaf("regs", 6, 15));

    LayoutNode right = LayoutNode::tabbed();
    right.children.push_back(LayoutNode::leaf("trace",  4, 15));
    right.children.push_back(LayoutNode::leaf("events", 4, 15));
    right.children.push_back(LayoutNode::leaf("csr",    6, 15));
    right.children.push_back(LayoutNode::leaf("perf",   6, 15));

    root.children.push_back(std::move(left));
    root.children.push_back(std::move(right));

    setRoot(std::move(root));
}

void LayoutTree::buildMinimal() {
    LayoutNode root = LayoutNode::tabbed();
    root.children.push_back(LayoutNode::leaf("inst",   8, 25));
    root.children.push_back(LayoutNode::leaf("func",   8, 25));
    root.children.push_back(LayoutNode::leaf("core",   4, 10));
    root.children.push_back(LayoutNode::leaf("regs",   6, 10));
    root.children.push_back(LayoutNode::leaf("csr",    6, 10));
    root.children.push_back(LayoutNode::leaf("trace",  4, 10));
    root.children.push_back(LayoutNode::leaf("events", 4, 10));
    root.children.push_back(LayoutNode::leaf("perf",   6, 10));

    setRoot(std::move(root));
}

// ============================================================================
// Layout computation
// ============================================================================

void LayoutTree::compute(uint16_t rows, uint16_t cols, uint16_t statusRows) {
    m_termRows   = rows;
    m_termCols   = cols;
    m_statusRows = statusRows;

    m_slots.clear();
    m_focusOrder.clear();
    m_sizedOk = false;

    uint16_t contentRows = (rows > statusRows) ? (rows - statusRows) : 0;
    if (contentRows == 0 || cols == 0) return;

    if (m_maximized && !m_maximizedPanel.empty()) {
        // Maximised: focused panel fills the content area
        m_sizedOk = true;
        LayoutSlot slot;
        slot.rect   = Rect{0, 0, contentRows, cols};
        slot.focused = true;
        m_slots[m_maximizedPanel] = slot;
        m_focusOrder.push_back(m_maximizedPanel);
        return;
    }

    m_sizedOk = computeRect(m_root, 0, 0, contentRows, cols);
    if (!m_sizedOk) {
        m_slots.clear();
        m_focusOrder.clear();
        return;
    }

    rebuildFocusOrder();

    // Restore focus if the previously focused panel still exists
    if (!m_focusedPanel.empty()) {
        auto it = m_slots.find(m_focusedPanel);
        if (it == m_slots.end() || m_focusOrder.empty()) {
            m_focusedPanel = m_focusOrder.empty() ? "" : m_focusOrder[0];
        }
    } else if (!m_focusOrder.empty()) {
        m_focusedPanel = m_focusOrder[0];
    }

    // Mark focused slot
    auto fit = m_slots.find(m_focusedPanel);
    if (fit != m_slots.end()) {
        fit->second.focused = true;
    }
}

static uint16_t clampU16(uint16_t val, uint16_t lo, uint16_t hi) {
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

bool LayoutTree::computeRect(const LayoutNode &node,
                              uint16_t row, uint16_t col,
                              uint16_t h, uint16_t w) {
    if (h < node.minH || w < node.minW) return false;

    if (node.type == LayoutNode::Leaf) {
        LayoutSlot slot;
        slot.rect = Rect{row, col, h, w};
        m_slots[node.panelId] = slot;
        return true;
    }

    if (node.type == LayoutNode::Split) {
        if (node.children.size() < 2) return false;

        const auto &c0 = node.children[0];
        const auto &c1 = node.children[1];

        if (node.splitDir == SplitDir::Horizontal) {
            // Stack vertically: c0 on top, c1 on bottom
            uint16_t firstH = static_cast<uint16_t>(h * node.ratio);
            uint16_t minH0 = c0.minH;
            uint16_t minH1 = c1.minH;
            firstH = clampU16(firstH, minH0, (h > minH1) ? (h - minH1) : 0);
            if (firstH < minH0 || (h - firstH) < minH1) return false;
            uint16_t secondH = h - firstH;

            return computeRect(c0, row, col, firstH, w)
                && computeRect(c1, row + firstH, col, secondH, w);
        } else {
            // Stack horizontally: c0 on left, c1 on right
            uint16_t firstW = static_cast<uint16_t>(w * node.ratio);
            uint16_t minW0 = c0.minW;
            uint16_t minW1 = c1.minW;
            firstW = clampU16(firstW, minW0, (w > minW1) ? (w - minW1) : 0);
            if (firstW < minW0 || (w - firstW) < minW1) return false;
            uint16_t secondW = w - firstW;

            return computeRect(c0, row, col, h, firstW)
                && computeRect(c1, row, col + firstW, h, secondW);
        }
    }

    if (node.type == LayoutNode::Tabbed) {
        if (node.children.empty()) return false;

        uint16_t tabBarH = (h >= 2) ? 1u : 0u;
        uint16_t contentH = h - tabBarH;

        size_t active = node.activeTab;
        if (active >= node.children.size()) {
            active = 0;
        }

        const auto &activeChild = node.children[active];
        // Recursively compute the active child
        bool ok = computeRect(activeChild, row + tabBarH, col, contentH, w);
        if (!ok) return false;

        // Mark tab info for all children's slots
        for (size_t i = 0; i < node.children.size(); i++) {
            if (node.children[i].type == LayoutNode::Leaf) {
                auto it = m_slots.find(node.children[i].panelId);
                if (it != m_slots.end()) {
                    it->second.tabIndex = i;
                    it->second.tabCount = node.children.size();
                }
            }
        }

        return true;
    }

    return false;
}

// ============================================================================
// Focus model
// ============================================================================

void LayoutTree::focusNext() {
    if (m_focusOrder.empty()) return;
    clearFocusInSlots();
    auto it = std::find(m_focusOrder.begin(), m_focusOrder.end(), m_focusedPanel);
    if (it == m_focusOrder.end()) {
        m_focusedPanel = m_focusOrder[0];
    } else {
        size_t idx = static_cast<size_t>(it - m_focusOrder.begin());
        idx = (idx + 1) % m_focusOrder.size();
        m_focusedPanel = m_focusOrder[idx];
    }
    auto fit = m_slots.find(m_focusedPanel);
    if (fit != m_slots.end()) fit->second.focused = true;
}

void LayoutTree::focusPrev() {
    if (m_focusOrder.empty()) return;
    clearFocusInSlots();
    auto it = std::find(m_focusOrder.begin(), m_focusOrder.end(), m_focusedPanel);
    if (it == m_focusOrder.end()) {
        m_focusedPanel = m_focusOrder.back();
    } else {
        size_t idx = static_cast<size_t>(it - m_focusOrder.begin());
        idx = (idx == 0) ? (m_focusOrder.size() - 1) : (idx - 1);
        m_focusedPanel = m_focusOrder[idx];
    }
    auto fit = m_slots.find(m_focusedPanel);
    if (fit != m_slots.end()) fit->second.focused = true;
}

bool LayoutTree::setFocus(const std::string &panelId) {
    if (m_slots.find(panelId) == m_slots.end()) return false;
    clearFocusInSlots();
    m_focusedPanel = panelId;
    auto fit = m_slots.find(m_focusedPanel);
    if (fit != m_slots.end()) fit->second.focused = true;
    return true;
}

void LayoutTree::clearFocusInSlots() {
    for (auto &kv : m_slots) {
        kv.second.focused = false;
    }
}

// ============================================================================
// Maximise
// ============================================================================

void LayoutTree::toggleMaximize() {
    if (m_focusedPanel.empty()) return;

    if (m_maximized && m_maximizedPanel == m_focusedPanel) {
        m_maximized = false;
        m_maximizedPanel.clear();
    } else {
        m_maximized = true;
        m_maximizedPanel = m_focusedPanel;
    }

    // Re-compute with current terminal size
    if (m_termRows > 0) {
        compute(m_termRows, m_termCols, m_statusRows);
    }
}

// ============================================================================
// Tab navigation
// ============================================================================

void LayoutTree::focusTabNext() {
    if (m_focusedPanel.empty()) return;

    // Find the slot for the focused panel to get its tab group
    auto it = m_slots.find(m_focusedPanel);
    if (it == m_slots.end() || it->second.tabCount <= 1) return;

    size_t curTab = it->second.tabIndex;
    size_t tabCount = it->second.tabCount;

    // Find all panels in the same tab group and figure out the next one
    // We need to modify m_root.activeTab for the parent tabbed node
    // Strategy: find the tabbed node containing m_focusedPanel, advance its activeTab
    bool found = advanceTabInNode(m_root, m_focusedPanel, tabCount);
    if (found) {
        // Re-compute layout
        if (m_termRows > 0) {
            compute(m_termRows, m_termCols, m_statusRows);
        }
    }
}

bool LayoutTree::advanceTabInNode(LayoutNode &node,
                                   const std::string &panelId,
                                   size_t tabCount) {
    if (node.type == LayoutNode::Tabbed) {
        for (size_t i = 0; i < node.children.size(); i++) {
            if (isPanelInSubtree(node.children[i], panelId)) {
                // Found the tab group containing this panel
                if (node.children.size() > 1) {
                    node.activeTab = (node.activeTab + 1) % node.children.size();
                    return true;
                }
                return false;
            }
        }
    } else if (node.type == LayoutNode::Split) {
        for (auto &child : node.children) {
            if (advanceTabInNode(child, panelId, tabCount)) return true;
        }
    }
    // Leaf nodes never contain other panels
    return false;
}

bool LayoutTree::isPanelInSubtree(const LayoutNode &node, const std::string &panelId) const {
    if (node.type == LayoutNode::Leaf) {
        return node.panelId == panelId;
    }
    for (const auto &child : node.children) {
        if (isPanelInSubtree(child, panelId)) return true;
    }
    return false;
}

bool LayoutTree::isFocusedInTabbed() const {
    auto it = m_slots.find(m_focusedPanel);
    if (it == m_slots.end()) return false;
    return it->second.tabCount > 1;
}

// ============================================================================
// Query helpers
// ============================================================================

const Rect *LayoutTree::rectFor(const std::string &panelId) const {
    auto it = m_slots.find(panelId);
    return (it != m_slots.end()) ? &it->second.rect : nullptr;
}

std::vector<std::string> LayoutTree::panelIds() const {
    std::vector<std::string> ids;
    if (!m_slots.empty()) {
        ids.reserve(m_slots.size());
        for (const auto &kv : m_slots) {
            ids.push_back(kv.first);
        }
    } else {
        collectAllLeafIds(m_root, ids);
    }
    return ids;
}

void LayoutTree::collectAllLeafIds(const LayoutNode &node,
                                    std::vector<std::string> &ids) const {
    if (node.type == LayoutNode::Leaf) {
        ids.push_back(node.panelId);
        return;
    }
    for (const auto &child : node.children) {
        collectAllLeafIds(child, ids);
    }
}

void LayoutTree::forEachLeaf(
    std::function<void(const std::string &, const Rect &, bool)> fn) const {
    for (const auto &kv : m_slots) {
        fn(kv.first, kv.second.rect, kv.second.focused);
    }
}

// ============================================================================
// Internal helpers
// ============================================================================

void LayoutTree::collectLeafOrder(const LayoutNode &node,
                                   std::vector<std::string> &ids) const {
    if (node.type == LayoutNode::Leaf) {
        ids.push_back(node.panelId);
        return;
    }
    if (node.type == LayoutNode::Split) {
        for (const auto &child : node.children) {
            collectLeafOrder(child, ids);
        }
        return;
    }
    if (node.type == LayoutNode::Tabbed) {
        // Only collect the active tab's leaves
        if (node.activeTab < node.children.size()) {
            collectLeafOrder(node.children[node.activeTab], ids);
        }
        return;
    }
}

void LayoutTree::rebuildFocusOrder() {
    m_focusOrder.clear();
    // Walk tree in visual order (left-to-right, top-to-bottom)
    collectLeafOrder(m_root, m_focusOrder);
}

// ============================================================================
// Tab bar drawing
// ============================================================================

void LayoutTree::drawTabBars(Canvas &canvas) const {
    drawTabBarsRecursive(canvas, m_root);
}

void LayoutTree::drawTabBarsRecursive(Canvas &canvas, const LayoutNode &node) const {
    if (node.type == LayoutNode::Tabbed) {
        if (node.children.size() <= 1) {
            // No tab bar needed for single child; still recurse
            if (!node.children.empty()) {
                drawTabBarsRecursive(canvas, node.children[node.activeTab]);
            }
            return;
        }

        // Find the rect of the active tab to locate the tab bar area
        std::string activeId;
        if (node.activeTab < node.children.size()
            && node.children[node.activeTab].type == LayoutNode::Leaf) {
            activeId = node.children[node.activeTab].panelId;
        }

        auto it = m_slots.find(activeId);
        if (it == m_slots.end() || it->second.rect.row == 0) {
            // Can't determine position; skip
            return;
        }

        uint16_t tabRow = it->second.rect.row - 1;  // tab bar is just above content
        uint16_t tabCol = it->second.rect.col;
        uint16_t tabW   = it->second.rect.w;

        // Draw tab bar background
        Style barStyle{kColourWhite, kColourBlack, false, false, false};
        canvas.fill(tabRow, tabCol, 1, tabW, ' ', barStyle);

        // Draw tab labels
        uint16_t x = tabCol;
        for (size_t i = 0; i < node.children.size() && x < tabCol + tabW; i++) {
            const char *label = "?";
            if (node.children[i].type == LayoutNode::Leaf) {
                label = node.children[i].panelId.c_str();
            }

            Style labelStyle;
            if (i == node.activeTab) {
                labelStyle = Style{kColourCyan, kColourBlack, true, false, false};
            } else {
                labelStyle = Style{kColourWhite, kColourBlack, false, false, false};
            }

            size_t labelLen = 0;
            for (const char *p = label; *p && labelLen < 16; p++, labelLen++) {}

            if (x + labelLen + 3 < tabCol + tabW) {
                canvas.put(tabRow, x, ' ', labelStyle);
                canvas.write(tabRow, x + 1, label, labelStyle);
                canvas.put(tabRow, x + static_cast<uint16_t>(labelLen) + 1, ' ', labelStyle);
                x += static_cast<uint16_t>(labelLen) + 3;
            } else {
                // Truncated label
                uint16_t remaining = tabCol + tabW - x;
                if (remaining > 2) {
                    canvas.put(tabRow, x, ' ', labelStyle);
                    for (uint16_t j = 1; j < remaining - 1 && j <= labelLen; j++) {
                        canvas.put(tabRow, x + j, label[j - 1], labelStyle);
                    }
                    canvas.put(tabRow, x + remaining - 1, ' ', labelStyle);
                }
                break;
            }
        }
    }

    // Recurse into children (only active tab for Tabbed)
    if (node.type == LayoutNode::Tabbed) {
        if (node.activeTab < node.children.size()) {
            drawTabBarsRecursive(canvas, node.children[node.activeTab]);
        }
    } else {
        for (const auto &child : node.children) {
            drawTabBarsRecursive(canvas, child);
        }
    }
}

} // namespace tui
