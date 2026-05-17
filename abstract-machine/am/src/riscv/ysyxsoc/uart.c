#include <am.h>
#include <riscv/riscv.h>

// UART16550 寄存器地址 (与 trm.c 保持一致)
#define UART_BASE_ADDR      0x10000000
#define UART_LSR_ADDR       (UART_BASE_ADDR + 5)

void __am_uart_config(AM_UART_CONFIG_T *cfg) {
    cfg->present = true;
}

void __am_uart_tx(AM_UART_TX_T *uart) {
    // 轮询直到 THR 为空
    while (!(inb(UART_LSR_ADDR) & 0x20));   // bit 5 = THR Empty
    outb(UART_BASE_ADDR, uart->data);
}

void __am_uart_rx(AM_UART_RX_T *uart) {
    if (inb(UART_LSR_ADDR) & 0x01) {         // bit 0 = Data Ready
        uart->data = inb(UART_BASE_ADDR);     // 读 RBR
    } else {
        uart->data = (char)-1;                // 无数据返回 0xFF
    }
}
