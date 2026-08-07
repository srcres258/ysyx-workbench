# AGENTS.md — ysyx-workbench

`ysyx-workbench` wraps NEMU, AbstractMachine, NPC, ysyxSoC, rt-thread, am-kernels, NVBoard, fceux-am, and yosys-sta.

## Start here

- Run `nix develop` first; its shellHook exports `NEMU_HOME`, `AM_HOME`, `NPC_HOME`, `NVBOARD_HOME`, `YSYX_HOME`, `VERILATOR_HOME`, and `YOSYS_STA_HOME`.
- `config.fish` only sets `NVBOARD_HOME`, `AM_HOME`, and `NPC_HOME`; it is not a full bootstrap path.
- `README.md` still points to `bash init.sh <subproject>`; `init.sh` is legacy/bootstrap only and may write env vars to `~/.bashrc`.
- The root `Makefile` is a tracer only (auto-commits to `tracer-ysyx` branch). Build with `make -C <subproject>`, not at repo root. There is no repo-wide lint, typecheck, or CI workflow.
- NEMU build/run/gdb and NPC run/gdb auto-commit on the `tracer-ysyx` branch; expect git history churn. NEMU commits "compile NEMU" on build and "run NEMU"/"gdb NEMU" on run/gdb; NPC commits "sim RTL" on run/gdb only (NPC binary build does not auto-commit).
- Root `.gitignore` is whitelist-based (`*.*` and `*` ignore everything, then `!` patterns re-include), so new root files usually need an explicit `.gitignore` entry.
- No OpenCode, Cursor, or Copilot config files exist — `AGENTS.md` is the sole agent-facing instruction file.
- Check `.sisyphus/plans/` before related feature work (35 plans covering NPC features, peripherals, synthesis tooling, memory/boot, RT-Thread integration, GPIO/PS2/VGA, and ChipLink).

## Boundaries

