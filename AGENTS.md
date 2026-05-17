# AGENTS.md — ysyx-workbench

"一生一芯" (One Student One Chip) educational RISC-V processor design project. A meta-repo tying together a CPU emulator, an AbstractMachine runtime, a Chisel-based SoC, and a Verilator-based processor simulation.

## Quick Start — Environment

```bash
# Activate nix dev shell — provides ALL dependencies and sets ALL env vars:
nix develop
# Sets: NEMU_HOME, AM_HOME, NPC_HOME, NVBOARD_HOME, YSYX_HOME, VERILATOR_HOME,
#       PKG_CONFIG_PATH, LD_LIBRARY_PATH

# Without nix, manually:
export NEMU_HOME=$(pwd)/nemu
export AM_HOME=$(pwd)/abstract-machine
export NPC_HOME=$(pwd)/npc
export NVBOARD_HOME=$(pwd)/nvboard
export YSYX_HOME=$(pwd)
export VERILATOR_HOME=$(nix build --no-link --print-out-paths nixpkgs#verilator)/share/verilator
```

Dependencies in `flake.nix`: verilator, gtkwave, circt, iverilog, SDL2 (+ image + ttf), SDL3 (+ image + ttf), capstone, libelf, libz, gcc, gnumake, pkg-config.

For fish shell users: `config.fish` provides partial env setup. Prefer `nix develop`.

## Repository Architecture

This is a **meta-repo with 4 git submodules** (not tracked in the top-level repo directly):

| Submodule | Remote (fork) | Upstream | Branch |
|-----------|--------------|----------|--------|
| `am-kernels/` | srcres258/ysyx-am-kernels | NJU-ProjectN/am-kernels | master |
| `npc/vsrc-chisel/` | srcres258/chisel-ysyx-cpu | — | npc-b2 |
| `rt-thread/` | srcres258/rt-thread-am | fork of RT-Thread/rt-thread | master |
| `ysyxSoC/` | srcres258/ysyx-ysyxSoC | OSCPU/ysyxSoC | ysyx6-own |

**Note**: `fceux-am/` is an **independent git repo** (NOT a submodule) — cloned by `init.sh`, `.git` preserved, gitignored. `nemu/`, `abstract-machine/`, `npc/`, `nvboard/` are direct directories tracked by the top-level repo, initialized via `init.sh`.

## Subproject Map

| Directory | Purpose | Build Tool |
|-----------|---------|------------|
| `nemu/` | ISA simulator (golden reference) | GNU Make + Kconfig |
| `abstract-machine/` | Bare-metal runtime (AM) | GNU Make |
| `am-kernels/` | Test kernels running on AM | GNU Make (via AM) |
| `npc/` | RISC-V CPU (Verilator simulation) | GNU Make + Verilator |
| `npc/vsrc-chisel/` | Chisel frontend for CPU generation | Mill |
| `nvboard/` | Virtual FPGA board (SDL2 GUI) | GNU Make (library) |
| `ysyxSoC/` | SoC integration (wraps CPU + periphs) | Mill + firtool |
| `fceux-am/` | NES emulator (FCEUX) ported to AM | GNU Make (via AM) |
| `rt-thread/` | RT-Thread RTOS with AM BSP | SCons + GNU Make (via AM) |

## NEMU — ISA Simulator (Golden Reference)

License: Mulan PSL v2 (NOT BSD 3-Clause like the root).

```bash
make -C nemu menuconfig          # Kconfig-based configuration
make -C nemu riscv32-am_defconfig # Preset (builds NEMU as AM app)
make -C nemu                     # Build
make -C nemu run                 # Run (needs build/program.elf)
make -C nemu gdb                 # Debug under GDB
```

**Critical for difftest**: NEMU must be built as a shared library via `TARGET_SHARE=y` in Kconfig. NPC expects `build/riscv32-nemu-interpreter-so` by default. Current `.config` is pre-configured for `riscv32` + `TARGET_SHARE=y`.

**Key facts**:
- **Config system**: Linux-kernel-style Kconfig (`menuconfig` → `.config` → `include/config/auto.conf`).
- **Source discovery**: Each subdirectory has `filelist.mk` with `DIRS-y`/`SRCS-y`. NOT a flat list.
- **`NEMU_HOME` required** — sanity-checks `$(NEMU_HOME)/src/nemu-main.c` exists.
- **`build/program.elf` required for `make run`** — usually a symlink to your kernel ELF.
- **`.gitignore` is whitelist-based** — `build/` is NOT gitignored.
- **Supported ISAs**: x86, mips32, riscv32, riscv64, loongarch32r.

