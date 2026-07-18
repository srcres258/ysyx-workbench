# NPC TUI Configuration

The NPC TUI uses TOML for all user‑facing configuration. The config file is loaded at
startup via the existing `Makefile var → env var → C++` pipeline. No binary CLI
arguments are introduced.

## Quick Start

Generate a minimal config file:

```bash
nix develop --command make -C npc run \
  RUN_CONFIG_TUI_GENERATE_CONFIG=on \
  RUN_CONFIG_TUI_CONFIG_FILE_PATH=build/npc-tui.toml
```

Configure the TUI by editing `build/npc-tui.toml`, then launch:

```bash
nix develop --command make -C npc run \
  RUN_CONFIG_TUI=on \
  RUN_SDB_ENABLED=false \
  IMG=path/to/program.bin
```

## Runtime Commands

| Env Variable | Purpose |
|---|---|
| `RUN_CONFIG_TUI=on` | Enable TUI mode |
| `RUN_CONFIG_TUI_CONFIG_FILE_PATH=path` | Override config file path (default: `build/npc-tui.toml`) |
| `RUN_CONFIG_TUI_PRINT_CONFIG_SCHEMA=on` | Print schema docs and exit |
| `RUN_CONFIG_TUI_PRINT_DEFAULT_CONFIG=on` | Print full default TOML and exit |
| `RUN_CONFIG_TUI_GENERATE_CONFIG=on` | Write config file; refuses if file exists |
| `RUN_CONFIG_TUI_GENERATE_FULL_CONFIG=on` | Used with GENERATE_CONFIG to emit verbose comments |
| `RUN_CONFIG_TUI_FORCE_OVERWRITE_CONFIG=on` | Overwrite existing config file |

All commands above exit before simulation starts — no Verilator boot required.

## Auto‑Generation

When `RUN_CONFIG_TUI=on` and the config file is missing, a minimal TOML file is
automatically created with all default values and section comments. The freshly‑generated
file is then parsed and used for the TUI session.

## Schema Reference

### [keybindings]

15 key‑to‑action mappings. Each value is a key token string.

**Key syntax:**

```
Single chars:   q, s, c, r, h, p, m, t
Named keys:     space, tab, up, down, left, right, enter, esc,
                backspace, delete, home, end, pgup, pgdn
Modified keys:  shift+<key>, ctrl+<key>, alt+<key>
```

**Fields (all `string`, all optional):**

| Field | Default | Action |
|---|---|---|
| `focus_next` | `"tab"` | Next pane |
| `focus_prev` | `"shift+tab"` | Previous pane |
| `help_overlay` | `"h"` | Toggle help overlay |
| `maximize_toggle` | `"m"` | Maximise / restore focused pane |
| `panel_picker` | `"p"` | Open panel picker |
| `pause_resume` | `"space"` | Pause / resume simulation |
| `quit` | `"q"` | Quit TUI |
| `reset` | `"r"` | Reset simulator |
| `resize_down` | `"down"` | Increase pane height |
| `resize_left` | `"left"` | Decrease pane width |
| `resize_right` | `"right"` | Increase pane width |
| `resize_up` | `"up"` | Decrease pane height |
| `step_clock` | `"c"` | Step one clock cycle |
| `step_instruction` | `"s"` | Step one instruction |
| `tab_next` | `"t"` | Next tab (tabbed panes) |

### [layout]

| Field | Type | Default | Values |
|---|---|---|---|
| `preset` | `string` | `"default"` | `default`, `wide`, `tall`, `minimal` |

- `default` — 3‑pane: core+regs (left), trace+events (right)
- `wide` — 2‑pane horizontal split
- `tall` — 2‑pane vertical split
- `minimal` — single‑pane, tab‑based switching

### [regs]

| Field | Type | Default | Description |
|---|---|---|---|
| `abi_names` | `bool` | `true` | Use ABI names (a0, t0, …) instead of xN |
| `highlight_changed` | `bool` | `true` | Bold+colour changed registers |

### [render]

| Field | Type | Default | Values |
|---|---|---|---|
| `box_border_style` | `string` | `"single"` | `single`, `double`, `rounded` |
| `theme` | `string` | `"default"` | `default`, `dark`, `light` |

### [trace]

| Field | Type | Default | Range | Description |
|---|---|---|---|---|
| `buffer_size` | `int` | `1024` | 64–65536 | Retired‑instruction buffer size |
| `follow_tail` | `bool` | `true` | — | Auto‑scroll to newest entry |
| `show_disasm` | `bool` | `true` | — | Show disassembly alongside hex |

### [ui]

| Field | Type | Default | Range | Description |
|---|---|---|---|---|
| `refresh_hz` | `int` | `30` | 1–120 | Frames per second |
| `show_fps` | `bool` | `true` | — | FPS counter in status bar |
| `status_bar` | `bool` | `true` | — | Bottom status bar |

## Comment Handling

Comments in the generated TOML file are **manually emitted** before each section.
The underlying TOML library (toml++) does not preserve comments during parse, so
round‑tripping a file will drop any hand‑written comments. Edit the file with
care — re‑generation will replace everything.

## Error Handling

- **Missing config file** → auto‑generated (minimal defaults)
- **Parse error** → defaults used; error printed to stderr
- **Invalid enum value** → warning on stderr; value accepted
- **Out‑of‑range numeric** → clamped to nearest bound with warning
