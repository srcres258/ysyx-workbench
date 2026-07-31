# SDB

## Overview

The debugger is split into a shared C expression core (`nemu/src/monitor/sdb/sdb_core.c`) plus thin NEMU/NPC adapters.

## Expression syntax

- literals: decimal, hex (`0x`), binary (`0b`), octal (`0o`), char literals, `true`/`false`
- registers: `$pc`, `$sp`, `$a0`, `$x10`, etc.
- symbols: identifiers such as `main`
- memory:
  - `*EXPR` : target XLEN read
  - `u8[EXPR]`, `u16[EXPR]`, `u32[EXPR]`, `u64[EXPR]`
  - `i8[EXPR]`, `i16[EXPR]`, `i32[EXPR]`, `i64[EXPR]`
  - `mem[EXPR]`, `pmem[EXPR]`
- operators: unary `+ - ! ~ *`, binary `* / % + - << >> < <= > >= == != & ^ | && ||`, ternary `?:`

## Value model

- values are carried as `uint64_t + width + is_signed`
- overflow wraps at the selected width
- `&&` / `||` short-circuit
- shifts with an out-of-range count report an error

## Target differences

- **NEMU**: register read via `isa_reg_str2val`, memory read via `vaddr_read_mtrace`, MMIO reads are refused by default
- **NPC**: register read/write goes through the DPI adapter; memory reads/writes go through the NPC MMIO/PMEM backends; MMIO reads are refused by default unless forced by the adapter

## Commands currently covered

- `help`
- `c`, `q`
- `si`
- `info r`, `info w`
- `x N EXPR`
- `p EXPR`
- `w EXPR`
- `d N`

NPC also keeps `sic` for cycle stepping.

## Tests

- `nix develop -c python3 npc/scripts/tests/test_sdb_core.py`
- `nix develop -c python3 npc/scripts/tests/test_sdb_smoke.py`

## Known limitations

- The command framework is still legacy string dispatch.
- Advanced breakpoint/source/script commands are not yet wired.
- NEMU symbol lookup currently uses function symbols only.
