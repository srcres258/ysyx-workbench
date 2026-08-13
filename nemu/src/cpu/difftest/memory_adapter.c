#include <difftest-def.h>
#include <isa.h>
#include <memory/paddr.h>

#ifdef CONFIG_TARGET_SHARE
static MemoryRegionType difftest_type_to_memory_type(DiffTestMemRegionType type) {
  switch (type) {
    case DIFFTEST_MEM_REGION_RAM: return MEMORY_REGION_RAM;
    case DIFFTEST_MEM_REGION_MMIO: return MEMORY_REGION_MMIO;
    default: panic("unsupported difftest memory region type %d", type);
  }
}

static DiffTestMemRegionType memory_type_to_difftest_type(MemoryRegionType type) {
  switch (type) {
    case MEMORY_REGION_MMIO: return DIFFTEST_MEM_REGION_MMIO;
    case MEMORY_REGION_RAM:
    case MEMORY_REGION_ROM:
      return DIFFTEST_MEM_REGION_RAM;
    default:
      panic("unsupported memory region type %d", type);
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
    out[i].type = memory_type_to_difftest_type(regions[i].type);
  }
  free(regions);
  return total;
}

__EXPORT void difftest_set_reset_vector(uint64_t reset_vector) {
  cpu.pc = reset_vector;
}
#endif
