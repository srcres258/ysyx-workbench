#include <machine/ysyxsoc.h>
#include <memory/host.h>

#include "internal.h"

static uint8_t uart_regs[YSYXSOC_UART_SIZE] = {};

void ysyxsoc_uart_reset(void) {
  memset(uart_regs, 0, sizeof(uart_regs));
  uart_regs[5] = 0x60;
}

bool ysyxsoc_uart_read(paddr_t addr, int len, word_t *data) {
  uint32_t offset;

  if (!ysyxsoc_mmio_contains(addr, len, YSYXSOC_UART_BASE, YSYXSOC_UART_SIZE)) {
    return false;
  }

  offset = addr - YSYXSOC_UART_BASE;
  if (offset == 0) {
    uart_regs[offset] = 0;
  } else if (offset == 5) {
    uart_regs[offset] = 0x60;
  }
  *data = host_read(uart_regs + offset, len);
  return true;
}

bool ysyxsoc_uart_write(paddr_t addr, int len, word_t data) {
  uint32_t offset;

  if (!ysyxsoc_mmio_contains(addr, len, YSYXSOC_UART_BASE, YSYXSOC_UART_SIZE)) {
    return false;
  }

  offset = addr - YSYXSOC_UART_BASE;
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
