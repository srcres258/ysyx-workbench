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

#ifndef __MEMORY_PADDR_H__
#define __MEMORY_PADDR_H__

#include <common.h>
#include <memory/region.h>

#define PMEM_LEFT  ((paddr_t)CONFIG_MBASE)
#define PMEM_RIGHT ((paddr_t)CONFIG_MBASE + CONFIG_MSIZE - 1)
#define RESET_VECTOR (PMEM_LEFT + CONFIG_PC_RESET_OFFSET)

/* convert the guest physical address in the guest program to host virtual address in NEMU */
uint8_t* guest_to_host(paddr_t paddr);
/* convert the host virtual address in NEMU to guest physical address in the guest program */
paddr_t host_to_guest(uint8_t *haddr);

bool in_pmem(paddr_t addr);

void paddr_set_region_map(const MemoryRegionDesc *regions, size_t nr_regions);
size_t paddr_get_region_map(MemoryRegionDesc *regions, size_t max_regions);
MemoryRegion *paddr_find_region(paddr_t addr, size_t len);
void paddr_load(paddr_t addr, const void *buf, size_t len);

word_t paddr_read(paddr_t addr, int len);
word_t paddr_read_mtrace(paddr_t addr, int len, bool mtrace_on);
void paddr_write(paddr_t addr, int len, word_t data);
void paddr_write_mtrace(paddr_t addr, int len, word_t data, bool mtrace_on);

#endif
