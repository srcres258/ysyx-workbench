#include <am.h>
#include <nemu.h>

void __am_uart_config(AM_UART_CONFIG_T *cfg) {
    cfg->present = true;
}

void __am_uart_tx(AM_UART_TX_T *uart) {
    while (!(inb(SERIAL_PORT + 5) & 0x20));
    outb(SERIAL_PORT, uart->data);
}

void __am_uart_rx(AM_UART_RX_T *uart) {
    if (inb(SERIAL_PORT + 5) & 0x01) {
        uart->data = inb(SERIAL_PORT);
    } else {
        uart->data = (char)-1;
    }
}
