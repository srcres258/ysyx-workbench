#include <iostream>
#include <fstream>
#include <cstring>
#include <sys/stat.h>
#include <toml++/toml.hpp>
#include <tui/tui_config.hpp>

namespace tui {

TuiConfig g_tuiConfig;

// ============================================================================
// Internal helpers
// ============================================================================

namespace {

template <typename T>
T getOr(const toml::table &tbl, const char *key, T defaultVal) {
    auto node = tbl[key];
    if (node) {
        if (auto val = node.template value<T>()) {
            return *val;
        }
    }
    return defaultVal;
}

bool getBool(const toml::table &tbl, const char *key, bool defaultVal) {
    auto node = tbl[key];
    if (node) {
        if (auto val = node.value<bool>()) {
            return *val;
        }
    }
    return defaultVal;
}

int32_t getInt(const toml::table &tbl, const char *key, int32_t defaultVal) {
    auto node = tbl[key];
    if (node) {
        if (auto val = node.value<int64_t>()) {
            return static_cast<int32_t>(*val);
        }
    }
    return defaultVal;
}

// Create parent directories for a file path.
bool mkdirParents(const std::string &path) {
    if (path.empty())
        return true;
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return true; // no parent dir
    std::string parent = path.substr(0, pos);
    if (parent.empty())
        return true;
    // Recursively try to create
    struct stat st;
    if (stat(parent.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }
    if (!mkdirParents(parent))
        return false;
    if (mkdir(parent.c_str(), 0755) != 0 && errno != EEXIST) {
        std::cerr << "[tui] failed to create directory " << parent
            << ": " << strerror(errno) << std::endl;
        return false;
    }
    return true;
}

} // anonymous namespace

// ============================================================================
// parseTuiConfig
// ============================================================================

TuiConfig parseTuiConfig(const std::string &path) {
    TuiConfig cfg;
    toml::table tbl;

    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error &e) {
        std::cerr << "[tui] config parse error in " << path << ":\n"
            << e.description() << "\n"
            << "\tat line " << e.source().begin.line
            << ", column " << e.source().begin.column << std::endl;
        throw;
    }

    // [keybindings]
    if (auto *kb = tbl["keybindings"].as_table()) {
        cfg.keybindings.focus_next       = getOr<std::string>(
            *kb, "focus_next",       cfg.keybindings.focus_next
        );
        cfg.keybindings.focus_prev       = getOr<std::string>(
            *kb, "focus_prev",       cfg.keybindings.focus_prev
        );
        cfg.keybindings.help_overlay     = getOr<std::string>(
            *kb, "help_overlay",     cfg.keybindings.help_overlay
        );
        cfg.keybindings.maximize_toggle  = getOr<std::string>(
            *kb, "maximize_toggle",  cfg.keybindings.maximize_toggle
        );
        cfg.keybindings.panel_picker     = getOr<std::string>(
            *kb, "panel_picker",     cfg.keybindings.panel_picker
        );
        cfg.keybindings.pause_resume     = getOr<std::string>(
            *kb, "pause_resume",     cfg.keybindings.pause_resume
        );
        cfg.keybindings.quit             = getOr<std::string>(
            *kb, "quit",             cfg.keybindings.quit
        );
        cfg.keybindings.reset            = getOr<std::string>(
            *kb, "reset",            cfg.keybindings.reset
        );
        cfg.keybindings.resize_down      = getOr<std::string>(
            *kb, "resize_down",      cfg.keybindings.resize_down
        );
        cfg.keybindings.resize_left      = getOr<std::string>(
            *kb, "resize_left",      cfg.keybindings.resize_left
        );
        cfg.keybindings.resize_right     = getOr<std::string>(
            *kb, "resize_right",     cfg.keybindings.resize_right
        );
        cfg.keybindings.resize_up        = getOr<std::string>(
            *kb, "resize_up",        cfg.keybindings.resize_up
        );
        cfg.keybindings.step_clock       = getOr<std::string>(
            *kb, "step_clock",       cfg.keybindings.step_clock
        );
        cfg.keybindings.step_instruction = getOr<std::string>(
            *kb, "step_instruction", cfg.keybindings.step_instruction
        );
        cfg.keybindings.tab_next         = getOr<std::string>(
            *kb, "tab_next",         cfg.keybindings.tab_next
        );
    }

