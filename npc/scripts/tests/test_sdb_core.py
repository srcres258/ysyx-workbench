from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path


REPO = Path(__file__).resolve().parents[3]
SDB_CORE = REPO / "nemu" / "src" / "monitor" / "sdb" / "sdb_core.c"
SDB_INC = REPO / "nemu" / "src" / "monitor" / "sdb"


HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "sdb_core.h"

static uint8_t mem[64];
static int reg_reads = 0;
static int mem_reads = 0;

static bool read_reg(void *ud, const char *name, SdbValue *out, SdbError *err) {
  (void)ud;
  ++reg_reads;
  if (strcmp(name, "a0") == 0 || strcmp(name, "x10") == 0) { *out = sdb_value_make(3, 64, false); return true; }
  if (strcmp(name, "sp") == 0 || strcmp(name, "x2") == 0) { *out = sdb_value_make(16, 64, false); return true; }
  if (strcmp(name, "pc") == 0) { *out = sdb_value_make(32, 64, false); return true; }
  sdb_error_set(err, SDB_ERR_UNKNOWN_REGISTER, 0, 0, "unknown register", "mock");
  return false;
}

static bool write_reg(void *ud, const char *name, SdbValue value, SdbError *err) {
  (void)ud; (void)name; (void)value;
  sdb_error_set(err, SDB_ERR_UNSUPPORTED, 0, 0, "unsupported", "mock");
  return false;
}

static bool read_mem(void *ud, SdbAddressSpace space, uint64_t addr, unsigned width,
                     bool is_signed, bool force_mmio, SdbValue *out, SdbError *err) {
  (void)ud; (void)space; (void)is_signed; (void)force_mmio;
  ++mem_reads;
  if (addr + width / 8 > sizeof(mem)) {
    sdb_error_set(err, SDB_ERR_MEMORY_FAULT, 0, 0, "oob", "mock");
    return false;
  }
  uint64_t v = 0;
  for (unsigned i = 0; i < width / 8; ++i) {
    v |= ((uint64_t)mem[addr + i]) << (8 * i);
  }
  *out = sdb_value_make(v, width, false);
  return true;
}

static bool write_mem(void *ud, SdbAddressSpace space, uint64_t addr, unsigned width,
                      SdbValue value, bool force_mmio, SdbError *err) {
  (void)ud; (void)space; (void)addr; (void)width; (void)value; (void)force_mmio;
  sdb_error_set(err, SDB_ERR_UNSUPPORTED, 0, 0, "unsupported", "mock");
  return false;
}

static bool resolve_symbol(void *ud, const char *name, uint64_t *addr, SdbError *err) {
  (void)ud;
  if (strcmp(name, "main") == 0) { *addr = 0x40; return true; }
  sdb_error_set(err, SDB_ERR_UNKNOWN_SYMBOL, 0, 0, "unknown symbol", "mock");
  return false;
}

static bool lookup_symbol(void *ud, uint64_t addr, char *name, size_t name_len,
                          uint64_t *offset, SdbError *err) {
  (void)ud; (void)addr; (void)name; (void)name_len; (void)offset; (void)err;
  return false;
}

static uint64_t get_pc(void *ud) { (void)ud; return 32; }
static bool set_pc(void *ud, uint64_t pc, SdbError *err) { (void)ud; (void)pc; (void)err; return false; }

static const SdbTargetOps OPS = {
  .userdata = NULL,
  .xlen = 64,
  .allow_mmio_read = false,
  .read_register = read_reg,
  .write_register = write_reg,
  .read_memory = read_mem,
  .write_memory = write_mem,
  .resolve_symbol = resolve_symbol,
  .lookup_symbol = lookup_symbol,
  .get_pc = get_pc,
  .set_pc = set_pc,
};

static void init_mem(void) {
  for (size_t i = 0; i < sizeof(mem); ++i) mem[i] = (uint8_t)i;
}

int main(int argc, char **argv) {
  init_mem();
  for (int i = 1; i < argc; ++i) {
    SdbEvalResult r = {0};
    if (!sdb_expr_eval_text(argv[i], &OPS, &r)) {
      printf("ERR:%d:%s\n", (int)r.error.code, r.error.message);
      continue;
    }
    printf("OK:%llu:%u:%d\n", (unsigned long long)r.value.bits, r.value.width, (int)r.value.is_signed);
  }
  printf("COUNTS:%d:%d\n", reg_reads, mem_reads);
  return 0;
}
"""


def compile_harness(tmp: Path) -> Path:
    src = tmp / "sdb_core_harness.c"
    exe = tmp / "sdb_core_harness"
    src.write_text(HARNESS)
    subprocess.run(
        [
            "gcc",
            "-std=c11",
            "-O2",
            "-I",
            str(SDB_INC),
            str(src),
            str(SDB_CORE),
            "-o",
            str(exe),
        ],
        check=True,
        cwd=REPO,
    )
    return exe


def test_sdb_core_expression_semantics():
    with tempfile.TemporaryDirectory() as td:
        exe = compile_harness(Path(td))
        out = subprocess.check_output(
            [
                str(exe),
                "1+2*3",
                "(1+2)*3",
                "1<2==1",
                "u8[1]",
                "main+4",
                "1||main",
                "0&&main",
                "'\\n'",
            ],
            text=True,
            cwd=REPO,
        )
    lines = [line.strip() for line in out.splitlines() if line.strip()]
    assert lines[0] == "OK:7:64:0"
    assert lines[1] == "OK:9:64:0"
    assert lines[2] == "OK:1:1:0"
    assert lines[3] == "OK:1:8:0"
    assert lines[4] == "OK:68:64:0"
    assert lines[5] == "OK:1:1:0"
    assert lines[6] == "OK:0:1:0"
    assert lines[7] == "OK:10:8:0"


def test_sdb_core_short_circuit_counts():
    with tempfile.TemporaryDirectory() as td:
        exe = compile_harness(Path(td))
        out = subprocess.check_output([str(exe), "0&&main", "1||main"], text=True, cwd=REPO)
    counts = [line for line in out.splitlines() if line.startswith("COUNTS:")][0]
    _, reg_reads, mem_reads = counts.split(":")
    assert int(reg_reads) < 6
    assert int(mem_reads) == 0


if __name__ == "__main__":
    test_sdb_core_expression_semantics()
    test_sdb_core_short_circuit_counts()
    print("sdb_core tests passed")
