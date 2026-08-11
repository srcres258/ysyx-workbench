#include <isa.h>
#include <machine.h>
#include <machine/ysyxsoc.h>
#include <memory/host.h>
#include <memory/paddr.h>

void init_mem(void);

extern uint64_t g_nr_guest_inst;

static uint8_t uart_regs[YSYXSOC_UART_SIZE] = {};
static uint8_t timer_regs[YSYXSOC_CLINT_MTIME_SIZE] = {};
static uint8_t vga_regs[YSYXSOC_VGA_SIZE] = {};

static void reset_ysyxsoc_devices(void) {
  memset(uart_regs, 0, sizeof(uart_regs));
  memset(timer_regs, 0, sizeof(timer_regs));
  memset(vga_regs, 0, sizeof(vga_regs));
  uart_regs[5] = 0x60;
  *(uint32_t *)(void *)vga_regs = (400u << 16) | 300u;
}

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
  if (addr >= YSYXSOC_UART_BASE && addr + len <= YSYXSOC_UART_BASE + YSYXSOC_UART_SIZE) {
    uint32_t offset = addr - YSYXSOC_UART_BASE;
    if (offset == 0) {
      uart_regs[offset] = 0;
    } else if (offset == 5) {
      uart_regs[offset] = 0x60;
    }
    *data = host_read(uart_regs + offset, len);
    return true;
  }

  if (addr >= YSYXSOC_CLINT_MTIME_BASE && addr + len <= YSYXSOC_CLINT_MTIME_BASE + YSYXSOC_CLINT_MTIME_SIZE) {
    uint64_t mtime = g_nr_guest_inst;
    uint32_t lo = (uint32_t)(mtime & 0xffffffffu);
    uint32_t hi = (uint32_t)(mtime >> 32);
    uint32_t offset = addr - YSYXSOC_CLINT_MTIME_BASE;
    memcpy(timer_regs, &lo, sizeof(lo));
    memcpy(timer_regs + 4, &hi, sizeof(hi));
    *data = host_read(timer_regs + offset, len);
    return true;
  }

  if (addr >= YSYXSOC_VGA_BASE && addr + len <= YSYXSOC_VGA_BASE + YSYXSOC_VGA_SIZE) {
    uint32_t offset = addr - YSYXSOC_VGA_BASE;
    *data = host_read(vga_regs + offset, len);
    return true;
  }

  return false;
}

static bool ysyxsoc_mmio_write(paddr_t addr, int len, word_t data) {
  if (addr >= YSYXSOC_UART_BASE && addr + len <= YSYXSOC_UART_BASE + YSYXSOC_UART_SIZE) {
    uint32_t offset = addr - YSYXSOC_UART_BASE;
    host_write(uart_regs + offset, len, data);
    if (offset == 0 && (uart_regs[3] & 0x80) == 0) {
      char ch = uart_regs[offset];
      fputc(ch, stdout);
      if (ch == '\n' || ch == '\r') {
        fflush(stdout);
      }
    }
    return true;
  }

  if (addr >= YSYXSOC_CLINT_MTIME_BASE && addr + len <= YSYXSOC_CLINT_MTIME_BASE + YSYXSOC_CLINT_MTIME_SIZE) {
    uint32_t offset = addr - YSYXSOC_CLINT_MTIME_BASE;
    host_write(timer_regs + offset, len, data);
    return true;
  }

  if (addr >= YSYXSOC_VGA_BASE && addr + len <= YSYXSOC_VGA_BASE + YSYXSOC_VGA_SIZE) {
    uint32_t offset = addr - YSYXSOC_VGA_BASE;
    host_write(vga_regs + offset, len, data);
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