    // [layout]
    if (auto *ly = tbl["layout"].as_table()) {
        cfg.layout.preset = getOr<std::string>(
            *ly, "preset", cfg.layout.preset
        );
    }

    // [regs]
    if (auto *rg = tbl["regs"].as_table()) {
        cfg.regs.abi_names         = getBool(
            *rg, "abi_names",         cfg.regs.abi_names
        );
        cfg.regs.highlight_changed = getBool(
            *rg, "highlight_changed", cfg.regs.highlight_changed
        );
    }

    // [render]
    if (auto *rd = tbl["render"].as_table()) {
        cfg.render.box_border_style = getOr<std::string>(
            *rd, "box_border_style", cfg.render.box_border_style
        );
        cfg.render.theme            = getOr<std::string>(
            *rd, "theme",            cfg.render.theme
        );
    }

    // [trace]
    if (auto *tr = tbl["trace"].as_table()) {
        cfg.trace.buffer_size = getInt(
            *tr, "buffer_size", cfg.trace.buffer_size
        );
        cfg.trace.follow_tail = getBool(
            *tr, "follow_tail", cfg.trace.follow_tail
        );
        cfg.trace.show_disasm = getBool(
            *tr, "show_disasm", cfg.trace.show_disasm
        );
    }

    // [ui]
    if (auto *u = tbl["ui"].as_table()) {
        cfg.ui.refresh_hz = getInt(
            *u, "refresh_hz", cfg.ui.refresh_hz
        );
        cfg.ui.show_fps   = getBool(
            *u, "show_fps",   cfg.ui.show_fps
        );
        cfg.ui.status_bar = getBool(
            *u, "status_bar", cfg.ui.status_bar
        );
    }

