# AGENTS.md — ysyx-workbench

`ysyx-workbench` wraps NEMU, AbstractMachine, NPC, ysyxSoC, rt-thread, am-kernels, NVBoard, fceux-am, and yosys-sta.

## Start here

- Run `nix develop` first; its shellHook exports `NEMU_HOME`, `AM_HOME`, `NPC_HOME`, `NVBOARD_HOME`, `YSYX_HOME`, `VERILATOR_HOME`, `YOSYS_STA_HOME` (plus `IEDA_BIN` on x86_64-linux).
- `config.fish` only sets `NVBOARD_HOME`, `AM_HOME`, and `NPC_HOME`; it is not a full bootstrap path.
- `README.md` still points to `bash init.sh <subproject>`; `init.sh` is legacy/bootstrap only and may write env vars to `~/.bashrc`.
- The root `Makefile` is a tracer only (auto-commits to `tracer-ysyx` branch). Build with `make -C <subproject>`, not at repo root. There is no repo-wide lint, typecheck, or CI workflow.
- NEMU build/run/gdb and NPC run/gdb auto-commit on the `tracer-ysyx` branch; expect git history churn. NEMU commits "compile NEMU" on build and "run NEMU"/"gdb NEMU" on run/gdb; NPC commits "sim RTL" on run/gdb only (NPC binary build does not auto-commit). Do not merge `tracer-ysyx` into your work branch.
- Work happens on feature branches (`pa1`, `npc-b1/b2/b3`, `refactor-1/2`, `rtt`, `frontend` exist); the active branch is usually the newest `npc-b*`. Check `git branch` before editing and never commit on `tracer-ysyx`.
- Root `.gitignore` is whitelist-based (`*.*` and `*` ignore everything, then `!` patterns re-include), so new root files usually need an explicit `.gitignore` entry. Stray junk (session exports, hs_err logs, gtkw files, mill launchers) accumulates at root and is silently ignored.
- No OpenCode, Cursor, or Copilot config files exist — `AGENTS.md` is the sole agent-facing instruction file.
- Check `.sisyphus/plans/` before related feature work (36 plans covering NPC features, peripherals, synthesis tooling, memory/boot, RT-Thread integration, GPIO/PS2/VGA, and ChipLink).

## Boundaries

- Submodules per `.gitmodules` (6): `am-kernels/`, `npc/vsrc-chisel/`, `rt-thread/`, `ysyxSoC/`, `fceux-am/`, `yosys-sta/`. There is NO `standard/` submodule. `fceux-am` and `yosys-sta/iEDA` are registered but currently uninitialized (`-` prefix in `git submodule status`); `fceux-am/` still has a live `.git` (manual clone). Nested submodules: `ysyxSoC/rocket-chip` (+ 6 deps), `yosys-sta/iEDA`.
- `nemu/`, `abstract-machine/`, `npc/`, and `nvboard/` are tracked directly in this repo. Each of these four has its own `.gitignore`, which takes precedence over the root whitelist inside those directories.
- `.gitmodules` is currently valid; `git submodule status --recursive` exits 0. Leftover orphaned metadata only: `npc-frontend`, `npc-chisel`, and `npc` under `.git/modules/`, stale `submodule.npc-chisel`/`submodule.npc/chisel` entries in `.git/config`, and stale `!/npc-frontend`, `!/nexus-am/*`, `!/nanos-lite/*`, `!/navy-apps/*` whitelist entries in the root `.gitignore`.
- Do not edit generated output directly: `npc/vsrc/generated/`, `npc/build/`, `nemu/build/`, `ysyxSoC/build/`.

## Commands that matter

### NEMU

```
make -C nemu menuconfig|savedefconfig|%defconfig|run|gdb|clean|clean-all|distclean
```

