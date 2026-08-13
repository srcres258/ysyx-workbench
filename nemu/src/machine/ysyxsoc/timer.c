#include <machine/ysyxsoc.h>
#include <memory/host.h>

#include "internal.h"

extern uint64_t g_nr_guest_inst;

static uint8_t timer_regs[YSYXSOC_CLINT_MTIME_SIZE] = {};

void ysyxsoc_timer_reset(void) {
  memset(timer_regs, 0, sizeof(timer_regs));
}

bool ysyxsoc_timer_read(paddr_t addr, int len, word_t *data) {
  uint64_t mtime;
  uint32_t lo;
  uint32_t hi;
  uint32_t offset;

  if (!ysyxsoc_mmio_contains(addr, len, YSYXSOC_CLINT_MTIME_BASE, YSYXSOC_CLINT_MTIME_SIZE)) {
    return false;
  }

  mtime = g_nr_guest_inst;
  lo = (uint32_t)(mtime & 0xffffffffu);
  hi = (uint32_t)(mtime >> 32);
  offset = addr - YSYXSOC_CLINT_MTIME_BASE;
  memcpy(timer_regs, &lo, sizeof(lo));
  memcpy(timer_regs + 4, &hi, sizeof(hi));
  *data = host_read(timer_regs + offset, len);
  return true;
}

bool ysyxsoc_timer_write(paddr_t addr, int len, word_t data) {
  uint32_t offset;

  if (!ysyxsoc_mmio_contains(addr, len, YSYXSOC_CLINT_MTIME_BASE, YSYXSOC_CLINT_MTIME_SIZE)) {
    return false;
  }

  offset = addr - YSYXSOC_CLINT_MTIME_BASE;
  host_write(timer_regs + offset, len, data);
  return true;
}
