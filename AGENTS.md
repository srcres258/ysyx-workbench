# AGENTS.md — ysyx-workbench

`ysyx-workbench` wraps NEMU, AbstractMachine, NPC, ysyxSoC, rt-thread, am-kernels, NVBoard, and fceux-am.

## Start here

- Run `nix develop` first; its shellHook exports `NEMU_HOME`, `AM_HOME`, `NPC_HOME`, `NVBOARD_HOME`, `YSYX_HOME`, and `VERILATOR_HOME`.
- `config.fish` only sets `NVBOARD_HOME`, `AM_HOME`, and `NPC_HOME`; it is not a full bootstrap path.
- `README.md` still points to `bash init.sh <subproject>`; `init.sh` is legacy/bootstrap only and may write env vars to `~/.bashrc`.
- The root `Makefile` is a tracer. Build with `make -C <subproject>`, not at repo root.
- NEMU build/run/gdb and NPC run/gdb auto-commit on the `tracer-ysyx` branch; expect git history churn.
- Root `.gitignore` is whitelist-based, so new root files usually need an explicit `.gitignore` entry.
- Check `.sisyphus/plans/` before related feature work.

## Boundaries

- Treat `am-kernels/` and `npc/vsrc-chisel/` as submodules; `fceux-am/` is a separate repo.
- `nemu/`, `abstract-machine/`, `npc/`, `nvboard/`, `ysyxSoC/`, and `rt-thread/` are tracked directly in this repo.
- `.gitmodules` has bad `npc-frontend` and `standard` entries (`path = rt-thread`), so `git submodule status --recursive` can fail.
- Do not edit generated output directly: `npc/vsrc/generated/`, `npc/build/`, `nemu/build/`, `ysyxSoC/build/`.

## Commands that matter

- NEMU: `make -C nemu menuconfig|savedefconfig|%defconfig|run|gdb|clean|clean-all|distclean`; `run` expects `build/program.elf`; difftest needs `TARGET_SHARE=y`.
- AbstractMachine: use `ARCH=riscv32e-ysyxsoc` for the SoC flow; focused test: `make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run`. `ARCH` defaults to `native` in rt-thread.
- NPC: use `RUN_CONFIG_*` env vars, not `ARCH`; default sim mode is `ysyxsoc`, `standalone` is the bare CPU path, and `RUN_CONFIG_NVBOARD` defaults to `on`.
- `make -C npc chisel-gen` rebuilds `npc/vsrc-chisel` and copies generated RTL to `npc/vsrc/generated/`.
- ysyxSoC: run `make -C ysyxSoC dev-init` once per clone, then `make -C ysyxSoC verilog`; `ysyxSoC/src/CPU.scala` wraps the student CPU as `ysyx_25070190`, and the Makefile pins firtool to `1.105.0`.
- rt-thread / AbstractMachine: `USE_SDRAM` and `USE_PSRAM` change linker-script selection in `rt-thread/bsp/abstract-machine`.
- fceux-am: `make -C fceux-am ARCH=native run mainargs=mario` and `make -C fceux-am rom`.

## Verification

- Prefer the narrowest relevant build/test command; there is no repo-wide lint/typecheck workflow here.
