# AGENTS.md — ysyx-workbench

"一生一芯" (One Student One Chip) educational RISC-V processor design project. A meta-repo that ties together a CPU emulator, an AbstractMachine runtime, a Chisel-based SoC, and a Verilator-based processor simulation.

## Quick Start — Environment

```bash
# Activate nix dev shell (provides verilator, gtkwave, circt, gcc, make, SDL2, etc.)
nix develop
# ↑ This also sets all env vars (NEMU_HOME, AM_HOME, NPC_HOME, etc.) via shellHook.

# Or manually (fish shell; note: config.fish is INCOMPLETE — missing NEMU_HOME, YSYX_HOME, VERILATOR_HOME; also uses bash-style `$(pwd)` instead of fish-style `(pwd)`):
export NEMU_HOME=$(pwd)/nemu
export AM_HOME=$(pwd)/abstract-machine
export NPC_HOME=$(pwd)/npc
export NVBOARD_HOME=$(pwd)/nvboard
export YSYX_HOME=$(pwd)
export VERILATOR_HOME=$(nix-shell -p verilator --run 'echo $out')/share/verilator
```

Dependencies managed via `flake.nix`: verilator, gtkwave, circt, iverilog, SDL2 (+ image + ttf), SDL3 (+ image + ttf), capstone, libelf, libz, gcc, gnumake, pkg-config. The shellHook also sets `PKG_CONFIG_PATH` and `LD_LIBRARY_PATH`.

## Repository Architecture

This is a **meta-repo with 3 git submodules** (not tracked in the top-level repo directly):

| Submodule | Remote (fork) | Upstream | Branch |
|-----------|--------------|----------|--------|
| `am-kernels/` | srcres258/ysyx-am-kernels | NJU-ProjectN/am-kernels | master |
| `npc/vsrc-chisel/` | srcres258/chisel-ysyx-cpu | — | npc-b2 |
| `ysyxSoC/` | srcres258/ysyx-ysyxSoC | OSCPU/ysyxSoC | ysyx6-own |

The rest (`nemu/`, `abstract-machine/`, `npc/`, `nvboard/`) are direct directories, initialized via `init.sh`.

## Subproject Map

| Directory | Purpose | Languages | Build Tool |
|-----------|---------|-----------|------------|
| `nemu/` | ISA simulator (golden reference) | C | GNU Make + Kconfig |
| `abstract-machine/` | Bare-metal runtime (AM) | C, asm | GNU Make |
| `am-kernels/` | Test kernels running on AM | C | GNU Make (via AM) |
| `npc/` | RISC-V CPU (Verilator simulation) | SystemVerilog, C++ | GNU Make + Verilator |
| `npc/vsrc-chisel/` | Chisel frontend for CPU generation | Chisel/Scala | Mill |
| `nvboard/` | Virtual FPGA board (SDL2 GUI) | C++, Python | GNU Make (library) |
| `ysyxSoC/` | SoC integration (wraps CPU + periphs) | Chisel/Scala, Verilog | Mill + firtool |
| `series/` | Lecture experiment materials | — | — |
| `docs/` | Documentation (RV32I instruction reference) | — | — |

## NEMU — ISA Simulator (Golden Reference)

License: Mulan PSL v2. Not BSD 3-Clause.

```bash
# Configure (pick ISA, engine, devices)
make -C nemu menuconfig
# Or use preset (these build NEMU as AM application — requires AM_HOME):
make -C nemu riscv32-am_defconfig
make -C nemu riscv64-am_defconfig

# Build
make -C nemu

# Run (needs build/program.elf — symlink or copy your kernel ELF)
make -C nemu run

# Run with disk image:
IMG=path/to/disk.img make -C nemu run

# Debug under GDB
make -C nemu gdb

# Clean
make -C nemu clean        # clean object files
make -C nemu distclean     # clean + remove .config
make -C nemu savedefconfig  # save current config as defconfig
```

