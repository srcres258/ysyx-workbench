/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#include <utils.h>
#include <device/map.h>
#include <sys/select.h>
#include <unistd.h>

/* http://en.wikibooks.org/wiki/Serial_Programming/8250_UART_Programming */
// NOTE: this is compatible to 16550

#define CH_OFFSET 0
#define LSR_OFFSET 5
#define LSR_RX_READY 0x01
#define LSR_TX_EMPTY 0x20

static uint8_t *serial_base = NULL;
static int rx_char = -1;

static void serial_putc(char ch) {
  MUXDEF(CONFIG_TARGET_AM, putch(ch), putc(ch, stderr));
}

static int try_getchar() {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(0, &fds);
  struct timeval tv = {0, 0};
  if (select(1, &fds, NULL, NULL, &tv) > 0) {
    char ch;
    if (read(0, &ch, 1) == 1) return (uint8_t)ch;
  }
  return -1;
}

static void serial_io_handler(uint32_t offset, int len, bool is_write) {
  assert(len == 1);
  switch (offset) {
    case CH_OFFSET:
      if (is_write) {
        serial_putc(serial_base[0]);
      } else {
        if (rx_char >= 0) {
          serial_base[0] = (uint8_t)rx_char;
          rx_char = -1;
        }
      }
      break;
    case LSR_OFFSET:
      if (!is_write) {
        if (rx_char < 0) rx_char = try_getchar();
        uint8_t lsr = LSR_TX_EMPTY;
        if (rx_char >= 0) lsr |= LSR_RX_READY;
        serial_base[LSR_OFFSET] = lsr;
      }
      break;
    default: panic("do not support offset = %d", offset);
  }
}

void init_serial() {
  serial_base = new_space(8);
#ifdef CONFIG_HAS_PORT_IO
  add_pio_map ("serial", CONFIG_SERIAL_PORT, serial_base, 8, serial_io_handler);
#else
  add_mmio_map("serial", CONFIG_SERIAL_MMIO, serial_base, 8, serial_io_handler);
#endif

}
