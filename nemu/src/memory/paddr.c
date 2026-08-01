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

#include <memory/host.h>
#include <memory/paddr.h>
#include <device/mmio.h>
#include <isa.h>
#include <utils.h>

#ifndef CONFIG_TARGET_SHARE

# if defined(CONFIG_PMEM_MALLOC)
static uint8_t *pmem = NULL;
# else
static uint8_t pmem[CONFIG_MSIZE] PG_ALIGN = {};
# endif

uint8_t* guest_to_host(paddr_t paddr) { return pmem + paddr - CONFIG_MBASE; }
paddr_t host_to_guest(uint8_t *haddr) { return haddr - pmem + CONFIG_MBASE; }

static word_t pmem_read(paddr_t addr, int len) {
  return host_read(guest_to_host(addr), len);
}

static void pmem_write(paddr_t addr, int len, word_t data) {
  host_write(guest_to_host(addr), len, data);
}

bool in_pmem(paddr_t addr) {
  return addr >= PMEM_LEFT && addr <= PMEM_RIGHT;
}

void init_mem() {
# if defined(CONFIG_PMEM_MALLOC)
  pmem = malloc(CONFIG_MSIZE);
  assert(pmem);
# endif
  IFDEF(CONFIG_MEM_RANDOM, memset(pmem, rand(), CONFIG_MSIZE));
  Log("physical memory area [" FMT_PADDR ", " FMT_PADDR "]", PMEM_LEFT, PMEM_RIGHT);
}

#else

typedef struct {
  paddr_t  base;
  size_t   size;
  uint8_t *buf;
} MemRegion;

static MemRegion regions[] = {
  { 0x0f000000, 0x2000,     NULL },  /* SRAM */
  { 0x20000000, 0x1000,     NULL },  /* MROM */
  { 0x30000000, 0x1000000,  NULL },  /* FLASH */
  { 0x80000000, 0x400000,   NULL },  /* PSRAM */
  { 0xa0000000, 0x8000000,  NULL },  /* SDRAM */
};

#define NR_REGIONS (sizeof(regions) / sizeof(regions[0]))

static MemRegion *find_region(paddr_t paddr) {
  size_t i;
  for (i = 0; i < NR_REGIONS; i++) {
    if (paddr >= regions[i].base && paddr < regions[i].base + regions[i].size) {
      return &regions[i];
    }
  }
  return NULL;
}

static MemRegion *find_region_by_host(uint8_t *haddr) {
  size_t i;
  for (i = 0; i < NR_REGIONS; i++) {
    if (haddr >= regions[i].buf && haddr < regions[i].buf + regions[i].size) {
      return &regions[i];
    }
  }
  return NULL;
}

uint8_t* guest_to_host(paddr_t paddr) {
  MemRegion *r = find_region(paddr);
  if (r == NULL) {
    panic("address " FMT_PADDR " is not in any memory region", paddr);
    return NULL;
  }
  return r->buf + (paddr - r->base);
}

paddr_t host_to_guest(uint8_t *haddr) {
  MemRegion *r = find_region_by_host(haddr);
  if (r == NULL) {
    panic("host address %p is not in any memory region", (void *)haddr);
    return 0;
  }
  return r->base + (haddr - r->buf);
}

static word_t pmem_read(paddr_t addr, int len) {
  return host_read(guest_to_host(addr), len);
}

static void pmem_write(paddr_t addr, int len, word_t data) {
  host_write(guest_to_host(addr), len, data);
}

bool in_pmem(paddr_t addr) {
  return find_region(addr) != NULL;
}

void init_mem() {
  size_t i;
  for (i = 0; i < NR_REGIONS; i++) {
    regions[i].buf = malloc(regions[i].size);
    assert(regions[i].buf);
  }
  IFDEF(CONFIG_MEM_RANDOM,
    for (i = 0; i < NR_REGIONS; i++) memset(regions[i].buf, rand(), regions[i].size);
  );
  Log("Initialized %zu ysyxSoC memory regions", NR_REGIONS);
  Log("  SRAM  [0x0f000000, 0x0f001fff] 8KB");
  Log("  MROM  [0x20000000, 0x20000fff] 4KB");
  Log("  FLASH [0x30000000, 0x30ffffff] 16MB");
  Log("  PSRAM [0x80000000, 0x803fffff] 4MB");
  Log("  SDRAM [0xa0000000, 0xa7ffffff] 128MB");
}

#endif

static void out_of_bound(paddr_t addr) {
#ifdef CONFIG_ITRACE
  nemu_iringbuf_dump();
#endif
  panic("address " FMT_PADDR " is out of bound of every memory region at pc = " FMT_WORD,
      addr, cpu.pc);
}

#ifdef CONFIG_MTRACE
static void mtrace_record(
  paddr_t addr, int len,
  word_t data, const char *type
) {
  size_t offset;

  nemu_state.mtrace_available = true;
  offset = strlen(nemu_state.mtrace_logbuf);
  snprintf(
    nemu_state.mtrace_logbuf + offset,
    sizeof(nemu_state.mtrace_logbuf) - offset,
    "[mtrace] " FMT_PADDR ": Memory %s at " FMT_PADDR ", len %d, data 0x%08x\n",
    cpu.pc, type, addr, len, data
  );
}
#endif

word_t paddr_read(paddr_t addr, int len) {
  return paddr_read_mtrace(addr, len, true);
}

word_t paddr_read_mtrace(paddr_t addr, int len, bool mtrace_on) {
  word_t res;

  if (likely(in_pmem(addr))) {
    res = pmem_read(addr, len);
#ifdef CONFIG_MTRACE
    if (mtrace_on) {
      mtrace_record(addr, len, res, "read");
    }
#endif
    return res;
  }
#ifdef CONFIG_DEVICE
  res = mmio_read(addr, len);
#ifdef CONFIG_MTRACE
  if (mtrace_on) {
    mtrace_record(addr, len, res, "read");
  }
#endif
  return res;
#endif
  out_of_bound(addr);
  return 0;
}

void paddr_write(paddr_t addr, int len, word_t data) {
  return paddr_write_mtrace(addr, len, data, true);
}

void paddr_write_mtrace(paddr_t addr, int len, word_t data, bool mtrace_on) {
#ifdef CONFIG_MTRACE
  if (mtrace_on) {
    mtrace_record(addr, len, data, "write");
  }
#endif
  if (likely(in_pmem(addr))) { pmem_write(addr, len, data); return; }
  IFDEF(CONFIG_DEVICE, mmio_write(addr, len, data); return);
  out_of_bound(addr);
}
