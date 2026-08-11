#ifndef __MEMORY_REGION_H__
#define __MEMORY_REGION_H__

#include <common.h>

typedef enum {
  MEMORY_REGION_RAM = 0,
  MEMORY_REGION_ROM = 1,
  MEMORY_REGION_MMIO = 2,
} MemoryRegionType;

typedef struct {
  const char *name;
  uint64_t base;
  uint64_t size;
  MemoryRegionType type;
} MemoryRegionDesc;

typedef struct {
  const char *name;
  uint64_t base;
  uint64_t size;
  MemoryRegionType type;
  uint8_t *buf;
} MemoryRegion;

const char *memory_region_type_name(MemoryRegionType type);
bool memory_region_is_host_backed(const MemoryRegion *region);
bool memory_region_is_writable(const MemoryRegion *region);
bool memory_region_contains(const MemoryRegion *region, paddr_t addr, size_t len);

void memory_region_reset(void);
void memory_set_regions(const MemoryRegionDesc *descs, size_t nr_regions);
size_t memory_get_regions(MemoryRegionDesc *out, size_t max_regions);

MemoryRegion *memory_find_region(paddr_t addr, size_t len);
MemoryRegion *memory_find_mapped_region(paddr_t addr, size_t len);
MemoryRegion *memory_find_host_region(uint8_t *haddr);

uint8_t *memory_guest_to_host(paddr_t addr, size_t len);
paddr_t memory_host_to_guest(uint8_t *haddr);
bool memory_addr_in_mapped_region(paddr_t addr, size_t len);
bool memory_addr_is_writable(paddr_t addr, size_t len);
void memory_load_bytes(paddr_t addr, const void *buf, size_t len);

#endif
