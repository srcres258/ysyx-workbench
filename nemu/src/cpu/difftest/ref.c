/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <isa.h>
#include <cpu/cpu.h>
#include <difftest-def.h>
#include <memory/paddr.h>

__EXPORT void difftest_memcpy(paddr_t addr, void *buf, size_t n, bool direction) {
  uint8_t *b_buf;

  b_buf = (uint8_t *) buf;
  
  if (direction == DIFFTEST_TO_REF) {
    paddr_load(addr, b_buf, n);
  } else {
    assert(0);
  }
}

__EXPORT void difftest_regcpy(void *dut, bool direction) {
  CPU_state *dut_cpu;

  dut_cpu = (CPU_state *) dut;

  if (direction == DIFFTEST_TO_REF) {
    memcpy(&cpu, dut_cpu, sizeof(CPU_state));
  } else {
    memcpy(dut_cpu, &cpu, sizeof(CPU_state));
  }
}

__EXPORT void difftest_exec(uint64_t n) {
  cpu_exec(n);
}

__EXPORT void difftest_raise_intr(word_t NO) {
  cpu.csr[CSR_MEPC] = cpu.pc;
  cpu.csr[CSR_MCAUSE] = NO;
  cpu.pc = cpu.csr[CSR_MTVEC];
}

__EXPORT void difftest_init(int port) {
  void init_mem();
  (void) port;
  init_mem();
  /* DiffTest REF mode does not boot a standalone machine profile.
   * The DUT side supplies the REF memory map, reset vector, image bytes, and
   * architectural register state through the DiffTest setup ABI before any
   * instruction is executed. Keep init_isa() out of this path so the REF does
   * not inject its own built-in image or standalone reset flow. */
  cpu.gpr[0] = 0;
}
