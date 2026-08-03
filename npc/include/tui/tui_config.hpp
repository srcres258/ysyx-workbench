#ifndef __TUI_CONFIG_HPP__
#define __TUI_CONFIG_HPP__ 1

#include <cstdint>
#include <string>
#include <ostream>

namespace tui {

// ============================================================================
// TuiConfig — TOML‑backed TUI configuration model
// ============================================================================

struct TuiConfig {
    // ---- [keybindings] ----
    struct Keybindings {
        std::string focus_next        = "tab";
        std::string focus_prev        = "shift+tab";
        std::string help_overlay      = "h";
        std::string maximize_toggle   = "m";
        std::string panel_picker      = "p";
        std::string pause_resume      = "space";
        std::string quit              = "q";
        std::string reset             = "r";
        std::string resize_down       = "down";
        std::string resize_left       = "left";
        std::string resize_right      = "right";
        std::string resize_up         = "up";
        std::string step_clock        = "c";
        std::string step_instruction  = "s";
        std::string tab_next          = "t";
    } keybindings;

    // ---- [layout] ----
    struct Layout {
        std::string preset = "default";  // default | wide | tall | minimal
    } layout;

    // ---- [regs] ----
    struct Regs {
        bool        abi_names        = true;
        bool        highlight_changed = true;
    } regs;

    // ---- [render] ----
    struct Render {
        std::string box_border_style = "single";  // single | double | rounded
        std::string theme            = "default"; // default | dark | light
    } render;

    // ---- [trace] ----
    struct Trace {
        int32_t     buffer_size      = 1024;
        bool        follow_tail      = true;
        bool        show_disasm      = true;
    } trace;

    // ---- [ui] ----
    struct Ui {
        int32_t     refresh_hz       = 30;
        bool        show_fps         = true;
        bool        status_bar       = true;
    } ui;
};

// ============================================================================
// Config system API
// ============================================================================

/**
 * @brief Parse a TOML config file into a TuiConfig.
 *
 * Missing optional keys are left at their defaults.
 * Unknown top‑level tables are ignored (forward‑compat).
 *
 * @throw toml::parse_error  on syntax error.
 * @return Parsed config with defaults applied for missing sections.
 */
TuiConfig parseTuiConfig(const std::string &path);

/**
 * @brief Validate a TuiConfig and return false if any value is out‑of‑range.
 *
 * Prints warnings for unknown/invalid values and applies clamping for
 * numeric fields where sensible.
 */
bool validateTuiConfig(TuiConfig &cfg);

/**
 * @brief Serialize a TuiConfig as formatted TOML with human‑editable comments.
 *
 * Comments are manually emitted because toml++ discards them during
 * parse and provides no comment‑emission API.
 *
 * Writes to @p os.
 */
void serializeTuiConfig(std::ostream &os, const TuiConfig &cfg, bool full = true);

/**
 * @brief Print the config schema (section‑by‑section documentation) to @p os.
 */
void printTuiConfigSchema(std::ostream &os);

/**
 * @brief Print a complete default config with comments to @p os.
 */
void printTuiDefaultConfig(std::ostream &os);

/**
 * @brief Generate (write) a minimal config file to @p path.
 *
 * Creates parent directories as needed.
 * Returns false if the file already exists and @p forceOverwrite is false.
 * Returns true on success or if the file exists and @p forceOverwrite is true.
 */
bool generateTuiConfig(
    const std::string &path, bool forceOverwrite, bool full = false
);

/**
 * @brief Load global config: parse file if it exists, or auto‑generate a
 *        minimal one if missing.  Populates g_tuiConfig.
 *
 * @return true on success, false on parse/validation/file‑creation error.
 */
bool loadOrGenerateTuiConfig(const std::string &path);

/**
 * @brief Global TUI config instance, populated at startup before simulation.
 *
 * Accessible from sim.cpp and all panel code via this extern.
 */
extern TuiConfig g_tuiConfig;

} // namespace tui

#endif /* __TUI_CONFIG_HPP__ */
