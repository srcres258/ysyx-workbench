# AGENTS.md — ysyx-workbench

`ysyx-workbench` is a wrapper repo for NEMU, AbstractMachine, NPC, ysyxSoC, rt-thread, am-kernels, NVBoard, and the standalone fceux-am repo.

## Start here

- Run `nix develop` first; its shellHook sets `NEMU_HOME`, `AM_HOME`, `NPC_HOME`, `NVBOARD_HOME`, `YSYX_HOME`, and `VERILATOR_HOME`.
- `config.fish` is not a full setup path; ignore it for environment bootstrap.
- `init.sh` is the legacy bootstrap path referenced by `README.md`; it clones subprojects and can write env vars to `~/.bashrc`.
- The README bootstrap command (`bash init.sh subproject-name`) is setup-only; use the dev shell for normal work.
- The root `Makefile` is only a tracer. Always build with `make -C <subproject>`.
- Some subproject `run`/`gdb` targets auto-commit through the tracer Makefile; expect git history churn.
- The root `.gitignore` is whitelist-based, so new root-level files usually need a `.gitignore` update.
- Check `.sisyphus/plans/` before related feature work; the repo already carries task-specific plans and they often encode the intended flow.

## Repo boundaries

- Treat `am-kernels/`, `npc/vsrc-chisel/`, `rt-thread/`, and `ysyxSoC/` as submodule-owned areas; `fceux-am/` is a separate repo with its own `.git`.
- `.gitmodules` currently has a bad `npc-frontend` entry (`path = rt-thread`), so `git submodule status --recursive` can error on `npc-frontend`.
- Do not edit generated output directly: `npc/vsrc-chisel/generated/`, `npc/vsrc/generated/`, `npc/build/`, `nemu/build/`, `ysyxSoC/build/`.
- Check `.sisyphus/plans/` before related feature work.

## Commands that matter

- NEMU: `make -C nemu menuconfig|savedefconfig|%defconfig|run|gdb|clean|clean-all|distclean`; `run` expects `build/program.elf`; difftest needs `TARGET_SHARE=y`.
- AbstractMachine / am-kernels: use `ARCH=riscv32e-ysyxsoc` for the SoC flow (`ARCH=riscv32e-npc` only for direct NPC). Focused test: `make -C am-kernels/tests/cpu-tests ARCH=riscv32e-ysyxsoc run`.
- NPC uses `RUN_CONFIG_*` variables, not `ARCH`. Default sim mode is `RUN_CONFIG_SIM_MODE=ysyxsoc`; `standalone` builds the bare CPU; `IMG=$(IMG)` feeds the program image; `RUN_CONFIG_NVBOARD` defaults to `on`.
- `make -C npc chisel-gen` rebuilds `npc/vsrc-chisel` and copies generated RTL into `npc/vsrc/generated/`.
- ysyxSoC: run `make -C ysyxSoC dev-init` once per clone, then `make -C ysyxSoC verilog`; `ysyxSoC/src/CPU.scala` wraps the student CPU as `ysyx_25070190`; firtool is pinned intentionally.
- rt-thread / abstract-machine: `ARCH` defaults to `native`, so set `ARCH=riscv32e-ysyxsoc` for the FPGA/SoC flow; `USE_SDRAM`/`USE_PSRAM` change linker scripts in `rt-thread/bsp/abstract-machine`.
- fceux-am: `make -C fceux-am ARCH=native run mainargs=mario` and `make -C fceux-am rom`.
- npc-frontend: `pnpm install`, `pnpm dev`, `pnpm build`, `pnpm test:unit`, `pnpm type-check`; `pnpm build` runs type-check first.

## Verification

- Prefer the narrowest relevant build/test command; there is no repo-wide lint/typecheck workflow here.
