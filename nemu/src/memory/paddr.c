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
#include <difftest-def.h>
#include <inttypes.h>
#include <stdlib.h>
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
  uint64_t base;
  uint64_t size;
  DiffTestMemRegionType type;
  uint8_t *buf;
} MemRegion;

static MemRegion *regions = NULL;
static size_t nr_regions = 0;

static void free_regions(void) {
  size_t i;

  if (regions == NULL) {
    return;
  }

  for (i = 0; i < nr_regions; i++) {
    free(regions[i].buf);
    regions[i].buf = NULL;
  }
  free(regions);
  regions = NULL;
  nr_regions = 0;
}

static MemRegion *find_region(paddr_t paddr) {
  size_t i;
  for (i = 0; i < nr_regions; i++) {
    if (regions[i].type == DIFFTEST_MEM_REGION_RAM &&
        paddr >= regions[i].base && paddr < regions[i].base + regions[i].size) {
      return &regions[i];
    }
  }
  return NULL;
}

static MemRegion *find_region_by_host(uint8_t *haddr) {
  size_t i;
  for (i = 0; i < nr_regions; i++) {
    if (regions[i].type == DIFFTEST_MEM_REGION_RAM &&
        haddr >= regions[i].buf && haddr < regions[i].buf + regions[i].size) {
      return &regions[i];
    }
  }
  return NULL;
}

__EXPORT void difftest_set_mem_map(const DiffTestMemRegion *new_regions, size_t new_nr_regions) {
  size_t i;

  assert(new_regions || new_nr_regions == 0);
  free_regions();
  if (new_nr_regions == 0) {
    return;
  }

  regions = (MemRegion *) calloc(new_nr_regions, sizeof(MemRegion));
  assert(regions);
  nr_regions = new_nr_regions;

  for (i = 0; i < nr_regions; i++) {
    regions[i].base = new_regions[i].base;
    regions[i].size = new_regions[i].size;
    regions[i].type = new_regions[i].type;
    if (regions[i].size == 0) {
      panic("memory region %zu has zero size", i);
    }
    if (i > 0) {
      size_t j;
      for (j = 0; j < i; j++) {
        uint64_t left = regions[i].base;
        uint64_t right = regions[i].base + regions[i].size - 1;
        uint64_t other_left = regions[j].base;
        uint64_t other_right = regions[j].base + regions[j].size - 1;
        if (left <= other_right && right >= other_left) {
          panic(
            "memory region %zu [0x%016" PRIx64 ", 0x%016" PRIx64 "] overlaps with region %zu [0x%016" PRIx64 ", 0x%016" PRIx64 "]",
            i, left, right, j, other_left, other_right
          );
        }
      }
    }
    if (regions[i].type == DIFFTEST_MEM_REGION_RAM) {
      regions[i].buf = (uint8_t *) calloc(1, regions[i].size);
      assert(regions[i].buf);
    }
  }
}

__EXPORT size_t difftest_get_mem_map(DiffTestMemRegion *out, size_t max_regions) {
  size_t ncopy;
  size_t i;

  ncopy = nr_regions;
  if (out && max_regions < ncopy) {
    ncopy = max_regions;
  }
  if (out) {
    for (i = 0; i < ncopy; i++) {
      out[i].base = regions[i].base;
      out[i].size = regions[i].size;
      out[i].type = regions[i].type;
    }
  }
  return nr_regions;
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
  free_regions();
  Log("Initialized REF memory map (runtime-configured)");
}

__EXPORT void difftest_set_reset_vector(uint64_t reset_vector) {
  cpu.pc = reset_vector;
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
  if (find_region(addr) != NULL) {
    panic("MMIO access to " FMT_PADDR " requires difftest skip on the DUT side", addr);
  }
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
  if (find_region(addr) != NULL) {
    panic("MMIO write to " FMT_PADDR " requires difftest skip on the DUT side", addr);
  }
  IFDEF(CONFIG_DEVICE, mmio_write(addr, len, data); return);
  out_of_bound(addr);
}