- **Config system**: Linux-kernel-style Kconfig (`menuconfig` → `.config` → `include/config/auto.conf` + `include/generated/autoconf.h`). The Makefile extracts `CONFIG_*` variables.
- **Build targets (Kconfig)**:
  - `TARGET_NATIVE_ELF` (default) — standalone Linux executable, linked with `-lreadline -ldl -lelf`
  - `TARGET_SHARE` — shared object `.so` for NPC difftest (build with `TARGET_SHARE=y` in config)
  - `TARGET_AM` — build NEMU as an AM application (used by `*_am_defconfig`)
- **Binary naming**: `build/{ISA}-nemu-{engine}` (e.g., `build/riscv32-nemu-interpreter`)
- **Source discovery**: NOT a flat list. Each subdirectory has a `filelist.mk` with `DIRS-y`/`SRCS-y`. The Makefile auto-discovers all `filelist.mk` files recursively.
- **`NEMU_HOME` required**: Sanity-checks `$(NEMU_HOME)/src/nemu-main.c` exists. Set it or builds fail.
- **`build/program.elf` required for `make run`**: Usually a symlink to your kernel ELF. Path is `--elf=$(BUILD_DIR)/program.elf`.
- **DiffTest shared lib**: For NPC difftest, NEMU must be built as a `.so` via `TARGET_SHARE` in Kconfig. NPC expects the file at `build/riscv32-nemu-interpreter-so` by default.
- **`.gitignore`**: Whitelist-based — ignores everything then whitelists source patterns. `build/` is tracked, not ignored.
- **Supported ISAs**: x86, mips32, riscv32, riscv64 (select riscv ISA + enable RV64), loongarch32r.
- **Debugging tools** in `tools/`: `spike-diff/`, `qemu-diff/`, `kvm-diff/` (differential testing helpers), `kconfig/` (build-time), `fixdep/`, `gen-expr/`.

## Abstract Machine (AM) — Bare-Metal Runtime

**Five abstraction layers** (increasing power):

| Layer | Purpose | Example API |
|-------|---------|-------------|
| TRM | Bare execution, memory, basic I/O | `putch()`, `halt()`, heap |
| IOE | Device abstraction | `ioe_read()`, `ioe_write()` — devices: UART, timer, keyboard, VGA, audio |
| CTE | Interrupt/exception, context switch | `cte_init()`, `yield()`, `kcontext()` |
| VME | Virtual memory, page tables | `map()`, `protect()`, `ucontext()` — RISC-V Sv39/Sv48 |
| MPE | Multi-processor | `cpu_count()`, `atomic_xchg()` |

**ARCH naming**: `{ISA}-{PLATFORM}` — the Makefile auto-discovers available ARCH values from `scripts/*.mk`. Add a new arch by creating `scripts/{ISA}-{PLATFORM}.mk`.

**Full supported ARCH list** (from `scripts/`):
`riscv32-nemu`, `riscv32e-nemu`, `riscv64-nemu`, `riscv32e-npc`, `riscv32e-ysyxsoc`, `minirv-npc`, `minirv-nemu`, `minirv-logisim`, `mips32-nemu`, `x86-nemu`, `x86-qemu`, `x86_64-qemu`, `loongarch32r-nemu`, `spike`, `native`

**Key gotcha**: `ARCH=riscv32e-npc` and `ARCH=riscv32e-ysyxsoc` are different. Use `riscv32e-ysyxsoc` for the ysyxSoC-based NPC build (RV32E with AXI4 interface, `-march=rv32e_zicsr -mabi=ilp32e`). Use `riscv32e-npc` for direct NPC build with plain memory interface.

**Platform scripts** (`scripts/platform/`): `nemu.mk`, `npc.mk`, `ysyxsoc.mk`, `qemu.mk`, `logisim.mk`

**Kernel build pattern** (3-line Makefile):
```makefile
NAME = hello
SRCS = hello.c
include $(AM_HOME)/Makefile
```

