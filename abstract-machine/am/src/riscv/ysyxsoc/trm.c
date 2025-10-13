#include <klib-macros.h>
#include <riscv/riscv.h>

int main(const char *args);

static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

#define UART_TX_ADDR 0x10000000

void putch(char ch) {
    outb(UART_TX_ADDR, ch);
}

void halt(int code) {
    // GCC/Clang 内嵌汇编语法: asm 或 __asm__
    // 后面可加 volatile 或 __volatile__ 关键字, 表示该语句不应被优化, 保留原样
    asm volatile ("mv a0, %0" : : "r" (code));
    asm volatile ("ebreak");

    while (1);
}

void _trm_init(void) {
    int ret = main(mainargs);
    halt(ret);
}