## Abstract Machine (AM) — Bare-Metal Runtime

**ARCH naming**: `{ISA}-{PLATFORM}`. Auto-discovered from `scripts/*.mk`.

**Key gotcha — platform confusion**:
- `ARCH=riscv32e-ysyxsoc` → ysyxSoC-based NPC (AXI4 interface, `-march=rv32e_zicsr -mabi=ilp32e`)
- `ARCH=riscv32e-npc` → direct NPC (plain memory interface)
- **Use `riscv32e-ysyxsoc` when building kernels for the ysyxSoC-based NPC.**

**Two different memory maps**:
- **NEMU/NPC**: Physical memory at `0x80000000`, 128MB. Devices at `0xa0000000`.
- **ysyxsoc**: SRAM `0x0f000000` (8KB), MROM `0x20000000` (4KB), PSRAM `0x80000000` (4MB), SDRAM `0xa0000000`, FLASH `0x30000000` (16MB). Uses `scripts/platform/ysyxsoc/linker.ld` with `MEMORY` regions (NOT the flat `scripts/linker.ld`). Supports `USE_PSRAM=1` and `USE_SDRAM=1` for alternative link scripts.

**Platform scripts** (`scripts/platform/`): `nemu.mk`, `npc.mk`, `ysyxsoc.mk`, `qemu.mk`, `logisim.mk`. The ysyxsoc platform file is the **preferred way** to set `RUN_CONFIG_*` vars — it properly sets trace paths and separates DTRACE/ETRACE logs.

### Platform Defaults Difference (critical):

| Config | npc.mk | ysyxsoc.mk |
|--------|--------|------------|
| `CONFIG_SDB_ENABLED` | `true` | `false` |
| `CONFIG_DIFFTEST` | `on` | `off` |
| `CONFIG_DEVICE` | `on` | `on` |
| `CONFIG_NVBOARD` | _not set_ | `on` |
| `CONFIG_MROM` | _not set_ | `off` |
| `CONFIG_DEBUG_OUTPUT` | `on` | `off` |
| DTRACE log path | shared dir, separate file | `trace-logs/dtrace.log` |
| ETRACE log path | shared dir, separate file | `trace-logs/etrace.log` |

**npc.mk gotcha**: Uses `RUN_CONFIG_ELF_FILE_PATH` (line 59) — a variable that **NPC's Makefile does NOT define or map**. This is silently ignored by NPC, so ftrace won't work with `ARCH=riscv32e-npc`. Use `ARCH=riscv32e-ysyxsoc` instead.

```bash
# Build + run a kernel
cd am-kernels/kernels/hello
make ARCH=riscv64-nemu run

# Run all CPU instruction tests
cd am-kernels/tests/cpu-tests
make ARCH=riscv32e-ysyxsoc run   # Preferred: runs on NPC via ysyxsoc platform
```

**Kernel build pattern** (3-line Makefile):
```makefile
NAME = hello
SRCS = hello.c
include $(AM_HOME)/Makefile
```

- **Cross-compiler**: `riscv64-unknown-linux-gnu-`. RV32E: `-march=rv32e_zicsr -mabi=ilp32e`. `riscv32e-nemu` uses `-march=rv32em_zicsr` (M extension included).
- **Halt mechanism**: AM calls `nemu_trap(code)` → RISC-V: `mv a0, code; ebreak`. NEMU detects ebreak and exits.
- **AM_HOME validation**: Checks `$(AM_HOME)/am/include/am.h` exists.

## am-kernels — Test Programs

```
am-kernels/
├── kernels/       # Demo kernels (hello, snake, etc.)
├── tests/
│   ├── cpu-tests/ # ~40 instruction-level regression tests (flat .c files)
│   ├── alu-tests/
│   ├── am-tests/
│   └── klib-tests/
```

**How cpu-tests work**: Parent Makefile dynamically generates per-test Makefiles from `tests/*.c`, runs each, reports PASS/FAIL.

```bash
cd am-kernels/tests/cpu-tests
make ARCH=riscv32e-ysyxsoc run   # Build + run all tests on NPC
```

## NPC — RISC-V CPU (Your Processor)

