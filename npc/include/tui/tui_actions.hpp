#ifndef __TUI_ACTIONS_HPP__
#define __TUI_ACTIONS_HPP__ 1

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <tui/tui_config.hpp>

namespace tui {

// ============================================================================
// Action — semantic TUI commands dispatched from keybindings
// ============================================================================

enum class Action : uint8_t {
    NONE             = 0,
    QUIT             = 1,
    FOCUS_NEXT       = 2,
    FOCUS_PREV       = 3,
    TOGGLE_MAXIMIZE  = 4,
    TAB_NEXT         = 5,
    PAUSE_RESUME     = 6,
    RESET            = 7,
    STEP_INST        = 8,
    STEP_CLOCK       = 9,
    HELP_OVERLAY     = 10,  ///< toggle command‑palette overlay on/off
    PANEL_PICKER     = 11,  ///< switch to a different visible panel
    RESIZE_UP        = 12,  ///< shift split ratio up (reserved)
    RESIZE_DOWN      = 13,  ///< shift split ratio down (reserved)
    RESIZE_LEFT      = 14,  ///< shift split ratio left (reserved)
    RESIZE_RIGHT     = 15,  ///< shift split ratio right (reserved)
    SCROLL_UP        = 16,  ///< move panel viewport up
    SCROLL_DOWN      = 17,  ///< move panel viewport down
    SCROLL_LEFT      = 18,  ///< move panel viewport left
    SCROLL_RIGHT     = 19,  ///< move panel viewport right
};

/** Human‑readable name for an Action. */
const char *actionName(Action a);

/** Human‑readable key chord string for help rendering. */
std::string formatKeyChord(const struct KeyChord &chord);

// ============================================================================
// KeyChord — a keypress with optional modifiers
// ============================================================================

/**
 * @brief Bitmask for key modifiers.
 */
enum KeyMod : uint8_t {
    kModNone  = 0,
    kModShift = 1 << 0,
    kModCtrl  = 1 << 1,
    kModAlt   = 1 << 2,
};

/**
 * @brief Special key codes (above the ASCII range 0–127).
 *
 * Values 32–126 are used as‑is for printable ASCII glyphs.
 * Control‑byte values 1–26 from the terminal are represented as
 * kModCtrl + the base letter (e.g. Ctrl+A → 0x01 → Ctrl + 'a').
 */
namespace SpecialKey {
    constexpr uint32_t kTab       = 0x100;
    constexpr uint32_t kEnter     = 0x101;
    constexpr uint32_t kEsc       = 0x102;
    constexpr uint32_t kBackspace = 0x103;
    constexpr uint32_t kSpace     = 0x104;
    constexpr uint32_t kUp        = 0x110;
    constexpr uint32_t kDown      = 0x111;
    constexpr uint32_t kLeft      = 0x112;
    constexpr uint32_t kRight     = 0x113;
    constexpr uint32_t kF1        = 0x120;
    constexpr uint32_t kF2        = 0x121;
    constexpr uint32_t kF3        = 0x122;
    constexpr uint32_t kF4        = 0x123;
    constexpr uint32_t kF5        = 0x124;
    constexpr uint32_t kF6        = 0x125;
    constexpr uint32_t kF7        = 0x126;
    constexpr uint32_t kF8        = 0x127;
    constexpr uint32_t kF9        = 0x128;
    constexpr uint32_t kF10       = 0x129;
    constexpr uint32_t kF11       = 0x12A;
    constexpr uint32_t kF12       = 0x12B;
}

struct KeyChord {
    uint8_t  mod  = 0;       ///< bitmask of KeyMod
    uint32_t key  = 0;       ///< ASCII code or SpecialKey value

    bool operator==(const KeyChord &o) const {
        return mod == o.mod && key == o.key;
    }
    bool operator<(const KeyChord &o) const {
        return mod != o.mod ? mod < o.mod : key < o.key;
    }
};

// ============================================================================
// Key‑string parser
// ============================================================================

/**
 * @brief Parse a TOML‑style keybinding string into a KeyChord.
 *
 * Supported forms:
 *   "q"             single printable ASCII (case‑sensitive glyph)
 *   "tab"           named key
 *   "shift+tab"     modifier + named key
 *   "ctrl+q"        modifier + ASCII letter (lowercase)
 *   "alt+up"        modifier + arrow
 *
 * Named keys: tab, space, enter, esc, backspace,
 *             up, down, left, right,
 *             f1–f12
 *
 * @param spec   The raw keybinding string from TOML config.
 * @return The parsed KeyChord.
 * @throws std::runtime_error if @p spec is not valid.
 */
KeyChord parseKeyChord(const std::string &spec);

/**
 * @brief Validate a keybinding string.
 *
 * @param spec   The raw keybinding string to check.
 * @param actionName Human‑readable action label for error messages.
 * @return true if @p spec parses successfully.
 *
 * On failure, prints a precise diagnostic to stderr naming the
 * offending key string and the action it belongs to.
 */
bool validateKeyBinding(const std::string &spec, const char *actionName);

/** Read a complete key chord from STDIN_FILENO. Returns nullopt on EOF. */
std::optional<KeyChord> readKeyChord();

// ============================================================================
// Action map — maps KeyChord → Action for dispatch
// ============================================================================

class ActionMap {
public:
    ActionMap() = default;

    /** Build the map from a TuiConfig::Keybindings block. */
    void buildFromConfig(const struct TuiConfig::Keybindings &kb);

    /** Look up an action for the given chord. */
    Action lookup(const KeyChord &chord) const;

    /** Return all chords that map to a non‑NONE action (for key‑hint rendering). */
    std::vector<std::pair<KeyChord, Action>> allBindings() const;

private:
    std::map<KeyChord, Action> m_map;
};

} // namespace tui

#endif /* __TUI_ACTIONS_HPP__ */