- Submodules: `am-kernels/`, `npc/vsrc-chisel/`, `rt-thread/`, `ysyxSoC/`, `standard/`, `yosys-sta/`.
- `fceux-am/` is a separate repo (listed in `.gitignore`, not tracked by this repo's git). It exists locally as a clone; glob/file-search tools may miss it due to gitignore exclusions.
- `nemu/`, `abstract-machine/`, `npc/`, and `nvboard/` are tracked directly in this repo. Note: `abstract-machine/`, `nvboard/`, and `nemu/` each have their own `.gitignore` — the root whitelist patterns do NOT control files inside these directories.
- `.gitmodules` has a bad `standard` entry (`path = rt-thread` instead of `path = standard`) and `fceux-am` has no `.gitmodules` entry at all — either will cause `git submodule status --recursive` to fail. `npc-frontend` is NOT in `.gitmodules` (stale `.gitignore` whitelist entry and orphaned metadata in `.git/modules/npc-frontend/` only).
- Do not edit generated output directly: `npc/vsrc/generated/`, `npc/build/`, `nemu/build/`, `ysyxSoC/build/`.

## Commands that matter

### NEMU

```
make -C nemu menuconfig|savedefconfig|%defconfig|run|gdb|clean|clean-all|distclean
```

- `run` expects `build/program.elf` (override with `IMG=<path>`).
- When `CONFIG_TARGET_AM=y` (menuconfig), NEMU switches to AM integration mode — `run`/`gdb` targets from `scripts/native.mk` are disabled, replaced by `$(AM_HOME)/Makefile` targets. Use defconfigs in `configs/` (`*-am_defconfig`) to set this.
- **ENGINE**: Only `interpreter` exists (no JIT or other options). The Kconfig choice (line 35–48) defines only `ENGINE_INTERPRETER`. Passing `ENGINE=interpreter` is the default and sole valid value.
- **GUEST_ISA shortcut**: You can override from the command line without menuconfig: `make -C nemu GUEST_ISA=riscv64`. Valid values: `riscv32`, `riscv64`, `x86`, `mips32`, `loongarch32r`. Note: `riscv64` reuses `src/isa/riscv32/` source directory (no separate riscv64 dir). The C code in `riscv32/` handles 64-bit mode via `MUXDEF(CONFIG_RV64, ...)`.
- **GUEST_ISA=riscv64 caveat**: `src/isa/filelist.mk` resolves source directories as `src/isa/$(GUEST_ISA)`, and no `src/isa/riscv64/` exists. When using `GUEST_ISA=riscv64` from CLI, ensure the menuconfig-managed `.config` has been generated first (e.g., via `riscv64-am_defconfig`) — the CLId `GUEST_ISA` override affects the `-D__GUEST_ISA__` compiler flag and artifact naming, but source-file discovery still depends on the directories present on disk.
- **Clean scope**: `clean` removes `build/` only; `distclean` removes `build/` + `.config` + `include/generated/` + `include/config/`; `clean-all` does both + cleans all tools subdirectories.
- **Difftest (NEMU as REF)**: set `TARGET_SHARE=y` in menuconfig → builds NEMU as a shared object (`.so`) with devices disabled. Build the SO: `make -C nemu GUEST_ISA=riscv32 SHARE=1 ENGINE=interpreter`. Output: `build/riscv32-nemu-interpreter-so`. NPC consumes this via `RUN_CONFIG_DIFFTEST_SO_FILE_PATH`. Note: `SHARE=1` passed on CLI triggers the `.so` build flags but does NOT disable devices (only `TARGET_SHARE=y` in menuconfig does); for a full difftest REF build, always use menuconfig.
- **Difftest (NEMU as DUT)**: NEMU can also compare against external refs — QEMU (any ISA), Spike (riscv only), KVM (x86 only). Enable via menuconfig (`CONFIG_DIFFTEST=y`, pick ref design); the ref SO auto-builds on first `make run`.

### AbstractMachine / am-kernels

```
# Primary regression (35 cpu-tests):
make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run
# Other test suites:
make -C am-kernels/tests/am-tests ARCH=riscv32e-ysyxsoc run
make -C am-kernels/tests/alu-tests ARCH=riscv32e-ysyxsoc run
make -C am-kernels/tests/klib-tests ARCH=riscv32e-ysyxsoc run
# Microbench (used by NPC perf):
make -C am-kernels/benchmarks/microbench ARCH=riscv32e-ysyxsoc
```

- Use `ARCH=riscv32e-ysyxsoc` for the SoC flow. `ARCH` defaults to `native` in rt-thread but has no default in abstract-machine itself.
- In rt-thread's `bsp/abstract-machine/`, `USE_SDRAM` and `USE_PSRAM` select linker scripts: `USE_SDRAM=1` → 32MB at `0xa0000000`; `USE_PSRAM=1` (auto-enabled on ysyxsoc when SDRAM is off) → 4MB at `0x80000000`.

### NPC

```
make -C npc [run|gdb|default|chisel-gen|chisel-gen-standalone|gen_header|
            synth|synth-search|synth-clean|perf|perf-clean|chisel-clean|clean|
            synth-exp-a|synth-exp-b|synth-exp-c|synth-exp-d|synth-exp-all|synth-flow-diff|
            mtrace|locality|locality-report|test-locality]
```

- **Select program**: Use `IMG=<path>` to specify the binary/ELF to run (e.g., `make -C npc run IMG=build/flash.bin`).
- **Sim mode**: `RUN_CONFIG_SIM_MODE` (default `ysyxsoc`). `standalone` uses bare `ysyx_25070190` without SoC peripherals; `ysyxsoc` uses `ysyxSoCFull` with NVBoard.
- **Make-level `RUN_CONFIG_*` vars**: `RUN_CONFIG_SIM_MODE` and `RUN_CONFIG_DPI=on` (default) are processed at Make level. All other `RUN_CONFIG_*` vars are passed as `NPC_CONFIG_*` env vars (default `off` unless noted):
  - `RUN_CONFIG_NVBOARD=on` (default), `RUN_CONFIG_PERF=on` (default)
  - `RUN_CONFIG_DIFFTEST`, `RUN_CONFIG_WAVE`, `RUN_CONFIG_ITRACE`/`MTRACE`/`FTRACE`/`DTRACE`/`ETRACE`
  - `RUN_CONFIG_TUI`, `RUN_CONFIG_DEVICE`, `RUN_CONFIG_MROM`, `RUN_CONFIG_DEBUG_OUTPUT`
  - Path overrides: `RUN_CONFIG_WAVE_FILE_PATH` (default `build/sim.fst`), `RUN_CONFIG_DIFFTEST_SO_FILE_PATH`, `RUN_CONFIG_FLASH_BIN_FILE_PATH`, etc.
- **C++ and ASAN**: Simulator builds with `-std=c++26` and links with `-fsanitize=address` (LDFLAGS only; CXXFLAGS does NOT include `-fsanitize=address`, so ASAN instrumentation may be incomplete). Runs with `ASAN_OPTIONS=detect_leaks=0:exitcode=0`. For NPC code changes, code must compile under C++26.
- `chisel-gen` rebuilds `npc/vsrc-chisel/` → `npc/vsrc/generated/`. It is an **automatic prerequisite** of Verilator builds — running `make` or `make run` triggers it. Use `chisel-gen-standalone` for standalone mode RTL (forces standalone regardless of `RUN_CONFIG_SIM_MODE`); use `chisel-clean` to force regeneration.
- **Note**: `gdb` and `chisel-gen-standalone` are valid targets but are NOT listed in `.PHONY`. If files with those names exist in the npc/ directory, `make` will skip them.
- `clean` removes `build/` + `vsrc/generated/` + `vsrc-chisel/generated/`, not just build artifacts.
- `synth` runs ASIC synthesis + STA via yosys-sta at 100 MHz. **It auto-rebuilds DPI-free RTL** (chisel-clean → chisel-gen with DPI off) before running STA to reflect the physical netlist boundary. Key synth knobs:
  - `SYNTH_CLK_MHZ` (default 100), `SYNTH_QOR_VIEW` (default `canonical_flat`), `SYNTH_AREA_BUDGET_UM2` (default 23000)
  - `YOSYS_STA_AUTO_INIT=on` (default, auto-bootstraps on first synth), `SYNTH_EXPERIMENT` (empty=canonical)
- `synth-search` does binary frequency search (1–500 MHz). Output: `build/synth/synth_summary.json`.
- `synth-exp-a|b|c|d` run controlled synthesis experiments; `synth-exp-all` runs all four; `synth-flow-diff` generates a comparison report.
- `perf` orchestrates the full profiling pipeline: synth → build microbench → simulate with all noisy knobs disabled → aggregate `perf.json` + `synth_summary.json`. Use `PERF_CHECK_STRICT=on` to fail on counter contract violations.
- **perf target quirk**: The `perf` target sets `NPC_CONFIG_*` env vars directly, bypassing the `RUN_CONFIG_*` → `NPC_CONFIG_*` translation — passing `RUN_CONFIG_*=X` on the `make perf` command line has **no effect**. Override via `NPC_CONFIG_*=X` instead.
- `mtrace` runs simulation with memory-trace output; `locality` runs cache locality analysis on the resulting JSONL traces; `locality-report` combines both in one step; `test-locality` runs the locality analyzer's unit tests.
- `default` builds the simulator binary without running; `gen_header` generates C++ Verilator headers for custom testbench development.

### ysyxSoC

```
make -C ysyxSoC dev-init   # once per clone: submodule init + rocket-chip patch
make -C ysyxSoC verilog     # generate build/ysyxSoCFull.v
make -C ysyxSoC clean
```

- `ysyxSoC/src/CPU.scala` wraps the student CPU as `ysyx_25070190`.
- Firtool is pinned to version **1.105.0** via `patch/update-firtool.sh`. If firtool updates upstream, this pin may need adjustment.
- `dev-init` runs `git submodule update --init --recursive` then applies `patch/rocket-chip.patch` (zeros AXI4 lock/cache/prot/qos bits that the student CPU doesn't implement).
- Build system: Mill **0.12.4**, Chisel **7.0.0-M2**, Scala **2.13.14**. Entry point: `mill -i ysyxsoc.runMain ysyx.Elaborate` → `build/ysyxSoCTop.sv` → sed post-processing → `build/ysyxSoCFull.v`.
- `ready-to-run/D-stage/` contains a pre-built reference SoC verilog; note that the D-stage CPU interface uses **SimpleBus** (not AXI4) and differs from the current SoC spec.

### fceux-am

```
make -C fceux-am ARCH=native run mainargs=mario
make -C fceux-am rom
```

- `ARCH=riscv32e-ysyxsoc` filters out emu2413 and VRC7 audio sources.

### yosys-sta

```
make -C yosys-sta              # setup (auto-bootstraps on first npc synth if YOSYS_STA_AUTO_INIT=on)
```

- The default target is `init` (downloads iEDA binary + icsprout55 PDK). The `sta` target runs synthesis + STA for the example GCD design. Use `DESIGN=mydesign SDC_FILE=/path/to/my.sdc RTL_FILES="/path/to/*.v"` to evaluate other designs.
- Firtool version pin (1.105.0) is in `ysyxSoC/patch/update-firtool.sh` lines 61, 72. NPC synth invokes yosys-sta via `npc/scripts/synth.sh`.

## Verification

- Prefer the narrowest relevant build/test command; there is no repo-wide lint/typecheck workflow.
- **Primary regression**: `make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run` (auto-discovers 35 tests, runs all, reports PASS/FAIL).
- **Difftest flow**: (1) `make -C nemu menuconfig` → enable `TARGET_SHARE=y` → (2) `make -C nemu GUEST_ISA=riscv32 SHARE=1 ENGINE=interpreter` → (3) `make -C npc run RUN_CONFIG_DIFFTEST=on`.
- **Waveform**: `make -C npc run RUN_CONFIG_WAVE=on` produces `build/sim.fst` (FST format via Verilator `--trace-fst`).
- **Performance regression**: `make -C npc perf` (builds microbench with `USE_SDRAM=1`, simulates, aggregates).
- **NPC tooling tests**: `python3 -m pytest npc/scripts/tests/` runs pytest-based unit tests for the synth/perf/equiv checker tooling.