- `run` takes the image positionally via `IMG=<path>` (appended after the default args). The default `--elf=build/program.elf` only feeds **FTRACE symbol loading** (`load_elf()` in monitor.c) — it is NOT the loaded image. Without `IMG`, the default `nemu` machine boots a built-in default image; the `ysyxsoc` machine requires one.
- Machine profiles: `--machine=nemu|ysyxsoc` (default `nemu`). The machine layer is pluggable (`src/machine/machine.c` registry + `machine_select()`): `nemu.c` carries a built-in default image and `requires_image=false`, while `ysyxsoc/` (`machine.c`, `uart.c`, `timer.c`, `vga.c`) defines sram/mrom/flash/psram/sdram regions, requires an image, and asserts if none is provided.
- When `CONFIG_TARGET_AM=y` (menuconfig), NEMU switches to AM integration mode — `run`/`gdb`/`clean-all` from `scripts/native.mk` are replaced by `$(AM_HOME)/Makefile` targets. Use defconfigs in `configs/` (`*-am_defconfig`; only riscv32/riscv64/mips32 variants exist).
- **ENGINE**: Only `interpreter` exists (no JIT). The Kconfig choice (lines 35–48) defines only `ENGINE_INTERPRETER`. Passing `ENGINE=interpreter` is the default and sole valid value. `MODE_SYSTEM` is the only CPU mode (no user mode).
- **GUEST_ISA shortcut**: Override from the command line without menuconfig: `make -C nemu GUEST_ISA=riscv64`. Valid values: `riscv32`, `riscv64`, `x86`, `mips32`, `loongarch32r`. `riscv64` reuses `src/isa/riscv32/` (no separate dir); the C code handles 64-bit via `MUXDEF(CONFIG_RV64, ...)` and also supports `CONFIG_RVE` (E extension).
- **GUEST_ISA=riscv64 caveat**: `src/isa/filelist.mk` resolves sources as `src/isa/$(GUEST_ISA)`, and no `src/isa/riscv64/` exists. When overriding from CLI, generate the menuconfig-managed `.config` first (e.g., `riscv64-am_defconfig`) — the CLI override affects the `-D__GUEST_ISA__` flag and artifact naming, but source discovery still depends on the on-disk directories. Note `loongarch32r` is a full source tree under `src/isa/` (not just a config name).
- **Clean scope**: `clean` removes `build/` only; `distclean` removes `build/` + `.config` + `.config.old` + `include/generated/` + `include/config/`; `clean-all` does both + cleans all tools subdirectories.
- **Difftest (NEMU as REF)**: set `TARGET_SHARE=y` in menuconfig → builds NEMU as a shared object (`.so`) with devices disabled. Build the SO: `make -C nemu GUEST_ISA=riscv32 SHARE=1 ENGINE=interpreter`. Output: `build/riscv32-nemu-interpreter-so`. NPC consumes this via `RUN_CONFIG_DIFFTEST_SO_FILE_PATH`. Note: CLI `SHARE=1` triggers the `.so` build flags but does NOT disable devices (only `TARGET_SHARE=y` does, via `depends on !TARGET_SHARE`); for a full difftest REF build, always use menuconfig.
- **Difftest (NEMU as DUT)**: NEMU compares against external refs — QEMU (any ISA), Spike (riscv only), KVM (x86 only). Enable via menuconfig (`CONFIG_DIFFTEST=y`, pick ref design); the ref SO auto-builds on first `make run`.
- Other CLI flags: `--batch`, `--port`, `--pc-trace`, `--pc-trace-format` (raw|run), `--pc-trace-compress` (none|bzip2).
- **PC-trace tooling**: dynamic PC traces (`.pctrace` / bzip2-compressed) feed the NPC cachesim and locality analysis. Source under `src/trace/` (`pc_trace.c`, `observer.c`); helpers in `tools/trace/` (`pc_trace.py`, `compare_pc_traces.py`, `dump_pc_trace.py`, `test_pc_trace.py`, `compare_npc_itrace.py`). `scripts/run_ysyxsoc_trace_regression.py` runs an end-to-end ysyxsoc trace regression; `scripts/trace_log_filter.py` filters trace logs. Other tools: `tools/elf/`, `tools/gen-expr/`.

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

