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

#ifndef __ISA_RISCV_H__
#define __ISA_RISCV_H__

#include <common.h>
#include <stddef.h>

typedef struct CPU_State {
  word_t gpr[MUXDEF(CONFIG_RVE, 16, 32)];
  vaddr_t pc;

  // RISC-V 中的 CSR 编号: 0x000 - 0xFFF, 共 4096 个
  word_t csr[4096];
} MUXDEF(CONFIG_RV64, riscv64_CPU_state, riscv32_CPU_state);

_Static_assert(sizeof(struct CPU_State) == sizeof(word_t) * MUXDEF(CONFIG_RVE, 16, 32) + sizeof(vaddr_t) + sizeof(word_t) * 4096,
               "CPU_State size mismatch: DiffTest ABI broken");
_Static_assert(offsetof(struct CPU_State, gpr) == 0,
               "CPU_State gpr offset mismatch: DiffTest ABI broken");
_Static_assert(offsetof(struct CPU_State, pc) == sizeof(word_t) * MUXDEF(CONFIG_RVE, 16, 32),
               "CPU_State pc offset mismatch: DiffTest ABI broken");
_Static_assert(offsetof(struct CPU_State, csr) == sizeof(word_t) * MUXDEF(CONFIG_RVE, 16, 32) + sizeof(vaddr_t),
               "CPU_State csr offset mismatch: DiffTest ABI broken");

// decode
typedef struct {
  uint32_t inst;
} MUXDEF(CONFIG_RV64, riscv64_ISADecodeInfo, riscv32_ISADecodeInfo);

#define isa_mmu_check(vaddr, len, type) (MMU_DIRECT)

#define CSR_MSTATUS  0x300
#define CSR_MISA     0x301
#define CSR_MIE      0x304
#define CSR_MTVEC    0x305
#define CSR_MSCRATCH 0x340
#define CSR_MEPC     0x341
#define CSR_MCAUSE   0x342
#define CSR_MTVAL    0x343
#define CSR_MIP      0x344

#endif
