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

#ifndef __DIFFTEST_DEF_H__
#define __DIFFTEST_DEF_H__

#include <stdint.h>
#include <stddef.h>
#include <macro.h>
#include <generated/autoconf.h>

#define __EXPORT __attribute__((visibility("default")))
#ifdef __cplusplus
# define __EXPORT_C extern "C" __EXPORT
#else
# define __EXPORT_C __EXPORT
#endif
enum { DIFFTEST_TO_DUT, DIFFTEST_TO_REF };

typedef enum {
  DIFFTEST_MEM_REGION_RAM = 0,
  DIFFTEST_MEM_REGION_MMIO = 1,
} DiffTestMemRegionType;

typedef enum {
  DIFFTEST_SKIP_REASON_MMIO = 0,
  DIFFTEST_SKIP_REASON_TIMER_DEVICE = 1,
  DIFFTEST_SKIP_REASON_NONDETERMINISTIC = 2,
} DiffTestSkipReason;

typedef struct {
  uint64_t base;
  uint64_t size;
  DiffTestMemRegionType type;
} DiffTestMemRegion;

__EXPORT_C void difftest_set_mem_map(const DiffTestMemRegion *regions, size_t nr_regions);
__EXPORT_C size_t difftest_get_mem_map(DiffTestMemRegion *regions, size_t max_regions);
__EXPORT_C void difftest_set_reset_vector(uint64_t reset_vector);

#if defined(CONFIG_ISA_x86)
# define DIFFTEST_REG_SIZE (sizeof(uint32_t) * 9) // GPRs + pc
#elif defined(CONFIG_ISA_mips32)
# define DIFFTEST_REG_SIZE (sizeof(uint32_t) * 38) // GPRs + status + lo + hi + badvaddr + cause + pc
#elif defined(CONFIG_ISA_riscv)
#define RISCV_GPR_TYPE MUXDEF(CONFIG_RV64, uint64_t, uint32_t)
#define RISCV_GPR_NUM  MUXDEF(CONFIG_RVE , 16, 32)
#define DIFFTEST_REG_SIZE (sizeof(RISCV_GPR_TYPE) * (RISCV_GPR_NUM + 1)) // GPRs + pc
#elif defined(CONFIG_ISA_loongarch32r)
# define DIFFTEST_REG_SIZE (sizeof(uint32_t) * 33) // GPRs + pc
#else
# error Unsupport ISA
#endif

#endif
