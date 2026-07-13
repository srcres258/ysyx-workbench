#include <tui/tui_actions.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <sstream>
#include <unistd.h>
#include <sys/select.h>

namespace tui {

const char *actionName(Action a) {
    switch (a) {
        case Action::QUIT:             return "quit";
        case Action::FOCUS_NEXT:       return "focus_next";
        case Action::FOCUS_PREV:       return "focus_prev";
        case Action::TOGGLE_MAXIMIZE:  return "maximize_toggle";
        case Action::TAB_NEXT:         return "tab_next";
        case Action::PAUSE_RESUME:     return "pause_resume";
        case Action::RESET:            return "reset";
        case Action::STEP_INST:        return "step_instruction";
        case Action::STEP_CLOCK:       return "step_clock";
        case Action::HELP_OVERLAY:     return "help_overlay";
        case Action::PANEL_PICKER:     return "panel_picker";
        case Action::RESIZE_UP:        return "resize_up";
        case Action::RESIZE_DOWN:      return "resize_down";
        case Action::RESIZE_LEFT:      return "resize_left";
        case Action::RESIZE_RIGHT:     return "resize_right";
        case Action::NONE:             return "none";
    }
    return "unknown";
}

namespace {

std::string toLower(const std::string &s) {
    std::string r;
    r.reserve(s.size());
    for (char c : s) r.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    return r;
}

bool startsWith(const std::string &s, const char *prefix) {
    size_t n = std::strlen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

// ============================================================================
// Named‑key lookup table
// ============================================================================

struct NamedKeyEntry {
    const char *name;
    uint32_t    code;
    bool        allowsShift; ///< shift+<name> is a valid chord
};

const NamedKeyEntry kNamedKeys[] = {
    {"tab",       SpecialKey::kTab,       true},
    {"space",     SpecialKey::kSpace,     false},
    {"enter",     SpecialKey::kEnter,     false},
    {"esc",       SpecialKey::kEsc,       false},
    {"backspace", SpecialKey::kBackspace, false},
    {"up",        SpecialKey::kUp,        true},
    {"down",      SpecialKey::kDown,      true},
    {"left",      SpecialKey::kLeft,      true},
    {"right",     SpecialKey::kRight,     true},
    {"f1",        SpecialKey::kF1,        false},
    {"f2",        SpecialKey::kF2,        false},
    {"f3",        SpecialKey::kF3,        false},
    {"f4",        SpecialKey::kF4,        false},
    {"f5",        SpecialKey::kF5,        false},
    {"f6",        SpecialKey::kF6,        false},
    {"f7",        SpecialKey::kF7,        false},
    {"f8",        SpecialKey::kF8,        false},
    {"f9",        SpecialKey::kF9,        false},
    {"f10",       SpecialKey::kF10,       false},
    {"f11",       SpecialKey::kF11,       false},
    {"f12",       SpecialKey::kF12,       false},
};

const NamedKeyEntry *findNamedKey(const std::string &name) {
    std::string lower = toLower(name);
    for (const auto &e : kNamedKeys) {
        if (lower == e.name) return &e;
    }
    return nullptr;
}

/** True if @p ch is a printable ASCII glyph (32–126, not a named key). */
bool isPrintableAscii(char ch) {
    unsigned char uc = static_cast<unsigned char>(ch);
    return uc >= 32 && uc <= 126;
}

} // anonymous namespace

KeyChord parseKeyChord(const std::string &spec) {
    if (spec.empty()) {
        throw std::runtime_error("empty keybinding string");
    }

    std::string work = spec;
    uint8_t mod = 0;

    // Parse modifier prefixes: "shift+", "ctrl+", "alt+"
    while (true) {
        if (startsWith(work, "shift+") && work.size() > 6) {
            mod |= kModShift;
            work = work.substr(6);
        } else if (startsWith(work, "ctrl+") && work.size() > 5) {
            mod |= kModCtrl;
            work = work.substr(5);
        } else if (startsWith(work, "alt+") && work.size() > 4) {
            mod |= kModAlt;
            work = work.substr(4);
        } else {
            break;
        }
    }

    if (!work.empty() && work[0] == '+') {
        throw std::runtime_error("keybinding \"" + spec
            + "\": stray '+' separator — empty key segment");
    }
    if (work.empty()) {
        throw std::runtime_error("keybinding \"" + spec + "\": missing key after modifier");
    }

    // Check for named keys first.
    const NamedKeyEntry *named = findNamedKey(work);
    if (named) {
        if ((mod & kModShift) && !named->allowsShift) {
            throw std::runtime_error("keybinding \"" + spec
                + "\": shift modifier not valid for key \"" + named->name + "\"");
        }
        return KeyChord{mod, named->code};
    }

    // Single printable ASCII character.
    if (work.size() == 1 && isPrintableAscii(work[0])) {
        char ch = work[0];
        // Uppercase letters imply Shift modifier.
        if (ch >= 'A' && ch <= 'Z') {
            mod |= kModShift;
            ch = static_cast<char>(ch + ('a' - 'A'));
        }
        if (mod & kModCtrl) {
            if (ch < 'a' || ch > 'z') {
                throw std::runtime_error("keybinding \"" + spec
                    + "\": ctrl modifier only valid with lowercase letters a–z");
            }
        }
        return KeyChord{mod, static_cast<uint32_t>(static_cast<unsigned char>(ch))};
    }

    throw std::runtime_error("keybinding \"" + spec
        + "\": unknown key \"" + work + "\"");
}

bool validateKeyBinding(const std::string &spec, const char *label) {
    try {
        parseKeyChord(spec);
        return true;
    } catch (const std::runtime_error &e) {
        std::fprintf(stderr, "[tui] invalid keybinding \"%s\" for action \"%s\": %s\n",
                     spec.c_str(), label, e.what());
        return false;
    }
}

namespace {
    KeyChord decodeSingleByte(unsigned char b);
    std::optional<KeyChord> decodeEscapeSequence();
}

std::optional<KeyChord> readKeyChord() {
    unsigned char first;
    int rd = ::read(STDIN_FILENO, &first, 1);
    if (rd == 0) return std::nullopt;
    if (rd < 0)  return std::nullopt;

    if (first != 0x1B) {
        return decodeSingleByte(first);
    }

    return decodeEscapeSequence();
}

namespace {

KeyChord decodeSingleByte(unsigned char b) {
    switch (b) {
        case 0x09: return KeyChord{kModNone, SpecialKey::kTab};
        case 0x0D: return KeyChord{kModNone, SpecialKey::kEnter};
        case 0x7F: return KeyChord{kModNone, SpecialKey::kBackspace};
        case 0x08: return KeyChord{kModNone, SpecialKey::kBackspace};
        case 0x20: return KeyChord{kModNone, SpecialKey::kSpace};
    }

    if (b >= 1 && b <= 26) {
        char ch = static_cast<char>('a' + b - 1);
        return KeyChord{kModCtrl, static_cast<uint32_t>(static_cast<unsigned char>(ch))};
    }

    if (b >= 32 && b <= 126) {
        uint8_t mod = kModNone;
        uint32_t key = b;
        if (b >= 'A' && b <= 'Z') {
            mod = kModShift;
            key = b + ('a' - 'A');
        }
        return KeyChord{mod, key};
    }

    return KeyChord{kModNone, b};
}

std::optional<KeyChord> decodeEscapeSequence() {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    struct timeval tv{0, 1000};

    int sel = ::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    if (sel <= 0) {
        return KeyChord{kModNone, SpecialKey::kEsc};
    }

    unsigned char buf[5];
    int n = ::read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) {
        return KeyChord{kModNone, SpecialKey::kEsc};
    }