**Commands**:
```bash
# Build + run kernel on NEMU
cd am-kernels/kernels/hello
make ARCH=riscv64-nemu run
make ARCH=riscv64-nemu gdb

# Run CPU instruction tests (auto-generates per-test Makefiles from tests/ directory)
cd am-kernels/tests/cpu-tests
make ARCH=riscv64-nemu run

# Run on native Linux (for fast testing)
make ARCH=native run

# Run on NPC (your own CPU)
make ARCH=riscv32e-npc run
```

- **AM_HOME validation**: The Makefile checks `$(AM_HOME)/am/include/am.h` exists. Set AM_HOME or builds fail.
- **ARCH auto-validation**: Automatically checks `$(ARCH)` against the list of `scripts/*.mk` files.
- **Memory map (NEMU/NPC)**: Physical memory at `0x80000000`, 128MB. Devices at `0xa0000000` (serial `+0x3f8`, RTC `+0x48`, keyboard `+0x60`, VGA `+0x100`).
- **Memory map (ysyxsoc)**: Three-zone model — SRAM at `0x0f000000` (8KB), MROM at `0x20000000` (4KB), PSRAM at `0x80000000` (4MB), FLASH at `0x30000000` (16MB). Uses a completely different linker script at `scripts/platform/ysyxsoc/linker.ld` with `MEMORY` regions (not the flat `scripts/linker.ld`). Note the ysyxsoc platform file (`scripts/platform/ysyxsoc.mk`) is the preferred way to set `RUN_CONFIG_*` vars — it properly sets all trace paths and separates DTRACE/ETRACE logs unlike NPC's own Makefile defaults.
- **Halt mechanism**: AM calls `nemu_trap(code)` → on RISC-V: `mv a0, code; ebreak`. NEMU detects ebreak and exits.
- **Cross-compiler**: RISC-V uses `riscv64-unknown-linux-gnu-` prefix (set in `scripts/isa/riscv.mk`). For RV32E: `-march=rv32e_zicsr -mabi=ilp32e`. Note: `riscv32e-nemu` uses `-march=rv32em_zicsr` (includes M extension); minirv variants use a different compiler (`minirv-gcc`).
- **Linker script**: `scripts/linker.ld` — entry at `_pmem_start + _entry_offset`, stack 32KB after `.bss`, heap starts page-aligned after end. For the ysyxsoc platform, the linker script is `scripts/platform/ysyxsoc/linker.ld` — completely different `MEMORY`-based layout with separate flash/sram regions.

## am-kernels — Test Programs

```
am-kernels/
├── kernels/       # Demo/application kernels (hello, snake, demo, flash-test, etc.)
├── tests/
│   ├── cpu-tests/ # Instruction-level regression tests (~40 tests)
│   ├── alu-tests/ # ALU operation tests
│   ├── am-tests/  # AM runtime tests
│   └── klib-tests/# Library tests
```

**How cpu-tests work**: The parent Makefile dynamically generates per-test Makefiles from `tests/*.c` files, runs each test, and reports PASS/FAIL. Tests are NOT flat `.c` files in a single directory — each is in its own subdirectory under `tests/`.

```bash
# Run all cpu-tests on NEMU (auto-builds + runs each test):
cd am-kernels/tests/cpu-tests
make ARCH=riscv32e-nemu run

# Run all cpu-tests on NPC:
make ARCH=riscv32e-npc run
```

## NPC — RISC-V CPU (Your Processor)

```bash
# Build
make -C npc

# Build + run
make -C npc run

# Debug under GDB
make -C npc gdb

# Clean
make -C npc clean
```

**CRITICAL: NPC does NOT use an `ARCH` variable.** The `TOPNAME` is hardcoded to `ysyxSoCFull` (line 19 of Makefile). There is **no Kconfig, no DEFCONFIG, no menuconfig** in npc/. All configuration is via Makefile variables with `RUN_CONFIG_*` prefix.

- **Build flow**: 
  1. `chisel-gen` — Builds Chisel CPU frontend (`vsrc-chisel/` via Mill) → generates SystemVerilog into `vsrc/generated/`
  2. Verilator converts all Verilog/SystemVerilog → C++ model
  3. g++ compiles with C++ testbench → native binary at `build/ysyxSoCFull`
