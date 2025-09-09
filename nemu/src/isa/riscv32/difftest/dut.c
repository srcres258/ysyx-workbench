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

bool isa_difftest_checkregs(CPU_state *ref_r, vaddr_t pc) {
  size_t i, count;

  count = sizeof(cpu.gpr) / sizeof(cpu.gpr[0]);
  for (i = 0; i < count; i++) {
    if (cpu.gpr[i] != ref_r->gpr[i]) {
      return false;
    }
  }

  if (cpu.pc != ref_r->pc) {
    return false;
  }

  if (cpu.csr[CSR_MSTATUS] != ref_r->csr[CSR_MSTATUS]) {
    return false;
  }
  if (cpu.csr[CSR_MTVEC] != ref_r->csr[CSR_MTVEC]) {
    return false;
  }
  if (cpu.csr[CSR_MEPC] != ref_r->csr[CSR_MEPC]) {
    return false;
  }
  if (cpu.csr[CSR_MCAUSE] != ref_r->csr[CSR_MCAUSE]) {
    return false;
  }
  if (cpu.csr[CSR_MTVAL] != ref_r->csr[CSR_MTVAL]) {
    return false;
  }

  return true;
}

void isa_difftest_attach() {
}
