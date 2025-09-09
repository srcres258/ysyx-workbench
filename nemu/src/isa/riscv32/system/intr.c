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
#include <cpu/difftest.h>

#include "../local-include/reg.h"
#include "../local-include/intr.h"

word_t isa_raise_intr(word_t NO, vaddr_t epc) {
#ifdef CONFIG_DIFFTEST
  ref_difftest_raise_intr(NO);
#endif

#ifdef CONFIG_ETRACE
  nemu_state.etrace_available = true;
  size_t offset = strlen(nemu_state.etrace_logbuf);
  snprintf(
    nemu_state.etrace_logbuf + offset,
    sizeof(nemu_state.etrace_logbuf) - offset,
    "[etrace] " FMT_PADDR ": Exception number " FMT_WORD ", epc" FMT_PADDR "\n",
    cpu.pc, NO, epc
  );
#endif

  // riscv32触发异常后硬件的响应过程如下:
  // 1. 将当前PC值保存到mepc寄存器
  cpu.csr[CSR_MEPC] = epc;
  // 2. 在mcause寄存器中设置异常号
  cpu.csr[CSR_MCAUSE] = NO;
  // 3. 从mtvec寄存器中取出异常入口地址
  // (此处直接返回mtvec中的值即可)
  return cpu.csr[CSR_MTVEC];
}

word_t isa_query_intr() {
  return INTR_EMPTY;
}
