# NEMU machine split and ysyxSoC PC trace

## Ownership map

- **RISC-V instruction semantics**: `nemu/src/isa/riscv32/`
- **Machine selection and startup ownership**: `nemu/src/machine/`, `nemu/include/machine.h`
- **ysyxSoC memory map and device constants**: `nemu/include/machine/ysyxsoc.h`
- **Physical memory backends**: `nemu/src/memory/paddr.c`, `nemu/src/memory/region.c`
- **DiffTest ABI wrappers**: `nemu/src/cpu/difftest/`, `nemu/include/difftest-def.h`
- **Execution observer seam**: `nemu/src/trace/observer.c`, `nemu/include/trace/observer.h`
- **PC trace writer**: `nemu/src/trace/pc_trace.c`, `nemu/include/trace/pc_trace.h`
- **Normal monitor/frontend**: `nemu/src/monitor/`, `nemu/src/engine/interpreter/`

## Startup model

`init_isa()` is architecture-only.

`isa_reset(reset_pc)` resets architectural CPU state and sets the reset PC.

The selected machine now owns:

- memory backend selection;
- image placement;
- reset vector choice;
- minimal MMIO behavior;
- per-step machine-side updates.

## Machine profiles

### `nemu`

- default machine;
- simple contiguous PMEM backend;
- built-in image still available when no image path is given;
- if `CONFIG_DEVICE` is off, a small fallback MMIO path still handles classic AM serial / timer / VGA config addresses so `riscv32-nemu` AM binaries keep running.

### `ysyxsoc`

- reset vector: `0x30000000` (flash);
- region-backed physical memory:
  - SRAM `0x0f000000 .. 0x0f001fff`
  - MROM `0x20000000 .. 0x20000fff`
  - FLASH `0x30000000 .. 0x30ffffff`
  - PSRAM `0x80000000 .. 0x803fffff`
  - SDRAM `0xa0000000 .. 0xa1ffffff`
- minimal MMIO modeled for current AM workloads:
  - UART16550 at `0x10000000`
  - ACLINT MTIME at `0x0200bff8`
  - VGA config / framebuffer aperture at `0x21000000`

The current timer model is **deterministic**: MTIME is derived from retired guest instruction count, not host wall-clock time.

## Physical memory backends

`paddr.c` now dispatches through two backends:

- **simple backend**: classic contiguous PMEM;
- **region backend**: explicit `MemoryRegionDesc[]` map.

The generic region implementation lives in `region.c`; DiffTest now uses it through the adapter in `nemu/src/cpu/difftest/memory_adapter.c` instead of letting generic memory code include the DiffTest ABI directly.

## PC trace format

Header (`16` bytes, little-endian):

- magic: `PCTR`
- version: `1`
- header size: `16`
- address width: `4`
- encoding:
  - `1` = raw
  - `2` = run
- endianness flag: `1` = little-endian

### Raw encoding

After the header, each record is one `uint32_t` PC.

### Run encoding

After the header, records are tagged:

- `0x01` + `uint32_t pc`: one single PC
- `0x02` + `uint32_t start_pc` + `uint32_t count`: a sequential `pc, pc+4, ...` run (`PCTR v1` defines the RUN stride as 4 bytes)

Compression is external to the semantic trace format. `--pc-trace-compress=bzip2` streams the same binary format through `bzip2 -c`.

## CLI

### Normal NEMU

```bash
./build/riscv32-nemu-interpreter -b path/to/program.bin
```

### Select machine profile

```bash
./build/riscv32-nemu-interpreter -b --machine=ysyxsoc path/to/program.bin
```

### Raw PC trace

```bash
./build/riscv32-nemu-interpreter -b --machine=ysyxsoc \
  --pc-trace=/tmp/trace.pctrace --pc-trace-format=raw path/to/program.bin
```

### Run-compressed PC trace

```bash
./build/riscv32-nemu-interpreter -b --machine=ysyxsoc \
  --pc-trace=/tmp/trace-run.pctrace --pc-trace-format=run path/to/program.bin
```

### Run-compressed + bzip2 stream

```bash
./build/riscv32-nemu-interpreter -b --machine=ysyxsoc \
  --pc-trace=/tmp/trace-run.pctrace.bz2 --pc-trace-format=run \
  --pc-trace-compress=bzip2 path/to/program.bin
```

## Trace utilities

- decode / inspect: `python3 nemu/tools/trace/dump_pc_trace.py trace.pctrace --limit 32`
- compare two NEMU traces: `python3 nemu/tools/trace/compare_pc_traces.py a.pctrace b.pctrace`
- compare NPC itrace JSONL vs NEMU PC trace: `python3 nemu/tools/trace/compare_npc_itrace.py npc.jsonl nemu.pctrace`
- decoder unit tests: `python3 nemu/tools/trace/test_pc_trace.py`
- region backend smoke test:

```bash
nix develop --command gcc -I nemu/include -I nemu/build/obj-riscv32-nemu-interpreter/include/generated \
  -o /tmp/opencode/test_region nemu/tools/tests/test_region.c nemu/src/memory/region.c
/tmp/opencode/test_region
```

## Observed validation notes

- native build passed:

```bash
nix develop --command make -C nemu GUEST_ISA=riscv32
```

- shared REF build passed:

```bash
nix develop --command make -C nemu GUEST_ISA=riscv32 SHARE=1 ENGINE=interpreter
```

- `riscv32-nemu` hello passed with the default `nemu` machine.
- `riscv32e-ysyxsoc` hello passed with flash reset, FSBL, SSBL, PSRAM handoff, and good trap.
- `riscv32e-ysyxsoc` `am-tests` RTC flow ran under the standalone `ysyxsoc` machine and showed deterministic increasing uptime output.
- raw vs run decoding matched exactly on the same `ysyxsoc hello` boot;
- run vs run+`bzip2` decoding matched exactly on the same boot;
- decoded PC count matched the NEMU dynamic instruction count for that run (`33993` PCs for `ysyxsoc hello` in raw, run, and run+`bz2` forms);
- Python trace decoder tests passed (`3` tests);
- host-side region backend smoke test passed (`test_region: ok`).
- NPC vs NEMU boot traces matched for the first `3152` dynamic PCs in `hello`, then diverged in the SSBL UART polling loop because the current NEMU UART model is intentionally functional and always-ready, while NPC’s UART path exposes extra poll iterations before TX-ready becomes visible.
