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
  size_t i;

  b_buf = (uint8_t *) buf;
  
  if (direction == DIFFTEST_TO_REF) {
    for (i = 0; i < n; i++) {
      paddr_write(addr + i, sizeof(uint8_t), b_buf[i]);
    }
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
  init_mem();
  /* Do NOT call init_isa() here.
   * init_isa() loads a built-in test stub at RESET_VECTOR (MROM 0x20000000)
   * that would conflict with the actual memory content the NPC DUT injects
   * via difftest_memcpy().
   * The NPC side fully initialises the REF CPU state (PC, GPRs, CSRs) via
   * difftest_regcpy(DIFFTEST_TO_REF) after memcpy.
   * Ensure x0 is hardwired to zero (required by the RISC-V spec). */
  cpu.gpr[0] = 0;
}
