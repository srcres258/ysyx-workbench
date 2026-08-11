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

#include <device/mmio.h>
#include <difftest-def.h>
#include <inttypes.h>
#include <isa.h>
#include <machine.h>
#include <memory/host.h>
#include <memory/paddr.h>
#include <memory/region.h>
#include <stdlib.h>
#include <utils.h>

typedef enum {
  PADDR_BACKEND_SIMPLE = 0,
  PADDR_BACKEND_REGIONS = 1,
} PaddrBackendType;

static PaddrBackendType paddr_backend =
#ifdef CONFIG_TARGET_SHARE
  PADDR_BACKEND_REGIONS;
#else
  PADDR_BACKEND_SIMPLE;
#endif

#ifndef CONFIG_TARGET_SHARE
# if defined(CONFIG_PMEM_MALLOC)
static uint8_t *pmem = NULL;
# else
static uint8_t pmem[CONFIG_MSIZE] PG_ALIGN = {};
# endif

static inline bool simple_range_ok(paddr_t addr, size_t len) {
  uint64_t end;
  if (len == 0) {
    len = 1;
  }
  end = (uint64_t)addr + len - 1;
  return end >= addr && addr >= PMEM_LEFT && end <= PMEM_RIGHT;
}
#endif

static inline bool use_region_backend(void) {
  return paddr_backend == PADDR_BACKEND_REGIONS;
}

static inline bool paddr_is_writable(paddr_t addr, size_t len) {
#ifdef CONFIG_TARGET_SHARE
  return memory_addr_is_writable(addr, len);
#else
  if (!use_region_backend()) {
    return simple_range_ok(addr, len);
  }
  return memory_addr_is_writable(addr, len);
#endif
}

uint8_t *guest_to_host(paddr_t paddr) {
#ifdef CONFIG_TARGET_SHARE
  return memory_guest_to_host(paddr, 1);
#else
  if (use_region_backend()) {
    return memory_guest_to_host(paddr, 1);
  }
  return pmem + paddr - CONFIG_MBASE;
#endif
}

paddr_t host_to_guest(uint8_t *haddr) {
#ifdef CONFIG_TARGET_SHARE
  return memory_host_to_guest(haddr);
#else
  if (use_region_backend()) {
    return memory_host_to_guest(haddr);
  }
  return haddr - pmem + CONFIG_MBASE;
#endif
}

static word_t pmem_read(paddr_t addr, int len) {
  if (use_region_backend()) {
    return host_read(memory_guest_to_host(addr, len), len);
  }
  return host_read(guest_to_host(addr), len);
}

static void pmem_write(paddr_t addr, int len, word_t data) {
  if (use_region_backend()) {
    host_write(memory_guest_to_host(addr, len), len, data);
    return;
  }
  host_write(guest_to_host(addr), len, data);
}

bool in_pmem(paddr_t addr) {
  if (use_region_backend()) {
    return memory_addr_in_mapped_region(addr, 1);
  }
#ifdef CONFIG_TARGET_SHARE
  return false;
#else
  return addr >= PMEM_LEFT && addr <= PMEM_RIGHT;
#endif
}

void init_mem() {
#ifndef CONFIG_TARGET_SHARE
# if defined(CONFIG_PMEM_MALLOC)
  pmem = malloc(CONFIG_MSIZE);
  assert(pmem);
# endif
  IFDEF(CONFIG_MEM_RANDOM, memset(pmem, rand(), CONFIG_MSIZE));
  paddr_backend = PADDR_BACKEND_SIMPLE;
  memory_region_reset();
  Log("physical memory area [" FMT_PADDR ", " FMT_PADDR "]", PMEM_LEFT, PMEM_RIGHT);
#else
  paddr_backend = PADDR_BACKEND_REGIONS;
  memory_region_reset();
  Log("Initialized REF memory map (runtime-configured)");
#endif
}

void paddr_set_region_map(const MemoryRegionDesc *regions, size_t nr_regions) {
  memory_set_regions(regions, nr_regions);
  paddr_backend = PADDR_BACKEND_REGIONS;
}

size_t paddr_get_region_map(MemoryRegionDesc *regions, size_t max_regions) {
#ifndef CONFIG_TARGET_SHARE
  if (!use_region_backend()) {
    if (regions != NULL && max_regions > 0) {
      regions[0].name = "pmem";
      regions[0].base = PMEM_LEFT;
      regions[0].size = CONFIG_MSIZE;
      regions[0].type = MEMORY_REGION_RAM;
    }
    return 1;
  }
#endif
  return memory_get_regions(regions, max_regions);
}