**CRITICAL: NPC does NOT use an `ARCH` variable.** `TOPNAME` is hardcoded to `ysyxSoCFull`. No Kconfig, no DEFCONFIG, no menuconfig. All configuration is via Makefile variables with `RUN_CONFIG_*` prefix.

```bash
make -C npc                      # Build
make -C npc run                  # Build + run
make -C npc gdb                  # Debug under GDB
make -C npc chisel-gen           # Re-build Chisel RTL
make -C npc clean                # Remove build/
```

**Build flow**: `chisel-gen` (Mill → SystemVerilog in `vsrc/generated/`) → Verilator (→ C++ model) → g++ (→ `build/ysyxSoCFull`).

- **Source files**: Hand-written `vsrc/` (flat `.v`/`.sv`) + generated `vsrc/generated/` (**do NOT manually edit**) + `$(YSYXSOC_PATH)/perip/` + `$(YSYXSOC_PATH)/build/ysyxSoCFull.v`. C++ from `csrc/`.
- **C++26** (`-std=c++26`). gcc/g++ wrapped via `ccache`.
- **`-fsanitize=address` only in LDFLAGS**, not CXXFLAGS (partial ASan instrumentation).
- **`VERILATOR_HOME` and `NVBOARD_HOME` required.**

### Configuration Variables

All via Makefile `RUN_CONFIG_*` variables. The Makefile translates them to `NPC_CONFIG_*` env vars at runtime (exception: `RUN_SDB_ENABLED` → `NPC_SDB_ENABLED`, not `NPC_CONFIG_SDB_ENABLED`).

```bash
# On/off flags:
RUN_SDB_ENABLED ?= false          # Simple Debugger: si N, info r/w, x N EXPR, p EXPR, c, q
RUN_CONFIG_ITRACE ?= off          # Instruction trace → build/itrace.log
RUN_CONFIG_MTRACE ?= off          # Memory access trace
RUN_CONFIG_FTRACE ?= off          # Function call trace
RUN_CONFIG_DTRACE ?= off          # Device access trace
RUN_CONFIG_ETRACE ?= off          # Exception/interrupt trace
RUN_CONFIG_DIFFTEST ?= off        # Diff-test vs NEMU shared lib
RUN_CONFIG_DEVICE ?= off          # Device simulation (flash, mrom, psram)
RUN_CONFIG_WAVE ?= off            # FST waveform → build/sim.fst
RUN_CONFIG_NVBOARD ?= on          # NVBoard GUI (NOTE: defaults to ON, unlike other flags)
RUN_CONFIG_DEBUG_OUTPUT ?= off    # Debug output

# Path configs (defaults all under build/):
RUN_CONFIG_DIFFTEST_PORT ?= 12345
RUN_CONFIG_ITRACE_OUT_FILE_PATH ?= build/itrace.log
RUN_CONFIG_MTRACE_OUT_FILE_PATH ?= build/mtrace.log
RUN_CONFIG_FTRACE_OUT_FILE_PATH ?= build/ftrace.log
RUN_CONFIG_DTRACE_OUT_FILE_PATH ?= build/dtrace.log
RUN_CONFIG_ETRACE_OUT_FILE_PATH ?= build/dtrace.log    # BUG: same as DTRACE
RUN_CONFIG_FLASH_BIN_FILE_PATH ?= build/flash.bin
RUN_CONFIG_FLASH_ELF_FILE_PATH ?= build/flash.elf
RUN_CONFIG_MROM_BIN_FILE_PATH ?= build/mrom.bin
RUN_CONFIG_MROM_ELF_FILE_PATH ?= build/mrom.elf        # BUG: never consumed by C++
RUN_CONFIG_DIFFTEST_SO_FILE_PATH ?= build/riscv32-nemu-interpreter-so
RUN_CONFIG_WAVE_FILE_PATH ?= build/sim.fst

# Usage:
make -C npc run RUN_CONFIG_DIFFTEST=on RUN_CONFIG_ITRACE=on RUN_CONFIG_WAVE=on RUN_CONFIG_DEVICE=on

# Run binary directly (use NPC_CONFIG_*):
NPC_CONFIG_DIFFTEST=on NPC_CONFIG_ITRACE=on ./build/ysyxSoCFull
```

### NPC Known Bugs (as of current codebase)

These are verified in `npc/Makefile`, `npc/csrc/main.cpp`, and `npc/include/utils.hpp`. **Do not assume these are fixed.**

