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
#include "local-include/reg.h"

void init_isa() {
}

void isa_reset(vaddr_t reset_pc) {
  memset(&cpu, 0, sizeof(cpu));
  cpu.pc = reset_pc;
  cpu.gpr[0] = 0;
#ifdef CONFIG_RV64
  cpu.csr[CSR_MSTATUS] = 0xa00001800;
#else
  cpu.csr[CSR_MSTATUS] = 0x1800;
#endif
}
