#include <machine/ysyxsoc.h>
#include <memory/host.h>

#include "internal.h"

static uint8_t vga_regs[YSYXSOC_VGA_SIZE] = {};

void ysyxsoc_vga_reset(void) {
  memset(vga_regs, 0, sizeof(vga_regs));
  *(uint32_t *)(void *) vga_regs = (400u << 16) | 300u;
}

bool ysyxsoc_vga_read(paddr_t addr, int len, word_t *data) {
  uint32_t offset;

  if (!ysyxsoc_mmio_contains(addr, len, YSYXSOC_VGA_BASE, YSYXSOC_VGA_SIZE)) {
    return false;
  }

  offset = addr - YSYXSOC_VGA_BASE;
  *data = host_read(vga_regs + offset, len);
  return true;
}

bool ysyxsoc_vga_write(paddr_t addr, int len, word_t data) {
  uint32_t offset;

  if (!ysyxsoc_mmio_contains(addr, len, YSYXSOC_VGA_BASE, YSYXSOC_VGA_SIZE)) {
    return false;
  }

  offset = addr - YSYXSOC_VGA_BASE;
  host_write(vga_regs + offset, len, data);
  return true;
}