- **Source files**: Hand-written RTL from `vsrc/` (flat `.v`/`.sv`) + generated RTL from `vsrc/generated/` (auto-created by `chisel-gen`, do NOT manually edit) + Verilog peripherals from `$(YSYXSOC_PATH)/perip/` + the elaborated `$(YSYXSOC_PATH)/build/ysyxSoCFull.v`. C++ sources (`.cpp`) from `csrc/`.
- **C++ standard**: C++26 (`-std=c++26`).
- **Sanitizers**: Address sanitizer enabled (`-fsanitize=address` in LDFLAGS; NOT in CXXFLAGS).
- **`VERILATOR_HOME` required**: NPC uses `$(VERILATOR_HOME)/include/` and `$(VERILATOR_HOME)/include/vltstd/`.
- **`NVBOARD_HOME` required**: Included via `$(NVBOARD_HOME)/scripts/nvboard.mk`.

### Configuration Variables

All via Makefile variables (NOT env vars directly, though the Makefile translates them to `NPC_CONFIG_*` at runtime):

```bash
# Available configuration vars (defaults):
RUN_SDB_ENABLED ?= false           # Simple Debugger interactive prompt
RUN_CONFIG_ITRACE ?= off           # Instruction trace → build/itrace.log
RUN_CONFIG_MTRACE ?= off           # Memory access trace
RUN_CONFIG_FTRACE ?= off           # Function call trace
RUN_CONFIG_DTRACE ?= off           # Device access trace
RUN_CONFIG_ETRACE ?= off           # Exception/interrupt trace (⚠ default OUT path is build/dtrace.log — likely bug)
RUN_CONFIG_DIFFTEST ?= off         # Diff-test against NEMU
RUN_CONFIG_DEVICE ?= off           # Device simulation (flash, mrom, psram)
RUN_CONFIG_WAVE ?= off             # FST waveform output
RUN_CONFIG_DEBUG_OUTPUT ?= off     # Debug output
RUN_CONFIG_DIFFTEST_PORT ?= 12345  # Difftest port
RUN_CONFIG_FLASH_BIN_FILE_PATH ?= build/flash.bin
RUN_CONFIG_FLASH_ELF_FILE_PATH ?= build/flash.elf
RUN_CONFIG_MROM_BIN_FILE_PATH ?= build/mrom.bin
RUN_CONFIG_MROM_ELF_FILE_PATH ?= build/mrom.elf
RUN_CONFIG_DIFFTEST_SO_FILE_PATH ?= build/riscv32-nemu-interpreter-so
RUN_CONFIG_WAVE_FILE_PATH ?= build/sim.fst

# Usage:
make -C npc run \
  RUN_CONFIG_ITRACE=on \
  RUN_CONFIG_DIFFTEST=on \
  RUN_CONFIG_WAVE=on \
  RUN_CONFIG_DEVICE=on \
  RUN_CONFIG_FLASH_BIN_FILE_PATH=build/flash.bin
```

**If you need to set these as env vars for debugging directly on the binary**, use `NPC_CONFIG_*` (the Makefile translates `RUN_CONFIG_*` → `NPC_CONFIG_*` at runtime):
```bash
NPC_CONFIG_DIFFTEST=on NPC_CONFIG_ITRACE=on ./build/ysyxSoCFull
```

### NPC Known Bugs (as of current codebase)

1. **`checkRequiredConfig()` error messages are wrong** (`csrc/main.cpp`, lines 173–184): When flash paths are missing, error messages say "MROM" instead of "FLASH":
   ```
   "未指定 NPC_CONFIG_MROM_BIN_FILE_PATH" — actually checking config_flashBinFilePath
   "未指定 NPC_CONFIG_MROM_ELF_FILE_PATH" — actually checking config_flashElfFilePath
   ```
