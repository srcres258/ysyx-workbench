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
- Check `.sisyphus/plans/` before related feature work (28 plans covering NPC features, peripherals, synthesis tooling, and RT-Thread integration).

## Boundaries

- Submodules: `am-kernels/`, `npc/vsrc-chisel/`, `rt-thread/`, `ysyxSoC/`, `npc-frontend/`, `standard/`, `yosys-sta/`.
- `fceux-am/` is a separate repo (listed in `.gitignore`, not tracked by this repo's git). It exists locally as a clone; glob/file-search tools may miss it due to gitignore exclusions.
- `nemu/`, `abstract-machine/`, `npc/`, and `nvboard/` are tracked directly in this repo. Note: `abstract-machine/` is NOT in the `.gitignore` whitelist — new files inside it require `git add -f` to track.
- `.gitmodules` has bad `npc-frontend` and `standard` entries (`path = rt-thread`), so `git submodule status --recursive` can fail.
- Do not edit generated output directly: `npc/vsrc/generated/`, `npc/build/`, `nemu/build/`, `ysyxSoC/build/`.

## Commands that matter

### NEMU

```
make -C nemu menuconfig|savedefconfig|%defconfig|run|gdb|clean|clean-all|distclean
```

- `run` expects `build/program.elf` (override with `IMG=<path>`).
- When `CONFIG_TARGET_AM=y` (menuconfig), NEMU switches to AM integration mode — `run`/`gdb`/`menuconfig` targets from `scripts/native.mk` are disabled, replaced by `$(AM_HOME)/Makefile` targets.
- **Difftest (NEMU as REF)**: set `TARGET_SHARE=y` in menuconfig → builds NEMU as a shared object (`.so`) with devices disabled. Build the SO: `make -C nemu GUEST_ISA=riscv32 SHARE=1 ENGINE=interpreter`. Output: `build/riscv32-nemu-interpreter-so`. NPC consumes this via `RUN_CONFIG_DIFFTEST_SO_FILE_PATH`.

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
make -C npc [run|gdb|chisel-gen|synth|synth-search|perf|clean]
```

- **Select program**: Use `IMG=<path>` to specify the binary/ELF to run (e.g., `make -C npc run IMG=build/flash.bin`).
- **Sim mode**: `RUN_CONFIG_SIM_MODE` (default `ysyxsoc`) — the ONLY `RUN_CONFIG_*` var processed at Make level. `standalone` uses bare `ysyx_25070190` without SoC peripherals; `ysyxsoc` uses `ysyxSoCFull` with NVBoard.
- **Key knobs** (all passed as `NPC_CONFIG_*` env vars, default `off` unless noted):
  - `RUN_CONFIG_NVBOARD=on` (default), `RUN_CONFIG_PERF=on` (default)
  - `RUN_CONFIG_DIFFTEST`, `RUN_CONFIG_WAVE`, `RUN_CONFIG_ITRACE`/`MTRACE`/`FTRACE`/`DTRACE`/`ETRACE`
  - `RUN_CONFIG_TUI`, `RUN_CONFIG_DEVICE`, `RUN_CONFIG_MROM`, `RUN_CONFIG_DPI=on`
  - Path overrides: `RUN_CONFIG_WAVE_FILE_PATH` (default `build/sim.fst`), `RUN_CONFIG_DIFFTEST_SO_FILE_PATH`, `RUN_CONFIG_FLASH_BIN_FILE_PATH`, etc.
- **C++ and ASAN**: Simulator builds with `-std=c++26 -fsanitize=address` and runs with `ASAN_OPTIONS=detect_leaks=0:exitcode=0`. For NPC code changes, code must compile under C++26.
- `chisel-gen` rebuilds `npc/vsrc-chisel/` → `npc/vsrc/generated/`. It is an **automatic prerequisite** of Verilator builds — running `make` or `make run` triggers it.
- `synth` runs ASIC synthesis + STA via yosys-sta at 100 MHz. Key synth knobs:
  - `SYNTH_CLK_MHZ` (default 100), `SYNTH_QOR_VIEW` (default `canonical_flat`), `SYNTH_AREA_BUDGET_UM2` (default 23000)
  - `YOSYS_STA_AUTO_INIT=on` (default, auto-bootstraps on first synth), `SYNTH_EXPERIMENT` (empty=canonical)
- `synth-search` does binary frequency search (1–500 MHz). Output: `build/synth/synth_summary.json`.
- `synth-exp-a|b|c|d` run controlled synthesis experiments; `synth-exp-all` runs all four; `synth-flow-diff` generates a comparison report.
- `perf` orchestrates the full profiling pipeline: synth → build microbench → simulate with all noisy knobs disabled → aggregate `perf.json` + `synth_summary.json`. Use `PERF_CHECK_STRICT=on` to fail on counter contract violations.

### ysyxSoC

```
make -C ysyxSoC dev-init   # once per clone: submodule init + rocket-chip patch
make -C ysyxSoC verilog     # generate build/ysyxSoCFull.v
make -C ysyxSoC clean
```

- `ysyxSoC/src/CPU.scala` wraps the student CPU as `ysyx_25070190`.
- Firtool is pinned to version **1.105.0** via `patch/update-firtool.sh`. If firtool updates upstream, this pin may need adjustment.

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

## Verification

- Prefer the narrowest relevant build/test command; there is no repo-wide lint/typecheck workflow.
- **Primary regression**: `make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run` (auto-discovers 35 tests, runs all, reports PASS/FAIL).
- **Difftest flow**: (1) `make -C nemu menuconfig` → enable `TARGET_SHARE=y` → (2) `make -C nemu GUEST_ISA=riscv32 SHARE=1 ENGINE=interpreter` → (3) `make -C npc run RUN_CONFIG_DIFFTEST=on`.
- **Waveform**: `make -C npc run RUN_CONFIG_WAVE=on` produces `build/sim.fst` (FST format via Verilator `--trace-fst`).
- **Performance regression**: `make -C npc perf` (builds microbench with `USE_SDRAM=1`, simulates, aggregates).
