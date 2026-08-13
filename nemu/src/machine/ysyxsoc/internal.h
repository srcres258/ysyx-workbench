#ifndef __SRC_MACHINE_YSYXSOC_INTERNAL_H__
#define __SRC_MACHINE_YSYXSOC_INTERNAL_H__

#include <common.h>

static inline bool ysyxsoc_mmio_contains(paddr_t addr, int len, uint64_t base, uint64_t size) {
  uint64_t start = addr;
  uint64_t end;

  Assert(len > 0, "unexpected MMIO access width %d", len);
  end = start + (uint64_t)len - 1;
  if (end < start) {
    return false;
  }
  return start >= base && end < base + size;
}

void ysyxsoc_uart_reset(void);
bool ysyxsoc_uart_read(paddr_t addr, int len, word_t *data);
bool ysyxsoc_uart_write(paddr_t addr, int len, word_t data);

void ysyxsoc_timer_reset(void);
bool ysyxsoc_timer_read(paddr_t addr, int len, word_t *data);
bool ysyxsoc_timer_write(paddr_t addr, int len, word_t data);

void ysyxsoc_vga_reset(void);
bool ysyxsoc_vga_read(paddr_t addr, int len, word_t *data);
bool ysyxsoc_vga_write(paddr_t addr, int len, word_t data);

#endif