2. **`NPC_CONFIG_MROM` is parsed in C++** (`main.cpp` line 69) but **never passed via Makefile RUN_ARGS or GDB_ARGS**. There is no `RUN_CONFIG_MROM` variable at all. Setting `RUN_CONFIG_DEVICE=on` from Makefile does NOT enable MROM — must set env var `NPC_CONFIG_MROM=on` directly on the binary.
3. **GDB_ARGS is missing `NPC_CONFIG_FLASH_ELF_FILE_PATH`** — RUN_ARGS includes it (line 157), but GDB_ARGS (lines 183–187) jumps from `FLASH_BIN_FILE_PATH` directly to `MROM_BIN_FILE_PATH`, omitting `FLASH_ELF_FILE_PATH`. This means `make gdb` with ftrace or ELF-dependent features will fail silently.
4. **ETRACE default output path inconsistency**: The Makefile default for `RUN_CONFIG_ETRACE_OUT_FILE_PATH` is `build/dtrace.log` (line 132), the **same path** as `RUN_CONFIG_DTRACE_OUT_FILE_PATH` (line 131) — both traces share one file. Meanwhile the C++ default in `include/utils.hpp` (line 27) is `"build/etrace.log"`. The C++ and Makefile defaults disagree.
5. **`CONFIG_RVE`/`CONFIG_RV64` control GPR count** (16 vs 32) and word size in C++ code (in `include/macro-def.hpp` and `include/common.hpp`), but are NOT passed via CXXFLAGS in the Makefile. They come from the Verilator-generated C++ wrapper.

### NPC Gotchas

- **`NPC_CONFIG_MROM` cannot be set via Makefile command line** — it's parsed in `main.cpp` but never translated from any `RUN_CONFIG_*` variable. Use the binary directly: `NPC_CONFIG_MROM=on ./build/ysyxSoCFull`.
- **`-fsanitize=address` is only in LDFLAGS** (link step), NOT in `CXXFLAGS` (compile step). ASan instrumentation is partial.
- The `constr/ysyxSoCFull.nxdc` file contains **only `top=ysyxSoCFull`** — no pin bindings. NVBoard is compiled in but never called in the sim loop. To activate: add pin bindings to the `.nxdc`, then add `nvboard_init()`/`nvboard_update()` to `csrc/sim.cpp`.

- **SDB (Simple Debugger)**: Interactive prompt with `si N`, `info r`, `info w`, `x N EXPR`, `p EXPR`, `c`, `q`. Enable with `RUN_SDB_ENABLED=true`.
- **DiffTest**: Loads NEMU as `.so` via `dlopen()`, compares all GPRs + PC + CSRs per-instruction against reference. Build NEMU with `TARGET_SHARE` in Kconfig first.
- **Memory map (NPC internal)**:
  | Region | Address | Size | Backing |
  |--------|---------|------|---------|
  | MROM | `0x20000000` | 4KB | `build/mrom.bin` |
  | FLASH | `0x30000000` | 16MB | `build/flash.bin` |
  | PSRAM | `0x80000000` | 4MB | In-memory array |
- **NVBoard**: Linked at build level but NOT called in simulation loop yet. There are TWO `.nxdc` files:
  - `constr/ysyxSoCFull.nxdc` — placeholder (only `top=ysyxSoCFull`, no pin bindings)
  - `constr/top.nxdc` — working example with pin bindings (a,b→switches, f→LEDs)
  To activate NVBoard: add pin bindings to `ysyxSoCFull.nxdc`, then call `nvboard_init()`, `nvboard_update()` in `csrc/sim.cpp`.
- **Generated code**: `vsrc/generated/` contains auto-generated CPU RTL files — do NOT manually edit these. These are produced by the `chisel-gen` Makefile target which builds `npc/vsrc-chisel/` and copies output to `vsrc/generated/`.
- **Formal verification**: Assert/assume/cover properties in `vsrc/generated/verification/`.

### Flash Binary Workflow

The simplest way to get a test program into NPC:

