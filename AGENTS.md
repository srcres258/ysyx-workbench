# AGENTS.md — ysyx-workbench

`ysyx-workbench` is a wrapper repo for NEMU, AbstractMachine, NPC, ysyxSoC, rt-thread, am-kernels, NVBoard, and the standalone fceux-am repo.

## Before you touch anything

- Run `nix develop` first; its shellHook sets `NEMU_HOME`, `AM_HOME`, `NPC_HOME`, `NVBOARD_HOME`, `YSYX_HOME`, and `VERILATOR_HOME`.
- `config.fish` is not a full setup path; ignore it for environment bootstrap.
- `init.sh` is the legacy bootstrap path referenced by `README.md`; it clones subprojects and can write env vars to `~/.bashrc`.
- The README bootstrap command (`bash init.sh subproject-name`) is setup-only; use the dev shell for normal work.
- The root `Makefile` is only a tracer. Always build with `make -C <subproject>`.
- Some subproject `run`/`gdb` targets auto-commit through the tracer Makefile; expect git history churn.
- The root `.gitignore` is whitelist-based, so new root-level files usually need a `.gitignore` update.
- Check `.sisyphus/plans/` before related feature work; the repo already carries task-specific plans and they often encode the intended flow.

## Layout that matters

- `am-kernels/`, `npc/vsrc-chisel/`, `rt-thread/`, and `ysyxSoC/` are submodules; `fceux-am/` is a separate repo.
- Use `git submodule status --recursive` when a nested directory looks like it might be tracked separately.
- Do not edit generated output directly: `npc/vsrc-chisel/generated/`, `npc/vsrc/generated/`, `npc/build/`, `nemu/build/`, or `ysyxSoC/build/`.

## Commands that are actually used here

- NEMU: `make -C nemu menuconfig`, `savedefconfig`, `%defconfig`, `make -C nemu`, `make -C nemu run`, `make -C nemu gdb`, `make -C nemu clean`, `clean-all`, `distclean`.
- `make -C nemu run` expects `build/program.elf`; difftest requires `TARGET_SHARE=y`.
- AbstractMachine / am-kernels: use `ARCH=riscv32e-ysyxsoc` for the ysyxSoC flow (`ARCH=riscv32e-npc` only for direct NPC). Focused test run: `make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run`.
- `am-kernels/tests/cpu-tests` generates temporary `Makefile.*` files from `tests/*.c` and reports PASS/FAIL.
- NPC uses `RUN_CONFIG_*` variables, not `ARCH`. Default sim mode is `RUN_CONFIG_SIM_MODE=ysyxsoc`; `standalone` builds the bare CPU.
- NPC also keys program input from `IMG=$(IMG)`; keep that env var in mind when running images.
- NPC targets worth knowing: `make -C npc`, `make -C npc run`, `make -C npc gdb`, `make -C npc chisel-gen`, `make -C npc auto_bind`, `make -C npc clean`.
- `make -C npc chisel-gen` rebuilds `npc/vsrc-chisel` and copies generated RTL into `npc/vsrc/generated/`.
- `RUN_CONFIG_NVBOARD` defaults to `on`; if you change NVBoard pins, keep signal names aligned with `auto_pin_bind.py` output.
- ysyxSoC: run `make -C ysyxSoC dev-init` once per clone, then `make -C ysyxSoC verilog`; `ysyxSoC/src/CPU.scala` wraps the student CPU as `ysyx_25070190`.
- `make -C ysyxSoC dev-init` applies `patch/rocket-chip.patch` to the nested rocket-chip submodule.
- `ysyxSoC` pins firtool intentionally; do not swap the version casually.
- rt-thread / abstract-machine: `ARCH` defaults to `native`, so set `ARCH=riscv32e-ysyxsoc` for the FPGA/SoC flow (`init`, `run`, `menuconfig`); `USE_SDRAM`/`USE_PSRAM` change linker scripts in `rt-thread/bsp/abstract-machine`.
- fceux-am: `make -C fceux-am ARCH=native run mainargs=mario` and `make -C fceux-am rom`.

## Verification

- Prefer the narrowest relevant build/test command; there is no repo-wide lint/typecheck workflow here.
- If you need to confirm a nested repo boundary, verify with `git submodule status --recursive` before assuming ownership.