    if (n >= 1 && buf[0] == 'O') {
        if (n >= 2) {
            switch (buf[1]) {
                case 'P': return KeyChord{kModNone, SpecialKey::kF1};
                case 'Q': return KeyChord{kModNone, SpecialKey::kF2};
                case 'R': return KeyChord{kModNone, SpecialKey::kF3};
                case 'S': return KeyChord{kModNone, SpecialKey::kF4};
                default: break;
            }
        }
        return KeyChord{kModNone, SpecialKey::kEsc};
    }

    if (n >= 1 && buf[0] == '[') {
        if (n >= 2 && buf[1] == 'Z') {
            return KeyChord{kModShift, SpecialKey::kTab};
        }
        if (n >= 2) {
            switch (buf[1]) {
                case 'A': return KeyChord{kModNone, SpecialKey::kUp};
                case 'B': return KeyChord{kModNone, SpecialKey::kDown};
                case 'C': return KeyChord{kModNone, SpecialKey::kRight};
                case 'D': return KeyChord{kModNone, SpecialKey::kLeft};
                case 'H': return KeyChord{kModNone, SpecialKey::kTab};
                default: break;
            }
        }
        if (n >= 3 && buf[n - 1] == '~') {
            std::string digits(reinterpret_cast<char *>(buf + 1), n - 2);
            int num = 0;
            try { num = std::stoi(digits); } catch (...) {}
            switch (num) {
                case 11: return KeyChord{kModNone, SpecialKey::kF1};
                case 12: return KeyChord{kModNone, SpecialKey::kF2};
                case 13: return KeyChord{kModNone, SpecialKey::kF3};
                case 14: return KeyChord{kModNone, SpecialKey::kF4};
                case 15: return KeyChord{kModNone, SpecialKey::kF5};
                case 17: return KeyChord{kModNone, SpecialKey::kF6};
                case 18: return KeyChord{kModNone, SpecialKey::kF7};
                case 19: return KeyChord{kModNone, SpecialKey::kF8};
                case 20: return KeyChord{kModNone, SpecialKey::kF9};
                case 21: return KeyChord{kModNone, SpecialKey::kF10};
                case 23: return KeyChord{kModNone, SpecialKey::kF11};
                case 24: return KeyChord{kModNone, SpecialKey::kF12};
                default: break;
            }
        }
    }

