#include <inttypes.h>
#include <memory/region.h>
#include <stdlib.h>

static MemoryRegion *regions = NULL;
static size_t nr_regions = 0;

const char *memory_region_type_name(MemoryRegionType type) {
  switch (type) {
    case MEMORY_REGION_RAM: return "ram";
    case MEMORY_REGION_ROM: return "rom";
    case MEMORY_REGION_MMIO: return "mmio";
    default: return "unknown";
  }
}

bool memory_region_is_host_backed(const MemoryRegion *region) {
  return region != NULL && region->type != MEMORY_REGION_MMIO;
}

bool memory_region_is_writable(const MemoryRegion *region) {
  return region != NULL && region->type == MEMORY_REGION_RAM;
}

bool memory_region_contains(const MemoryRegion *region, paddr_t addr, size_t len) {
  uint64_t start = addr;
  uint64_t end;

  if (region == NULL || region->size == 0) {
    return false;
  }
  if (len == 0) {
    len = 1;
  }
  end = start + len - 1;
  if (end < start) {
    return false;
  }
  return start >= region->base && end < region->base + region->size;
}

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

void memory_region_reset(void) {
  free_regions();
}

static void validate_region_desc(const MemoryRegionDesc *desc, size_t idx) {
  if (desc->size == 0) {
    panic("memory region %zu has zero size", idx);
  }
  if (desc->base + desc->size - 1 < desc->base) {
    panic("memory region %zu [%s] overflows address space", idx,
        desc->name == NULL ? memory_region_type_name(desc->type) : desc->name);
  }
}

void memory_set_regions(const MemoryRegionDesc *descs, size_t new_nr_regions) {
  size_t i;

  assert(descs != NULL || new_nr_regions == 0);
  free_regions();
  if (new_nr_regions == 0) {
    return;
  }

  regions = (MemoryRegion *)calloc(new_nr_regions, sizeof(MemoryRegion));
  assert(regions != NULL);
  nr_regions = new_nr_regions;

  for (i = 0; i < nr_regions; i++) {
    size_t j;
    uint64_t left;
    uint64_t right;

    validate_region_desc(descs + i, i);
    regions[i].name = descs[i].name;
    regions[i].base = descs[i].base;
    regions[i].size = descs[i].size;
    regions[i].type = descs[i].type;
    left = regions[i].base;
    right = regions[i].base + regions[i].size - 1;

    for (j = 0; j < i; j++) {
      uint64_t other_left = regions[j].base;
      uint64_t other_right = regions[j].base + regions[j].size - 1;
      if (left <= other_right && right >= other_left) {
        panic(
          "memory region %zu [%s] [0x%016" PRIx64 ", 0x%016" PRIx64 "] overlaps with region %zu [%s] [0x%016" PRIx64 ", 0x%016" PRIx64 "]",
          i,
          regions[i].name == NULL ? memory_region_type_name(regions[i].type) : regions[i].name,
          left, right,
          j,
          regions[j].name == NULL ? memory_region_type_name(regions[j].type) : regions[j].name,
          other_left, other_right
        );
      }
    }

    if (memory_region_is_host_backed(&regions[i])) {
      regions[i].buf = (uint8_t *)calloc(1, regions[i].size);
      assert(regions[i].buf != NULL);
    }
  }
}

size_t memory_get_regions(MemoryRegionDesc *out, size_t max_regions) {
  size_t ncopy = nr_regions;
  size_t i;

  if (out != NULL && max_regions < ncopy) {
    ncopy = max_regions;
  }
  if (out != NULL) {
    for (i = 0; i < ncopy; i++) {
      out[i].name = regions[i].name;
      out[i].base = regions[i].base;
      out[i].size = regions[i].size;
      out[i].type = regions[i].type;
    }
  }
  return nr_regions;
}

MemoryRegion *memory_find_region(paddr_t addr, size_t len) {
  size_t i;
  for (i = 0; i < nr_regions; i++) {
    if (memory_region_contains(&regions[i], addr, len)) {
      return &regions[i];
    }
  }
  return NULL;
}

MemoryRegion *memory_find_mapped_region(paddr_t addr, size_t len) {
  MemoryRegion *region = memory_find_region(addr, len);
  if (!memory_region_is_host_backed(region)) {
    return NULL;
  }
  return region;
}

MemoryRegion *memory_find_host_region(uint8_t *haddr) {
  size_t i;
  for (i = 0; i < nr_regions; i++) {
    if (!memory_region_is_host_backed(&regions[i])) {
      continue;
    }
    if (haddr >= regions[i].buf && haddr < regions[i].buf + regions[i].size) {
      return &regions[i];
    }
  }
  return NULL;
}

uint8_t *memory_guest_to_host(paddr_t addr, size_t len) {
  MemoryRegion *region = memory_find_mapped_region(addr, len);
  if (region == NULL) {
    panic("address " FMT_PADDR " (len=%zu) is not in any host-backed memory region", addr, len);
  }
  return region->buf + (addr - region->base);
}

paddr_t memory_host_to_guest(uint8_t *haddr) {
  MemoryRegion *region = memory_find_host_region(haddr);
  if (region == NULL) {
    panic("host address %p is not in any memory region", (void *)haddr);
  }
  return region->base + (haddr - region->buf);
}

bool memory_addr_in_mapped_region(paddr_t addr, size_t len) {
  return memory_find_mapped_region(addr, len) != NULL;
}

bool memory_addr_is_writable(paddr_t addr, size_t len) {
  MemoryRegion *region = memory_find_mapped_region(addr, len);
  return memory_region_is_writable(region);
}

void memory_load_bytes(paddr_t addr, const void *buf, size_t len) {
  const uint8_t *src = (const uint8_t *)buf;
  size_t remaining = len;
  paddr_t cur = addr;

  while (remaining > 0) {
    MemoryRegion *region = memory_find_mapped_region(cur, 1);
    size_t chunk;

    if (region == NULL) {
      panic("can not load %zu bytes at " FMT_PADDR ": unmapped or MMIO region", remaining, cur);
    }
    chunk = region->base + region->size - cur;
    if (chunk > remaining) {
      chunk = remaining;
    }
    memcpy(region->buf + (cur - region->base), src, chunk);
    cur += chunk;
    src += chunk;
    remaining -= chunk;
  }
}
