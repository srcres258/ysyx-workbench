#include <isa.h>
#include <machine.h>
#include <machine/ysyxsoc.h>
#include <memory/paddr.h>

#include "internal.h"

void init_mem(void);

static long load_file_to_flash(const char *path) {
  FILE *fp = fopen(path, "rb");
  long size;
  uint8_t *buf;

  Assert(fp != NULL, "Can not open '%s'", path);
  fseek(fp, 0, SEEK_END);
  size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  buf = (uint8_t *)malloc(size == 0 ? 1 : size);
  assert(buf != NULL);
  Assert(fread(buf, size, 1, fp) == 1, "Failed to read '%s'", path);
  fclose(fp);

  Log("The ysyxsoc image is %s, size = %ld", path, size);
  paddr_load(YSYXSOC_FLASH_BASE, buf, size);
  free(buf);
  return size;
}

static long load_blob_to_flash(const void *buf, size_t size) {
  paddr_load(YSYXSOC_FLASH_BASE, buf, size);
  return size;
}

static void reset_ysyxsoc_devices(void) {
  ysyxsoc_uart_reset();
  ysyxsoc_timer_reset();
  ysyxsoc_vga_reset();
}

static void init_ysyxsoc_machine(void) {
  static const MemoryRegionDesc regions[] = {
    { .name = "sram",  .base = YSYXSOC_SRAM_BASE,  .size = YSYXSOC_SRAM_SIZE,  .type = MEMORY_REGION_RAM },
    { .name = "mrom",  .base = YSYXSOC_MROM_BASE,  .size = YSYXSOC_MROM_SIZE,  .type = MEMORY_REGION_ROM },
    { .name = "flash", .base = YSYXSOC_FLASH_BASE, .size = YSYXSOC_FLASH_SIZE, .type = MEMORY_REGION_ROM },
    { .name = "psram", .base = YSYXSOC_PSRAM_BASE, .size = YSYXSOC_PSRAM_SIZE, .type = MEMORY_REGION_RAM },
    { .name = "sdram", .base = YSYXSOC_SDRAM_BASE, .size = YSYXSOC_SDRAM_SIZE, .type = MEMORY_REGION_RAM },
  };

  init_mem();
  paddr_set_region_map(regions, ARRLEN(regions));
  reset_ysyxsoc_devices();
}

static void reset_ysyxsoc_machine(void) {
  reset_ysyxsoc_devices();
  isa_reset(YSYXSOC_FLASH_BASE);
}

static vaddr_t ysyxsoc_reset_vector(void) {
  return YSYXSOC_FLASH_BASE;
}

static void ysyxsoc_step(void) {
}

static bool ysyxsoc_mmio_read(paddr_t addr, int len, word_t *data) {
  if (ysyxsoc_uart_read(addr, len, data)) {
    return true;
  }
  if (ysyxsoc_timer_read(addr, len, data)) {
    return true;
  }
  if (ysyxsoc_vga_read(addr, len, data)) {
    return true;
  }
  return false;
}

static bool ysyxsoc_mmio_write(paddr_t addr, int len, word_t data) {
  if (ysyxsoc_uart_write(addr, len, data)) {
    return true;
  }
  if (ysyxsoc_timer_write(addr, len, data)) {
    return true;
  }
  if (ysyxsoc_vga_write(addr, len, data)) {
    return true;
  }
  return false;
}

const NemuMachine ysyxsoc_machine_profile = {
  .name = "ysyxsoc",
  .requires_image = true,
  .init = init_ysyxsoc_machine,
  .load_file = load_file_to_flash,
  .load_blob = load_blob_to_flash,
  .load_default_image = NULL,
  .reset = reset_ysyxsoc_machine,
  .reset_vector = ysyxsoc_reset_vector,
  .step = ysyxsoc_step,
  .mmio_read = ysyxsoc_mmio_read,
  .mmio_write = ysyxsoc_mmio_write,
};