1. **`NPC_CONFIG_MROM` (on/off toggle) has no `RUN_CONFIG_MROM` mapping.** Neither the `RUN_CONFIG_MROM` variable nor its mapping to `NPC_CONFIG_MROM` exists in the Makefile. Every other on/off flag maps `RUN_CONFIG_*` → `NPC_CONFIG_*` via `RUN_ARGS`, but MROM doesn't. Must set directly on the binary: `NPC_CONFIG_MROM=on ./build/ysyxSoCFull`.

2. **GDB_ARGS is missing `NPC_CONFIG_FLASH_ELF_FILE_PATH`** — RUN_ARGS includes it (line 161), GDB_ARGS (line 188→189) jumps from `FLASH_BIN_FILE_PATH` to `MROM_BIN_FILE_PATH`. `make gdb` with ftrace fails silently.

3. **ETRACE defaults to `build/dtrace.log`** (same as DTRACE). Both traces share one file when using NPC Makefile defaults. Running both without overriding paths produces interleaved/corrupted output. **Note**: C++ default in `utils.hpp` is `build/etrace.log` (correctly separate), but the Makefile always overrides it to `dtrace.log`. The ysyxsoc platform properly separates them to `trace-logs/dtrace.log` and `trace-logs/etrace.log`.

4. **`checkRequiredConfig()` runs unconditionally** — FLASH paths are validated even with `DEVICE=off`. Must always provide flash paths or the binary refuses to start.

5. **`checkRequiredConfig()` error messages are wrong** — flash path errors say "MROM" not "FLASH" (`csrc/main.cpp`, ~lines 180-186). Checks `config_flashBinFilePath` but prints `NPC_CONFIG_MROM_BIN_FILE_PATH`.

6. **`checkRequiredConfig()` only validates FLASH, not MROM** — missing MROM path goes undetected, crashes deeper in code.

7. **`NPC_CONFIG_MROM_ELF_FILE_PATH` is passed but never read** — `SimConfig` struct has no `config_mromElfFilePath` field (only `config_mromBinFilePath`). The env var is set by both RUN_ARGS and GDB_ARGS but `loadConfig()` never reads it.

### NPC Gotchas

- **`vsrc/generated/` is auto-generated** — never manually edit.
- **NVBoard is integrated and functional.** Pin bindings exist in `constr/ysyxSoCFull.nxdc` (16 LEDs, 16 switches, 8× 7-segment displays). NVBoard activates when `DEVICE=on` (NVBOARD defaults to `on`). Called via `device_update()` → `nvboard_update()` in `csrc/sim.cpp:194`. Exits properly via `nvboard_quit()` on shutdown.
- **DiffTest**: Loads NEMU as `.so` via `dlopen()`, compares GPRs + PC + CSRs per-instruction. Build NEMU with `TARGET_SHARE` first.
- **NPC internal memory map**: MROM `0x20000000` (4KB), FLASH `0x30000000` (16MB), PSRAM `0x80000000` (4MB).

### Flash Binary Workflow (Testing on NPC)

```bash
# Build test kernel for ysyxSoC-based NPC:
cd am-kernels/tests/cpu-tests
make ARCH=riscv32e-ysyxsoc

# Copy binary and run:
cp build/riscv32e-ysyxsoc/add.bin ../npc/build/flash.bin
cd ../npc
make run RUN_CONFIG_DIFFTEST=on RUN_CONFIG_ITRACE=on RUN_CONFIG_DEVICE=on

# Or single-step via ysyxsoc platform (passes all RUN_CONFIG_* correctly):
cd am-kernels/tests/cpu-tests
make ARCH=riscv32e-ysyxsoc run
```

## NVBoard — Virtual FPGA Board