```bash
# 1. Build a test kernel
cd am-kernels/tests/cpu-tests
make ARCH=riscv32e-ysyxsoc  # builds all tests

# 2. Copy the binary to NPC's expected location
cp build/riscv32e-ysyxsoc/xxx.bin ../npc/build/flash.bin

# 3. Run NPC with device support
cd ../npc
make run \
  RUN_CONFIG_DIFFTEST=on \
  RUN_CONFIG_ITRACE=on \
  RUN_CONFIG_DEVICE=on
```

**Preferred (single-step) method via ysyxsoc platform:**

```bash
# The ysyxsoc.mk platform file properly passes all RUN_CONFIG_* vars
# including correct DTRACE/ETRACE log paths:
cd am-kernels/tests/cpu-tests
make ARCH=riscv32e-ysyxsoc run
```

**Note**: Use `riscv32e-ysyxsoc` ARCH (AXI4 variant) NOT `riscv32e-npc` (plain memory variant) when running on the ysyxSoC-based NPC.

## NVBoard — Virtual FPGA Board

- SDL2-based GUI rendering a virtual FPGA board (LEDs, switches, 7-seg, VGA, keyboard, UART).
- **Library, not executable**: Consumed by NPC via `include $(NVBOARD_HOME)/scripts/nvboard.mk`.
- **Pin binding workflow**: `.nxdc` constraint file → `auto_pin_bind.py` → `auto_bind.cpp` (C++ with `nvboard_bind_all_pins()`). See `npc/constr/top.nxdc` for a working example.
- **API**: `nvboard_init()`, `nvboard_update()` (call every cycle), `nvboard_bind_pin(signal, len, pin_ids...)`, `nvboard_quit()`.
- **Key gotcha**: No RST pin in NVBoard pin list. Reset RTL via Verilator wrapper, NOT through a board pin. Re-run to reset NVBoard internal state.
- **Public headers**: `usr/include/` (for consumers). Internal: `include/` (for NVBoard's own `src/`).

## ysyxSoC — System-on-Chip Integration

- Wraps a student's Verilog CPU into a complete SoC with peripherals using Chisel/Scala.
- **Tools**: Mill build tool (`build.sc`), Scala 2.13.14, Chisel 7.0.0-M2, firtool 1.105.0.
- **Build**:
  ```bash
  make -C ysyxSoC dev-init    # Init submodules (rocket-chip) + apply patches
  make -C ysyxSoC verilog     # Elaborate → build/ysyxSoCFull.v
  ```
  `dev-init` does `git submodule update --init --recursive` then applies `patch/rocket-chip.patch`.
  `verilog` runs Mill to generate `build/ysyxSoCTop.sv`, then:
  1. Replaces firtool with a patched version (`patch/update-firtool.sh`)
  2. Renames `ysyxSoCTop.sv` → `ysyxSoCFull.v`
  3. Applies `sed` transformations to clean up generated Verilog (AXI signal renaming, removing black box resource lines)

- **CPU BlackBox**: `src/CPU.scala` wraps student's Verilog core as `class ysyx_25070190 extends BlackBox`. The Verilog file must be `ysyx_25070190.v` (matching the 8-digit student ID). Interface spec at `spec/cpu-interface.md` — requires AXI4 master + AXI4 slave ports + clock, reset, interrupt.
- **Peripherals** (`perip/`): UART 16550, SPI+XIP flash, SPI bus, GPIO, PS/2, VGA, SDRAM, PSRAM, AMBA (AXI infrastructure), bitrev (utility) — all plain Verilog, wrapped as Chisel BlackBoxes in `src/device/`.
- **Config flags** in `src/Top.scala`: `Config.hasChipLink`, `Config.sdramUseAXI`.
- **Pre-built D-stage**: `ready-to-run/D-stage/ysyxSoCFull.v` is a SimpleBus version for early-stage students.

## npc/vsrc-chisel — Chisel CPU Frontend

- Separate submodule for generating CPU RTL from Chisel/Scala.
- Uses Mill build tool (not sbt). Build with `mill -i ...`.
- Feeds generated RTL into `npc/vsrc/generated/` for Verilator simulation.

## Top-Level Conventions

- **`.gitignore` is whitelist-based**: Ignores `*.*` and `*`, then whitelists specific directories/files via `!` patterns. The whitelist includes legacy entries (`nexus-am/`, `nanos-lite/`, `navy-apps/`, `npc-chisel/`) from earlier project versions that no longer exist. `abstract-machine/` and `nvboard/` are NOT explicitly whitelisted — they were force-tracked via `init.sh`. Adding new files to the root requires updating `.gitignore`. Submodules (am-kernels, ysyxSoC, npc/vsrc-chisel) don't need gitignore entries.
- **Top-level `Makefile`** is NOT for building — it manages a git tracer branch for the course submission system. It captures every `make run` into a separate git branch (`tracer-ysyx`). NPC's Makefile and NEMU's `native.mk` both `include ../Makefile` for this. **Run `make` inside subprojects only.**
- **Student ID**: `ysyx_25070190` (in root `Makefile` and `ysyxSoC/src/CPU.scala`). Change in your fork with `sed`.
- **`init.sh`** is the initial setup script — clones missing subprojects from GitHub and sets env vars in `~/.bashrc`.
- **No CI/CD** configured in this repo.
- **LICENSE**: BSD 3-Clause (root). Note: NEMU is separately licensed under Mulan PSL v2.

## Testing/Verification Flow (NPC)

1. Write Verilog/SystemVerilog changes (`vsrc/generated/*.sv` or hand-written `vsrc/*.sv`)
2. Build AM kernel as test program:
   ```bash
   cd am-kernels/tests/cpu-tests
   make ARCH=riscv32e-ysyxsoc  # builds all tests; find binary in build/riscv32e-ysyxsoc/
   ```
3. Copy kernel binary to NPC:
   ```bash
   cp build/riscv32e-ysyxsoc/add.bin ../npc/build/flash.bin
   ```
4. Build and run NPC with difftest:
   ```bash
   cd ../npc
   make run \
     RUN_CONFIG_ITRACE=on \
     RUN_CONFIG_DIFFTEST=on \
     RUN_CONFIG_WAVE=on \
     RUN_CONFIG_DEVICE=on
   ```
5. Check `build/itrace.log` for instruction trace, `build/sim.fst` for waveform (gtkwave)
6. If difftest fails (DUT ≠ REF), diff GPRs/PC/CSRs are printed. Use SDB (`RUN_SDB_ENABLED=true`) to step through execution.

### Quick Iteration Shortcut

After first build, skip the full AM build:
```bash
# Re-run with same flash.bin after changing RTL:
make -C npc run RUN_CONFIG_DIFFTEST=on
# (only re-verilates changed RTL, re-links, and runs)
```

## Common Pitfalls for Agents

- **Don't guess at NPC config var names** — they are `RUN_CONFIG_*` in the Makefile, not `NPC_CONFIG_*`.
- **Don't add `ARCH=` to NPC commands** — NPC doesn't use ARCH. Only AM and NEMU use ARCH.
- **Don't edit `vsrc/generated/`** — these are auto-generated from Chisel.
- **Don't forget `VERILATOR_HOME`** — must be set or NPC build fails.
- **gitignore is whitelist-based** — new root-level files won't be tracked unless added to `.gitignore`.
- **Submodules are separate repos** — check them individually with `git submodule status`, not top-level `git log`.
- **`am-kernels` is a submodule** — cd into it to see the actual test structure.
- **NEMU for difftest must be built with `TARGET_SHARE`** in Kconfig, not `TARGET_AM`.
- **Use `riscv32e-ysyxsoc` not `riscv32e-npc`** as ARCH when building kernels for the ysyxSoC-based NPC.
- **Top-level `make` is the tracer**, not a build system — always `make -C <subproject>`.
- **Don't run `make` in root** — it prints "Please run 'make' under subprojects."
