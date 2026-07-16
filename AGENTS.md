# AGENTS.md — ysyx-workbench

`ysyx-workbench` wraps NEMU, AbstractMachine, NPC, ysyxSoC, rt-thread, am-kernels, NVBoard, fceux-am, and yosys-sta.

## Start here

- Run `nix develop` first; its shellHook exports `NEMU_HOME`, `AM_HOME`, `NPC_HOME`, `NVBOARD_HOME`, `YSYX_HOME`, and `VERILATOR_HOME`.
- `config.fish` only sets `NVBOARD_HOME`, `AM_HOME`, and `NPC_HOME`; it is not a full bootstrap path.
- `README.md` still points to `bash init.sh <subproject>`; `init.sh` is legacy/bootstrap only and may write env vars to `~/.bashrc`.
- The root `Makefile` is a tracer. Build with `make -C <subproject>`, not at repo root.
- NEMU build/run/gdb and NPC run/gdb auto-commit on the `tracer-ysyx` branch; expect git history churn.
- Root `.gitignore` is whitelist-based (`*.*` and `*` ignore everything, then `!` patterns re-include), so new root files usually need an explicit `.gitignore` entry.
- Check `.sisyphus/plans/` before related feature work.

## Boundaries

- Submodules: `am-kernels/`, `npc/vsrc-chisel/`, `rt-thread/`, `ysyxSoC/`, `npc-frontend/`, `standard/`, `yosys-sta/`.
- `fceux-am/` is a separate repo (listed in `.gitignore`, not tracked).
- `nemu/`, `abstract-machine/`, `npc/`, and `nvboard/` are tracked directly in this repo. Note: `abstract-machine/` is NOT in the `.gitignore` whitelist — new files inside it require `git add -f` to track.
- `.gitmodules` has bad `npc-frontend` and `standard` entries (`path = rt-thread`), so `git submodule status --recursive` can fail.
- Do not edit generated output directly: `npc/vsrc/generated/`, `npc/build/`, `nemu/build/`, `ysyxSoC/build/`.

## Commands that matter

### NEMU

```
make -C nemu menuconfig|savedefconfig|%defconfig|run|gdb|clean|clean-all|distclean
```

- `run` expects `build/program.elf` (override with `IMG=<path>`).
- **Difftest (NEMU as REF)**: set `TARGET_SHARE=y` in menuconfig → builds NEMU as a shared object (`.so`) with devices disabled. Build the SO: `make -C nemu GUEST_ISA=riscv32 SHARE=1 ENGINE=interpreter`. Output: `build/riscv32-nemu-interpreter-so`. NPC consumes this via `RUN_CONFIG_DIFFTEST_SO_FILE_PATH`.

### AbstractMachine / am-kernels

```
# Primary regression (35 cpu-tests):
make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run
# Other test suites:
make -C am-kernels/tests/am-tests ARCH=riscv32e-ysyxsoc run
make -C am-kernels/tests/alu-tests ARCH=riscv32e-ysyxsoc run
make -C am-kernels/tests/klib-tests ARCH=riscv32e-ysyxsoc run
```

- Use `ARCH=riscv32e-ysyxsoc` for the SoC flow. `ARCH` defaults to `native` in rt-thread but has no default in abstract-machine itself.
- In rt-thread's `bsp/abstract-machine/`, `USE_SDRAM` and `USE_PSRAM` select linker scripts: `USE_SDRAM=1` → 32MB at `0xa0000000`; `USE_PSRAM=1` (auto-enabled on ysyxsoc when SDRAM is off) → 4MB at `0x80000000`.

### NPC

```
make -C npc [run|gdb|chisel-gen|synth|synth-search|perf|clean]
```

- **Sim mode**: `RUN_CONFIG_SIM_MODE` (default `ysyxsoc`) — the ONLY `RUN_CONFIG_*` var processed at Make level. `standalone` uses bare `ysyx_25070190` without SoC peripherals; `ysyxsoc` uses `ysyxSoCFull` with NVBoard.
- **Key knobs** (all passed as `NPC_CONFIG_*` env vars, default `off` unless noted):
  - `RUN_CONFIG_NVBOARD=on` (default), `RUN_CONFIG_PERF=on` (default)
  - `RUN_CONFIG_DIFFTEST`, `RUN_CONFIG_WAVE`, `RUN_CONFIG_ITRACE`/`MTRACE`/`FTRACE`/`DTRACE`/`ETRACE`
  - `RUN_CONFIG_TUI`, `RUN_CONFIG_DEVICE`, `RUN_CONFIG_MROM`, `RUN_CONFIG_DPI=on`
  - Path overrides: `RUN_CONFIG_WAVE_FILE_PATH` (default `build/sim.fst`), `RUN_CONFIG_DIFFTEST_SO_FILE_PATH`, `RUN_CONFIG_FLASH_BIN_FILE_PATH`, etc.
- `chisel-gen` rebuilds `npc/vsrc-chisel/` → `npc/vsrc/generated/`. It is an **automatic prerequisite** of Verilator builds — running `make` or `make run` triggers it.
- `synth` runs ASIC synthesis + STA via yosys-sta at 100 MHz. `synth-search` does binary frequency search (1–500 MHz). Output: `build/synth/synth_summary.json`.
- `perf` orchestrates the full profiling pipeline: synth → build microbench → simulate with all noisy knobs disabled → aggregate `perf.json` + `synth_summary.json`.

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