MemoryRegion *paddr_find_region(paddr_t addr, size_t len) {
#ifndef CONFIG_TARGET_SHARE
  if (!use_region_backend()) {
    if (simple_range_ok(addr, len)) {
      static MemoryRegion simple_region = {
        .name = "pmem",
        .base = PMEM_LEFT,
        .size = CONFIG_MSIZE,
        .type = MEMORY_REGION_RAM,
        .buf = NULL,
      };
      simple_region.buf = pmem;
      return &simple_region;
    }
    return NULL;
  }
#endif
  return memory_find_region(addr, len);
}

void paddr_load(paddr_t addr, const void *buf, size_t len) {
#ifndef CONFIG_TARGET_SHARE
  if (!use_region_backend()) {
    Assert(simple_range_ok(addr, len),
        "image [" FMT_PADDR ", " FMT_PADDR "] does not fit in pmem [" FMT_PADDR ", " FMT_PADDR "]",
        addr, (paddr_t)(addr + len - 1), PMEM_LEFT, PMEM_RIGHT);
    memcpy(guest_to_host(addr), buf, len);
    return;
  }
#endif
  memory_load_bytes(addr, buf, len);
}

#ifdef CONFIG_TARGET_SHARE
static MemoryRegionType difftest_type_to_memory_type(DiffTestMemRegionType type) {
  switch (type) {
    case DIFFTEST_MEM_REGION_RAM: return MEMORY_REGION_RAM;
    case DIFFTEST_MEM_REGION_MMIO: return MEMORY_REGION_MMIO;
    default: panic("unsupported difftest memory region type %d", type);
  }
}

__EXPORT void difftest_set_mem_map(const DiffTestMemRegion *new_regions, size_t new_nr_regions) {
  MemoryRegionDesc *regions = NULL;
  size_t i;

  if (new_nr_regions > 0) {
    regions = (MemoryRegionDesc *)calloc(new_nr_regions, sizeof(MemoryRegionDesc));
    assert(regions != NULL);
  }
  for (i = 0; i < new_nr_regions; i++) {
    regions[i].name = NULL;
    regions[i].base = new_regions[i].base;
    regions[i].size = new_regions[i].size;
    regions[i].type = difftest_type_to_memory_type(new_regions[i].type);
  }
  paddr_set_region_map(regions, new_nr_regions);
  free(regions);
}

__EXPORT size_t difftest_get_mem_map(DiffTestMemRegion *out, size_t max_regions) {
  size_t total = paddr_get_region_map(NULL, 0);
  MemoryRegionDesc *regions;
  size_t i;
  size_t ncopy = total;

  if (out != NULL && max_regions < ncopy) {
    ncopy = max_regions;
  }
  if (out == NULL) {
    return total;
  }

  regions = (MemoryRegionDesc *)calloc(total == 0 ? 1 : total, sizeof(MemoryRegionDesc));
  assert(regions != NULL);
  paddr_get_region_map(regions, total);
  for (i = 0; i < ncopy; i++) {
    out[i].base = regions[i].base;
    out[i].size = regions[i].size;
    out[i].type = regions[i].type == MEMORY_REGION_MMIO ? DIFFTEST_MEM_REGION_MMIO : DIFFTEST_MEM_REGION_RAM;
  }
  free(regions);
  return total;
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

  if (likely(use_region_backend() ? memory_addr_in_mapped_region(addr, len) : in_pmem(addr))) {
    res = pmem_read(addr, len);
#ifdef CONFIG_MTRACE
    if (mtrace_on) {
      mtrace_record(addr, len, res, "read");
    }
#endif
    return res;
  }
  if (machine_mmio_read(addr, len, &res)) {
#ifdef CONFIG_MTRACE
    if (mtrace_on) {
      mtrace_record(addr, len, res, "read");
    }
#endif
    return res;
  }
  if (use_region_backend() && memory_find_region(addr, len) != NULL) {
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
  if (likely(use_region_backend() ? memory_addr_in_mapped_region(addr, len) : in_pmem(addr))) {
    if (!paddr_is_writable(addr, len)) {
      panic("write to read-only memory region at " FMT_PADDR " (len=%d)", addr, len);
    }
    pmem_write(addr, len, data);
    return;
  }
  if (use_region_backend() && memory_find_region(addr, len) != NULL) {
    panic("MMIO write to " FMT_PADDR " requires difftest skip on the DUT side", addr);
  }
  if (machine_mmio_write(addr, len, data)) {
    return;
  }
  out_of_bound(addr);
}