    return cfg;
}

// ============================================================================
// validateTuiConfig
// ============================================================================

static bool validateStringEnum(
    const char *section, const char *key,
    const std::string &val,
    const char *const allowed[], size_t n
) {
    for (size_t i = 0; i < n; ++i) {
        if (val == allowed[i])
            return true;
    }
    std::cerr << "[tui] error: [" << section << "] " << key
        << " = \"" << val << "\" is not a recognised value."
        << std::endl;
    return false;
}

bool validateTuiConfig(TuiConfig &cfg) {
    bool ok = true;

    // layout.preset
    {
        static const char *const allowed[] = {
            "default", "wide", "tall", "minimal"
        };
        if (!validateStringEnum(
            "layout", "preset", cfg.layout.preset, allowed, 4
        ))
            ok = false;
    }

    // render.box_border_style
    {
        static const char *const allowed[] = {
            "single", "double", "rounded"
        };
        if (!validateStringEnum(
            "render", "box_border_style", cfg.render.box_border_style,
            allowed, 3
        ))
            ok = false;
    }

    // render.theme
    {
        static const char *const allowed[] = {
            "default", "dark", "light"
        };
        if (!validateStringEnum(
            "render", "theme", cfg.render.theme, allowed, 3
        ))
            ok = false;
    }

    // Numerical bounds
    if (cfg.ui.refresh_hz < 1) {
        std::cerr << "[tui] warning: ui.refresh_hz clamped to 1 (was "
            << cfg.ui.refresh_hz << ")" << std::endl;
        cfg.ui.refresh_hz = 1;
    }
    if (cfg.ui.refresh_hz > 120) {
        std::cerr << "[tui] warning: ui.refresh_hz clamped to 120 (was "
            << cfg.ui.refresh_hz << ")" << std::endl;
        cfg.ui.refresh_hz = 120;
    }
    if (cfg.trace.buffer_size < 64) {
        std::cerr << "[tui] warning: trace.buffer_size clamped to 64 (was "
            << cfg.trace.buffer_size << ")" << std::endl;
        cfg.trace.buffer_size = 64;
    }
    if (cfg.trace.buffer_size > 65536) {
        std::cerr << "[tui] warning: trace.buffer_size clamped to 65536 (was "
            << cfg.trace.buffer_size << ")" << std::endl;
        cfg.trace.buffer_size = 65536;
    }

    return ok;
}

// ============================================================================
// serializeTuiConfig
// ============================================================================

static void emitKeybindings(std::ostream &os, const TuiConfig &cfg, bool full) {
    os << "# ── Keybindings ───────────────────────────────────────────────\n";
    os << "# Key syntax:\n";
    os << "#   Single chars:    q, s, c, r, h, p, m, t\n";
    os << "#   Named keys:      space, tab, up, down, left, right, enter, esc, "
        << "backspace, delete, home, end, pgup, pgdn\n";
    os << "#   Modified keys:   shift+<key>, ctrl+<key>, alt+<key>\n";
    if (full) {
        os << "#\n";
        os << "# These 15 keys cover the v1 interaction surface.\n";
        os << "# All bindings must be valid key tokens; unbound actions are "
            << "no-ops.\n";
    }
    os << "\n[keybindings]\n";
    os << "focus_next = \""       << cfg.keybindings.focus_next       << "\"\n";
    os << "focus_prev = \""       << cfg.keybindings.focus_prev       << "\"\n";
    os << "help_overlay = \""     << cfg.keybindings.help_overlay     << "\"\n";
    os << "maximize_toggle = \""  << cfg.keybindings.maximize_toggle  << "\"\n";
    os << "panel_picker = \""     << cfg.keybindings.panel_picker     << "\"\n";
    os << "pause_resume = \""     << cfg.keybindings.pause_resume     << "\"\n";
    os << "quit = \""             << cfg.keybindings.quit             << "\"\n";
    os << "reset = \""            << cfg.keybindings.reset            << "\"\n";
    os << "resize_down = \""      << cfg.keybindings.resize_down      << "\"\n";
    os << "resize_left = \""      << cfg.keybindings.resize_left      << "\"\n";
    os << "resize_right = \""     << cfg.keybindings.resize_right     << "\"\n";
    os << "resize_up = \""        << cfg.keybindings.resize_up        << "\"\n";
    os << "step_clock = \""       << cfg.keybindings.step_clock       << "\"\n";
    os << "step_instruction = \"" << cfg.keybindings.step_instruction << "\"\n";
    os << "tab_next = \""         << cfg.keybindings.tab_next         << "\"\n";
    os << "\n";
}

static void emitLayout(std::ostream &os, const TuiConfig &cfg, bool full) {
    os << "# ── Layout ───────────────────────────────────────────────────\n";
    os << "# preset: one of default | wide | tall | minimal\n";
    if (full) {
        os << "#   default — 3‑pane: core+regs (left) | trace+events (right)\n";
        os << "#   wide    — 2‑pane horizontal split (more column width)\n";
        os << "#   tall    — 2‑pane vertical split (more rows)\n";
        os << "#   minimal — single‑pane, tab‑based switching\n";
    }
    os << "\n[layout]\n";
    os << "preset = \"" << cfg.layout.preset << "\"\n";
    os << "\n";
}

static void emitRegs(std::ostream &os, const TuiConfig &cfg, bool) {
    os << "# ── Register Panel ───────────────────────────────────────────\n";
    os << "# abi_names: true  → display a0–a7, t0–t6, s0–s11, etc.\n";
    os << "#            false → display x0–x31\n";
    os << "# highlight_changed: bold+colour registers that changed since last "
        << "frame\n";
    os << "\n[regs]\n";
    os << "abi_names = " << (cfg.regs.abi_names ? "true" : "false")
        << "\n";
    os << "highlight_changed = "
        << (cfg.regs.highlight_changed ? "true" : "false")
        << "\n";
    os << "\n";
}

static void emitRender(std::ostream &os, const TuiConfig &cfg, bool) {
    os << "# ── Renderer ─────────────────────────────────────────────────\n";
    os << "# box_border_style: single | double | rounded\n";
    os << "# theme:            default | dark | light\n";
    os << "\n[render]\n";
    os << "box_border_style = \"" << cfg.render.box_border_style << "\"\n";
    os << "theme = \""            << cfg.render.theme            << "\"\n";
    os << "\n";
}

static void emitTrace(std::ostream &os, const TuiConfig &cfg, bool full) {
    os << "# ── Trace Panel ──────────────────────────────────────────────\n";
    os << "# buffer_size: number of retired‑instruction entries (64–65536)\n";
    if (full) {
        os << "# follow_tail: auto‑scroll to newest entry\n";
        os << "# show_disasm: display disassembly alongside hex instruction\n";
    } else {
        os << "# follow_tail: true → auto‑scroll to newest entry\n";
        os << "# show_disasm: true → show disassembly\n";
    }
    os << "\n[trace]\n";
    os << "buffer_size = " << cfg.trace.buffer_size << "\n";
    os << "follow_tail = " << (cfg.trace.follow_tail ? "true" : "false")
        << "\n";
    os << "show_disasm = " << (cfg.trace.show_disasm ? "true" : "false")
        << "\n";
    os << "\n";
}

static void emitUi(std::ostream &os, const TuiConfig &cfg, bool full) {
    os << "# ── UI ───────────────────────────────────────────────────────\n";
    os << "# refresh_hz: frames per second (1–120)\n";
    if (full) {
        os << "# show_fps:   display an FPS counter in the status bar\n";
        os << "# status_bar: show a bottom status bar with state + key hints\n";
    } else {
        os << "# show_fps:   true → show FPS counter in status bar\n";
        os << "# status_bar: true → show bottom status bar\n";
    }
    os << "\n[ui]\n";
    os << "refresh_hz = " << cfg.ui.refresh_hz << "\n";
    os << "show_fps = "   << (cfg.ui.show_fps ? "true" : "false") << "\n";
    os << "status_bar = " << (cfg.ui.status_bar ? "true" : "false") << "\n";
    os << "\n";
}

void serializeTuiConfig(std::ostream &os, const TuiConfig &cfg, bool full) {
    const char *modeLabel = full ? "Full" : "Minimal";
    os << "# =============================================================================\n";
    os << "# NPC TUI Configuration — " << modeLabel << " Generated Config\n";
    os << "# =============================================================================\n";
    os << "#\n";
    os << "# This file is auto‑generated by npc-tui-btop.\n";
    os << "# Lines starting with '#' are comments and ignored by the parser.\n";
    os << "# Keys omitted from the file will be filled with their default values.\n";
    os << "#\n\n";

    emitKeybindings(os, cfg, full);
    emitLayout(os, cfg, full);
    emitRegs(os, cfg, full);
    emitRender(os, cfg, full);
    emitTrace(os, cfg, full);
    emitUi(os, cfg, full);
}

// ============================================================================
// printTuiConfigSchema
// ============================================================================

void printTuiConfigSchema(std::ostream &os) {
    os << "================================================================================\n";
    os << "  NPC TUI Configuration Schema\n";
    os << "================================================================================\n";
    os << "\n";
    os << "Sections:\n";
    os << "  [keybindings]   — Key‑to‑action mappings (15 actions, string values)\n";
    os << "  [layout]        — Layout preset selection\n";
    os << "  [regs]          — Register panel display options\n";
    os << "  [render]        — Visual theme and border style\n";
    os << "  [trace]         — Trace panel buffer and behaviour\n";
    os << "  [ui]            — Refresh rate, FPS display, status bar\n";
    os << "\n";
    os << "── [keybindings] ────────────────────────────────────────────────────────────────\n";
    os << "  focus_next        (string)  key for next pane                          [\"tab\"]\n";
    os << "  focus_prev        (string)  key for previous pane               [\"shift+tab\"]\n";
    os << "  help_overlay      (string)  key to toggle help overlay                   [\"h\"]\n";
    os << "  maximize_toggle   (string)  key to maximise/restore focused pane         [\"m\"]\n";
    os << "  panel_picker      (string)  key to open panel picker                    [\"p\"]\n";
    os << "  pause_resume      (string)  key to pause/resume simulation          [\"space\"]\n";
    os << "  quit              (string)  key to quit the TUI                          [\"q\"]\n";
    os << "  reset             (string)  key to reset the simulator                   [\"r\"]\n";
    os << "  resize_down       (string)  key to increase focused pane height     [\"down\"]\n";
    os << "  resize_left       (string)  key to decrease focused pane width      [\"left\"]\n";
    os << "  resize_right      (string)  key to increase focused pane width     [\"right\"]\n";
    os << "  resize_up         (string)  key to decrease focused pane height       [\"up\"]\n";
    os << "  step_clock        (string)  key to step one clock cycle                   [\"c\"]\n";
    os << "  step_instruction  (string)  key to step one instruction                   [\"s\"]\n";
    os << "  tab_next          (string)  key for next tab in tabbed pane              [\"t\"]\n";
    os << "\n";
    os << "── [layout] ─────────────────────────────────────────────────────────────────────\n";
    os << "  preset            (string)  default | wide | tall | minimal       [\"default\"]\n";
    os << "\n";
    os << "── [regs] ──────────────────────────────────────────────────────────────────────\n";
    os << "  abi_names         (bool)    use ABI names (a0, t0...) instead of xN     [true]\n";
    os << "  highlight_changed (bool)    highlight registers changed since last frame [true]\n";
    os << "\n";
    os << "── [render] ────────────────────────────────────────────────────────────────────\n";
    os << "  box_border_style  (string)  single | double | rounded              [\"single\"]\n";
    os << "  theme             (string)  default | dark | light               [\"default\"]\n";
    os << "\n";
    os << "── [trace] ─────────────────────────────────────────────────────────────────────\n";
    os << "  buffer_size       (int)     retired‑instruction buffer size (64–65536)   [1024]\n";
    os << "  follow_tail       (bool)    auto‑scroll to newest entry                  [true]\n";
    os << "  show_disasm       (bool)    show disassembly alongside hex               [true]\n";
    os << "\n";
    os << "── [ui] ────────────────────────────────────────────────────────────────────────\n";
    os << "  refresh_hz        (int)     frames per second (1–120)                      [30]\n";
    os << "  show_fps          (bool)    display FPS counter in status bar            [true]\n";
    os << "  status_bar        (bool)    show bottom status bar                       [true]\n";
    os << "\n";
    os << "================================================================================\n";
}

// ============================================================================
// printTuiDefaultConfig
// ============================================================================

void printTuiDefaultConfig(std::ostream &os) {
    TuiConfig cfg;
    serializeTuiConfig(os, cfg, true);
}

// ============================================================================
// generateTuiConfig
// ============================================================================

bool generateTuiConfig(const std::string &path, bool forceOverwrite, bool full) {
    // Check if file exists
    std::ifstream check(path);
    if (check.good()) {
        check.close();
        if (!forceOverwrite) {
            std::cerr << "[tui] config file already exists: " << path
                << "\n      use NPC_CONFIG_TUI_FORCE_OVERWRITE_CONFIG=on to overwrite."
                << std::endl;
            return false;
        }
        std::cout << "[tui] overwriting existing config: " << path << std::endl;
    }

    // Create parent directories
    if (!mkdirParents(path)) {
        std::cerr << "[tui] failed to create parent directories for " << path << std::endl;
        return false;
    }

    // Write config
    std::ofstream out(path);
    if (!out) {
        std::cerr << "[tui] failed to open " << path << " for writing: "
            << strerror(errno) << std::endl;
        return false;
    }

    TuiConfig cfg;
    serializeTuiConfig(out, cfg, full);
    out.close();

    if (!out) {
        std::cerr << "[tui] write error while generating " << path << std::endl;
        return false;
    }

    std::cout << "[tui] config written to " << path
        << " (" << (full ? "full" : "minimal") << ")" << std::endl;
    return true;
}

bool loadOrGenerateTuiConfig(const std::string &path) {
    std::ifstream check(path);
    if (!check.good()) {
        std::cout << "[tui] config file not found, generating minimal config at "
            << path << std::endl;
        if (!generateTuiConfig(path, false, false)) {
            return false;
        }
        // Re‑parse the freshly‑generated file
    } else {
        check.close();
    }

    try {
        g_tuiConfig = parseTuiConfig(path);
    } catch (const toml::parse_error &e) {
        std::cerr << "[tui] failed to parse config file; using defaults."
            << std::endl;
        // Fall through — g_tuiConfig keeps its default values
    }

    if (!validateTuiConfig(g_tuiConfig)) {
        std::cerr << "[tui] config validation failed; aborting." << std::endl;
        return false;
    }
    std::cout << "[tui] config loaded from " << path << std::endl;
    return true;
}

} // namespace tui
