#include <memory/region.h>
#include <stdio.h>

FILE *log_fp = NULL;
NEMUState nemu_state = {0};

void assert_fail_msg(void) {
}

static void test_region_basic(void) {
  MemoryRegionDesc regions[] = {
    { .name = "rom", .base = 0x1000, .size = 0x100, .type = MEMORY_REGION_ROM },
    { .name = "ram", .base = 0x2000, .size = 0x100, .type = MEMORY_REGION_RAM },
    { .name = "mmio", .base = 0x3000, .size = 0x100, .type = MEMORY_REGION_MMIO },
  };
  uint32_t value = 0x11223344;
  MemoryRegion *rom;
  MemoryRegion *ram;

  memory_region_reset();
  memory_set_regions(regions, 3);

  rom = memory_find_region(0x1008, 4);
  ram = memory_find_region(0x2008, 4);

  if (rom == NULL || ram == NULL) {
    fprintf(stderr, "failed to find configured regions\n");
    exit(1);
  }
  if (memory_addr_in_mapped_region(0x3000, 4)) {
    fprintf(stderr, "mmio region unexpectedly treated as mapped RAM/ROM\n");
    exit(1);
  }
  if (memory_addr_is_writable(0x1008, 4)) {
    fprintf(stderr, "rom region unexpectedly writable\n");
    exit(1);
  }
  if (!memory_addr_is_writable(0x2008, 4)) {
    fprintf(stderr, "ram region unexpectedly read-only\n");
    exit(1);
  }

  memory_load_bytes(0x1008, &value, sizeof(value));
  memory_load_bytes(0x2008, &value, sizeof(value));

  if (*(uint32_t *)memory_guest_to_host(0x1008, 4) != value) {
    fprintf(stderr, "rom load mismatch\n");
    exit(1);
  }
  if (*(uint32_t *)memory_guest_to_host(0x2008, 4) != value) {
    fprintf(stderr, "ram load mismatch\n");
    exit(1);
  }
  if (memory_host_to_guest(memory_guest_to_host(0x2008, 4)) != 0x2008) {
    fprintf(stderr, "host/guest translation mismatch\n");
    exit(1);
  }

  memory_region_reset();
}

int main(void) {
  test_region_basic();
  puts("test_region: ok");
  return 0;
}