- Use `ARCH=riscv32e-ysyxsoc` for the SoC flow. `ARCH` defaults to `native` in rt-thread but has **no default** in abstract-machine itself (unset `ARCH` is a hard error: "Expected $ARCH in {...}").
- **The regression drives NPC**: the AM ysyxsoc platform (`abstract-machine/scripts/platform/ysyxsoc.mk`) builds the image, inserts mainargs, then invokes `make -C npc run IMG=...` with a full `RUN_CONFIG_*` mapping — the "primary regression" is really end-to-end RTL simulation. Trace logs land in `<app>/build/trace-logs/` (`sim.fst`, itrace/mtrace/ftrace/dtrace logs). Note the AM platform flips all traces **on** by default (`CONFIG_ITRACE/MTRACE/FTRACE/DTRACE/ETRACE ?= on`, `CONFIG_TRACE_FORMAT ?= both`), overriding the NPC Makefile-level "default off" — expect big trace logs.
- **AM-side extra targets**: `ysyxsoc.mk` also exposes `gdb`, and `cachesim`/`cachesim-inner` (NEMU PC-trace + Rust CacheSim pipeline, results in `<app>/build/cachesim-result/cachesim.json`). `abstract-machine/Makefile` itself has `locality`/`locality-report` targets (memory-locality analysis via `npc/scripts/mtrace_analyzer.py`).
- cpu-tests auto-discovers `tests/*.c` via `find` (currently 35: `dummy.c`, `add.c`, `movsx`, `unalign`, ...), not a hardcoded list. am-tests/klib-tests compile to a single image; alu-tests is generator-based — none iterate per-test like cpu-tests.
- Memory map (defined by `--defsym` in `abstract-machine/scripts/platform/ysyxsoc.mk`): flash 16M @ `0x30000000`, sram 8K @ `0x0f000000`, mrom @ `0x20000000`, psram 4M @ `0x80000000`, sdram 32M @ `0xa0000000`. `USE_SDRAM`/`USE_PSRAM`/`USE_FLASH_XIP` are **Makefile-level vars, NOT Kconfig**: they select the linker script and start code (flash-xip boot disables DTRACE).
- In rt-thread's `bsp/abstract-machine/`: `USE_SDRAM=1` → `linker-sdram-rtthread.ld` (32MB at `0xa0000000`); `USE_PSRAM=1` (auto-enabled on ysyxsoc when SDRAM is off) → `linker-psram-rtthread.ld` (4MB at `0x80000000`); a third `linker-nemu-rtthread.ld` serves the nemu/npc platform. The bsp is scons-driven: `make init`/`menuconfig` run scons to regenerate `rtconfig.h` + `files.mk` (both committed); the `update` target runs `integrate-am-apps.py` to fold AM apps into the build.

### NPC

```
make -C npc [run|gdb|default|chisel-gen|chisel-gen-standalone|gen_header|
            synth|synth-search|synth-clean|perf|perf-clean|chisel-clean|clean|
            synth-exp-a|synth-exp-b|synth-exp-c|synth-exp-d|synth-exp-all|synth-flow-diff|
            mtrace|locality|locality-report|test-locality|chisel-test|
            libnpc|npc-runner|npc-test|test|test-run]
```