- Library consumed by NPC via `include $(NVBOARD_HOME)/scripts/nvboard.mk`. Builds a static archive linked into the NPC binary.
- **No RST pin** — reset RTL via Verilator wrapper, not through a board pin.
- Pin bindings generated by `auto_pin_bind.py` from `.nxdc` constraint file. Current bindings in `npc/constr/ysyxSoCFull.nxdc` include GPIO LED output, switch input, and 8 seven-segment displays.
- NVBoard is compiled by its own pattern rule in `nvboard.mk` (not NPC's CXXFLAGS path), with SDL2 flags added directly.

## ysyxSoC — System-on-Chip Integration

```bash
make -C ysyxSoC dev-init   # Init submodules (rocket-chip) + apply rocket-chip patch
make -C ysyxSoC verilog    # Elaborate → build/ysyxSoCFull.v
make -C ysyxSoC clean
```

`verilog` flow: runs Mill (`ysyx.Elaborate` target) → replaces firtool with patched version 1.105.0 → applies `sed` transformations:
- Renames AXI4 channel signals: `s/_\(aw\|ar\|w\|r\|b\)_\(\|bits_\)/_\1/g`
- Strips `firrtl_black_box_resource_files.f` section

- **CPU BlackBox**: `src/CPU.scala` wraps student's Verilog as `ysyx_25070190 extends BlackBox`. Verilog must be `ysyx_25070190.v`. Interface spec at `spec/cpu-interface.md` — AXI4 master + slave + clock, reset, interrupt.
- **Peripherals** (`perip/`): UART 16550, SPI+XIP flash, SPI, GPIO, PS/2, VGA, SDRAM, PSRAM — plain Verilog, Chisel BlackBoxes in `src/device/`.
- **Firtool version pinned at 1.105.0** — the build patches in a specific firtool binary via `patch/update-firtool.sh`.
- **`dev-init` only needs to be run once**: initializes submodules and applies `patch/rocket-chip.patch`.

## fceux-am — NES Emulator

Independent git repo (not a submodule). Build: `make -C fceux-am ARCH=native run mainargs=mario`. ROMs in `nes/rom/`, pre-generated C arrays in `nes/gen/`.

## rt-thread — RT-Thread RTOS

Submodule (srcres258/rt-thread-am). AM BSP at `bsp/abstract-machine/`. Build: `make ARCH=riscv64-nemu init && make ARCH=riscv64-nemu run`.

## Top-Level Conventions

- **`.gitignore` is whitelist-based** — ignores `*.*` and `*`, whitelists specific entries via `!` patterns. Adding new root-level files requires updating `.gitignore`.
- **Top-level `Makefile` is the tracer, NOT a build system** — captures `make run` invocations into git branch `tracer-ysyx`. Always `make -C <subproject>`. Running `make` in root prints: "Please run 'make' under subprojects."
- **Student ID**: `ysyx_25070190` (root `Makefile`, `ysyxSoC/src/CPU.scala`).
- **No CI/CD** configured.
- **IDE support**: `.vscode/` has VS Code settings (C++ Runner, file associations); `.metals/` has Scala Metals LSP.
- **`.sisyphus/plans/`** contains 5 existing implementation plans: GPIO/NVBoard water light, dip switch status, SDRAM expansion (32-bit, word extension), and SDRAM bootloader. Reference these before working on related features.
- **Utility scripts**:
  | Script | Purpose |
  |--------|---------|
  | `nvboard/scripts/auto_pin_bind.py` | Generate C++ pin bindings from `.nxdc` |
  | `abstract-machine/tools/insert-arg.py` | Patch `mainargs` into AM binary |
  | `fceux-am/nes/build-roms.py` | Generate C arrays from NES ROMs |

## Common Pitfalls for Agents

- **Don't add `ARCH=` to NPC commands** — NPC doesn't use ARCH.
- **Don't guess NPC config var names** — they are `RUN_CONFIG_*` in Makefile, `NPC_CONFIG_*` as env vars.
- **Don't edit `vsrc/generated/`** — auto-generated from Chisel.
- **Don't forget `VERILATOR_HOME`** — required for NPC build.
- **NEMU for difftest must use `TARGET_SHARE`** in Kconfig, not `TARGET_AM`.
- **Use `riscv32e-ysyxsoc` not `riscv32e-npc`** when building kernels for ysyxSoC-based NPC. The `npc` platform passes `RUN_CONFIG_ELF_FILE_PATH` which NPC ignores, so ftrace is broken there.
- **`NPC_CONFIG_MROM` cannot be set via Makefile** — use binary directly or set env var.
- **Submodules are separate repos** — check with `git submodule status`, not top-level `git log`.
- **gitignore is whitelist-based** — new root files need explicit whitelisting.
- **`RUN_CONFIG_NVBOARD` defaults to `on`** — if you want no GUI, explicitly set `RUN_CONFIG_NVBOARD=off`.
- **When adding new `.nxdc` pin bindings**, the RTL side signal names must match what `auto_pin_bind.py` generates for. NVBoard pin names are defined in `nvboard/board/`.
