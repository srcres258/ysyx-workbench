# AGENTS.md — ysyx-workbench

`ysyx-workbench` is a meta-repo for NEMU, AbstractMachine, NPC, NVBoard, ysyxSoC, rt-thread, am-kernels, and fceux-am.

## Start here

- Run `nix develop` first. It exports `NEMU_HOME`, `AM_HOME`, `NPC_HOME`, `NVBOARD_HOME`, `YSYX_HOME`, and `VERILATOR_HOME`.
- `config.fish` is incomplete; do not rely on it instead of `nix develop`.
- The root `Makefile` is only a tracer. Always use `make -C <subproject>`.
- The root `.gitignore` is whitelist-based, so new root-level files usually need an explicit gitignore update.

## Repo layout

- `nemu/`, `abstract-machine/`, `npc/`, `nvboard/` are top-level tracked directories.
- `am-kernels/`, `npc/vsrc-chisel/`, `rt-thread/`, and `ysyxSoC/` are git submodules; use `git submodule status --recursive` when in doubt.
- `fceux-am/` is an independent git repo, not a submodule.
- Do not edit generated output directly: `npc/vsrc-chisel/generated/`, `npc/vsrc/generated/`, `npc/build/`, `nemu/build/`, and `ysyxSoC/build/`.
- Check `.sisyphus/plans/` before starting related feature work; some repo-specific plans already exist there.

## Verified commands

### NEMU

- `make -C nemu menuconfig`
- `make -C nemu savedefconfig`
- `make -C nemu %defconfig`
- `make -C nemu`
- `make -C nemu run`
- `make -C nemu gdb`
- `make -C nemu clean`
- `make -C nemu clean-all`
- `make -C nemu distclean`
- `make -C nemu run` expects `build/program.elf` to exist.
- For difftest, NEMU must be configured with `TARGET_SHARE=y`.

### AbstractMachine / am-kernels

- Use `ARCH=riscv32e-ysyxsoc` for the ysyxSoC-based NPC.
- Use `ARCH=riscv32e-npc` only for direct NPC.
- Typical test run: `make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run`
- `am-kernels/tests/cpu-tests` generates per-test Makefiles from `tests/*.c` and reports PASS/FAIL.

### NPC

- NPC uses `RUN_CONFIG_*` variables, not `ARCH`.
- Default mode is `RUN_CONFIG_SIM_MODE=ysyxsoc`; `standalone` builds the bare CPU.
- Verified targets: `make -C npc`, `make -C npc run`, `make -C npc gdb`, `make -C npc chisel-gen`, `make -C npc auto_bind`, `make -C npc clean`.
- Never edit `npc/vsrc/generated/`; edit hand-written RTL in `npc/vsrc/` and regenerate.
- `RUN_CONFIG_NVBOARD` defaults to `on`.
- Known NPC caveats: `NPC_CONFIG_MROM_ELF_FILE_PATH` is unused, MROM validation is incomplete, and `make gdb` omits the MROM toggle.

### ysyxSoC

- `make -C ysyxSoC dev-init` once per clone, then `make -C ysyxSoC verilog`.
- `make -C ysyxSoC clean`
- `ysyxSoC/src/CPU.scala` wraps the student CPU as `ysyx_25070190`.
- Firtool is pinned; do not swap it casually.

### rt-thread / fceux-am

- `rt-thread/bsp/abstract-machine` defaults to `ARCH=native`; set `ARCH=riscv32e-ysyxsoc` explicitly for the FPGA/SoC flow.
- `make -C rt-thread/bsp/abstract-machine ARCH=riscv32e-ysyxsoc init`
- `make -C rt-thread/bsp/abstract-machine ARCH=riscv32e-ysyxsoc run`
- `make -C rt-thread/bsp/abstract-machine ARCH=riscv32e-ysyxsoc menuconfig`
- `make -C fceux-am ARCH=native run mainargs=mario`
- `make -C fceux-am rom`

## Workflow reminders

- Use `git submodule status --recursive` before assuming a nested directory is part of the top-level repo.
- For NVBoard pin changes, keep RTL signal names aligned with `auto_pin_bind.py` output.
- Verify with the narrowest relevant build/test command; there is no repo-wide lint/typecheck workflow here.