- **Select program**: Use `IMG=<path>` for the binary/ELF to run (e.g., `make -C npc run IMG=build/flash.bin`). Note `gdb` does **NOT** forward `IMG` — set it inside gdb (`set env IMG ...`).
- **Sim mode**: `RUN_CONFIG_SIM_MODE` (default `ysyxsoc`). `standalone` uses bare `ysyx_25070190` without SoC peripherals (adds SDL2 for VGA); `ysyxsoc` uses `ysyxSoCFull` with NVBoard.
- **Make-level `RUN_CONFIG_*` vars**: `RUN_CONFIG_SIM_MODE` and `RUN_CONFIG_DPI=on` (default) are processed at Make level. All other `RUN_CONFIG_*` vars are passed as `NPC_CONFIG_*` env vars (default `off` unless noted):
  - `RUN_CONFIG_NVBOARD=on` (default), `RUN_CONFIG_PERF=on` (default)
  - `RUN_CONFIG_DIFFTEST`, `RUN_CONFIG_WAVE`, `RUN_CONFIG_ITRACE`/`MTRACE`/`FTRACE`/`DTRACE`/`ETRACE`, `RUN_CONFIG_VGA`
  - `RUN_CONFIG_TUI`, `RUN_CONFIG_DEVICE`, `RUN_CONFIG_MROM`, `RUN_CONFIG_DEBUG_OUTPUT`
  - Path overrides: `RUN_CONFIG_WAVE_FILE_PATH` (default `build/sim.fst`; AM overrides to `build/trace-logs/sim.fst`), `RUN_CONFIG_DIFFTEST_SO_FILE_PATH`, `RUN_CONFIG_FLASH_BIN_FILE_PATH`, plus per-trace log/jsonl paths and difftest port/payload/mode options.
  - `RUN_SDB_ENABLED` is a separate var (default `false`), passed as `NPC_SDB_ENABLED`.
- **C++ and ASAN**: Simulator builds with `-std=c++26` and links with `-fsanitize=address` (LDFLAGS only; CXXFLAGS does NOT include it, so ASAN instrumentation may be incomplete). Runs with `ASAN_OPTIONS=detect_leaks=0:exitcode=0`. NPC code must compile under C++26.
- `chisel-gen` rebuilds `npc/vsrc-chisel/` → `npc/vsrc/generated/`. It is a **PHONY** prerequisite of Verilator builds, so **every** `make`/`make run` re-runs Chisel elaboration (Mill + copy) — expect churn. `chisel-gen-standalone` forces standalone RTL regardless of `RUN_CONFIG_SIM_MODE`; `chisel-clean` forces regeneration.
- **Note**: `gdb` and `chisel-gen-standalone` are valid targets but are NOT in `.PHONY` — a same-named file in npc/ would make `make` skip them.
- `clean` removes `build/` + `vsrc/generated/` + `vsrc-chisel/generated/`, not just build artifacts.
- I-cache geometry flows into Chisel via Make vars `ICACHE_BLOCK_BYTES` (default 4) and `ICACHE_NUM_ENTRIES` (default 8).
- `synth` runs ASIC synthesis + STA via yosys-sta at 100 MHz. **It auto-rebuilds DPI-free RTL** (chisel-clean → chisel-gen with DPI off) before STA to reflect the physical netlist boundary. Key synth knobs:
  - `SYNTH_CLK_MHZ` (default 100), `SYNTH_QOR_VIEW` (default `canonical_flat`), `SYNTH_AREA_BUDGET_UM2` (default 23000)
  - `YOSYS_STA_AUTO_INIT=on` (default, auto-bootstraps on first synth), `SYNTH_EXPERIMENT` (empty=canonical)