    return KeyChord{kModNone, SpecialKey::kEsc};
}

} // anonymous namespace

void ActionMap::buildFromConfig(const TuiConfig::Keybindings &kb) {
    m_map.clear();

    auto bind = [&](const char *name, const std::string &spec, Action a) {
        if (spec.empty()) return;
        try {
            KeyChord chord = parseKeyChord(spec);
            m_map[chord] = a;
        } catch (const std::runtime_error &) {
        }
    };

    bind("focus_next",       kb.focus_next,       Action::FOCUS_NEXT);
    bind("focus_prev",       kb.focus_prev,       Action::FOCUS_PREV);
    bind("help_overlay",     kb.help_overlay,     Action::HELP_OVERLAY);
    bind("maximize_toggle",  kb.maximize_toggle,  Action::TOGGLE_MAXIMIZE);
    bind("panel_picker",     kb.panel_picker,     Action::PANEL_PICKER);
    bind("pause_resume",     kb.pause_resume,     Action::PAUSE_RESUME);
    bind("quit",             kb.quit,             Action::QUIT);
    bind("reset",            kb.reset,            Action::RESET);
    bind("resize_down",      kb.resize_down,      Action::RESIZE_DOWN);
    bind("resize_left",      kb.resize_left,      Action::RESIZE_LEFT);
    bind("resize_right",     kb.resize_right,     Action::RESIZE_RIGHT);
    bind("resize_up",        kb.resize_up,        Action::RESIZE_UP);
    bind("step_clock",       kb.step_clock,       Action::STEP_CLOCK);
    bind("step_instruction", kb.step_instruction, Action::STEP_INST);
    bind("tab_next",         kb.tab_next,         Action::TAB_NEXT);
}

Action ActionMap::lookup(const KeyChord &chord) const {
    if (chord.mod == kModCtrl && chord.key == static_cast<uint32_t>('c')) {
        return Action::QUIT;
    }

    auto it = m_map.find(chord);
    if (it != m_map.end()) return it->second;
    return Action::NONE;
}

std::vector<std::pair<KeyChord, Action>> ActionMap::allBindings() const {
    std::vector<std::pair<KeyChord, Action>> result;
    result.reserve(m_map.size());
    for (const auto &[chord, action] : m_map) {
        result.emplace_back(chord, action);
    }
    return result;
}

} // namespace tui