- `synth-search` does binary frequency search (1–500 MHz). Output: `build/synth/synth_summary.json`.
- `synth-exp-a|b|c|d` run controlled synthesis experiments; `synth-exp-all` runs all four; `synth-flow-diff` generates a comparison report.
- `perf` orchestrates the full profiling pipeline: synth → rebuild simulator with DPI enabled → build microbench with `USE_SDRAM=1` → simulate with noisy knobs disabled → aggregate `perf.json` + `synth_summary.json`. Use `PERF_CHECK_STRICT=on` to fail on counter contract violations.
- **perf target quirk**: `perf` sets `NPC_CONFIG_*` env vars directly, bypassing the `RUN_CONFIG_*` → `NPC_CONFIG_*` translation — passing `RUN_CONFIG_*=X` on `make perf` has **no effect**. Override via `NPC_CONFIG_*=X` instead.
- `mtrace` runs simulation with memory-trace output (`RUN_CONFIG_TRACE_FORMAT=both`, `TRACE_DATA_MODE=stores`); `locality` analyzes the JSONL traces; `locality-report` combines both; `test-locality` runs the locality analyzer's unit tests. Note `test-locality` uses `python3 -m unittest discover` on `scripts/tests/test_locality_analyzer.py`, not pytest (pytest still works for the whole `scripts/tests/` suite).
- `default` builds the simulator binary without running; `gen_header` generates C++ Verilator headers for custom testbenches; `libnpc`/`npc-runner`/`npc-test`/`test`/`test-run` build the library/runner/test harnesses; `chisel-test` runs Chisel unit tests via Mill.
- **CacheSim (`npc/cachesim/`)**: a Rust reference model for the NPC I-cache (`cargo run --release -- simulate|compare`, CLI documented in `cachesim/README.md`). It consumes NEMU PC traces (`.pctrace.bz2`) — see the AM-side `cachesim` targets. `scripts/icache_dse.py` drives block-size DSE. The current RTL geometry is 4B blocks × 8 lines × 1 way, and cacheable ranges are flash `0x3000_0000..=0x3fff_ffff`, psram `0x8000_0000..=0x803f_ffff`, sdram `0xa000_0000..=0xa7ff_ffff`; everything else (incl. SRAM/MROM) bypasses.
- **`npc/docs/`**: `icache.md`, `memory-locality.md`, `npc-tui-config.md`, `perf-counter.md`, `testing.md`. Caveat per `cachesim/README.md`: `icache.md` (default geometry) and `perf-counter.md` (counter index range) are stale relative to current RTL.
- **TUI**: `RUN_CONFIG_TUI=on` plus config-file knobs `TUI_CONFIG_FILE_PATH` (default `build/npc-tui.toml`), `TUI_GENERATE_CONFIG`, `TUI_GENERATE_FULL_CONFIG`, `TUI_FORCE_OVERWRITE_CONFIG`, `TUI_PRINT_CONFIG_SCHEMA`, `TUI_PRINT_DEFAULT_CONFIG`. Backed by `csrc/tui/`, `include/tui/`, and a vendored `include/toml++/`.
- **Other `NPC_CONFIG_*` keys**: `TRACE_FORMAT` (default `human`), `TRACE_DATA_MODE` (default `stores`), `DIFFTEST_START_MODE/START_PC/PAYLOAD_*`, `DIFFTEST_MEM_MODE`, `MROM_BIN_FILE_PATH`, `MROM_ELF_FILE_PATH`, `FLASH_ELF_FILE_PATH`, `ETRACE_JSONL_OUT_FILE_PATH`.
- **Source layout**: C++ lives in `csrc/` (not `src/`); headers in `include/`. The Chisel sources are in `npc/vsrc-chisel/` (Chisel **7.1.1**, Scala **2.13.16** — independent of ysyxSoC's 7.0.0-M2/2.13.14). Scripts: `synth.sh`, `synth_search.py`, `synth_summary.py`, `synth_hierarchy.py`, `synth_timing.py`, `perf_aggregator.py`, `mtrace_analyzer.py`, `equiv_check.py`, `icache_dse.py`, `report_parser.py`, `ista_coverage_batch_runner.py`, `run_ista_coverage_repro.sh`.

### ysyxSoC

```
make -C ysyxSoC dev-init   # once per clone: submodule init + rocket-chip patch
make -C ysyxSoC verilog     # generate build/ysyxSoCFull.v
make -C ysyxSoC clean
```

- `ysyxSoC/src/CPU.scala` wraps the student CPU as an AXI4 blackbox named `ysyx_25070190` (it does not elaborate student RTL).
- Firtool comes from the root `flake.nix` dev shell via `circt` on `PATH`; `ysyxSoC/Makefile` expects `firtool` discoverable directly and no longer bootstraps its own copy.
- `dev-init` runs `git submodule update --init --recursive` then applies `patch/rocket-chip.patch` (zeros AXI4 lock/cache/prot/qos bits). **Not idempotent**: re-running after the patch is applied fails at `git apply`.
- Build system: Mill **1.1.2** (pinned by `.mill-version`; a stale `out/mill-launcher/0.12.4.jar` remains from an older setup), Chisel **7.0.0-M2**, Scala **2.13.14**. Entry point: `mill -i ysyxsoc.runMain ysyx.Elaborate --target-dir build` → `build/ysyxSoCTop.sv` → `mv` + two sed post-processing steps (AXI4 signal-prefix rename, strip `firrtl_black_box_resource_files`) → `build/ysyxSoCFull.v`. Only `src/*.scala` changes trigger rebuilds — edits under `rocket-chip/` require `make clean`.
- `ready-to-run/D-stage/` contains a pre-built reference SoC verilog; the D-stage CPU interface uses **SimpleBus** (bridged to AXI4 via MemBridge), not the AXI4 interface the current `CPU.scala` expects.

### fceux-am

```
make -C fceux-am ARCH=native run mainargs=mario
make -C fceux-am rom
```

- The Makefile **unconditionally** filters out `src/boards/emu2413.c` and `src/boards/vrc7.cpp` (audio sources) — not ARCH-dependent.
- `rom` regenerates `nes/gen/*.c` from the .nes ROMs via `build-roms.py`; the build depends on those generated sources.

### yosys-sta

```
make -C yosys-sta              # setup (auto-bootstraps on first npc synth if YOSYS_STA_AUTO_INIT=on)
```

- The default target is `init` (downloads a prebuilt iEDA binary + icsprout55 PDK; `pdk/` also has nangate45, the synthesis default `PDK`). `IEDA_BIN ?= iEDA` resolves via PATH, so a fresh clone's `bin/` is empty until `init` runs (or until first NPC `synth` with `YOSYS_STA_AUTO_INIT=on`). The `sta` target runs synthesis (yosys) + STA (iEDA) for the example GCD design — `syn` is its prerequisite. Use `DESIGN=mydesign SDC_FILE=/path/to/my.sdc RTL_FILES="/path/to/*.v"` for other designs; `CLK_FREQ_MHZ` defaults to 500 here (NPC synth overrides it). Requires yosys ≥ 0.48.
- Firtool comes from the nix dev shell `circt` package; `nix develop` is the supported way to get it on `PATH`. NPC synth invokes yosys-sta via `npc/scripts/synth.sh`.

## Verification

- Prefer the narrowest relevant build/test command; there is no repo-wide lint/typecheck workflow.
- **Primary regression**: `make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run` (auto-discovers 35 tests, runs all via NPC simulation, reports PASS/FAIL).
- **Difftest flow**: (1) `make -C nemu menuconfig` → enable `TARGET_SHARE=y` → (2) `make -C nemu GUEST_ISA=riscv32 SHARE=1 ENGINE=interpreter` → (3) `make -C npc run RUN_CONFIG_DIFFTEST=on`.
- **Waveform**: `make -C npc run RUN_CONFIG_WAVE=on` produces `build/sim.fst` (FST via Verilator `--trace-fst`; `build/trace-logs/sim.fst` when launched through the AM platform).
- **Performance regression**: `make -C npc perf` (builds microbench with `USE_SDRAM=1`, simulates, aggregates).
- **NPC tooling tests**: `python3 -m pytest npc/scripts/tests/` runs pytest-based unit tests for the synth/perf/equiv checker tooling.
