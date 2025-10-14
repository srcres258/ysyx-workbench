#include <stdint.h>
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

extern char _sram_start, _data_start, _data_end;

static void load_data_section(void) {
    volatile const uint8_t *src, *src_end;
    volatile uint8_t *dst;
    uint8_t val;

    for (
        src = (const uint8_t *) (intptr_t) _data_start,
        src_end = (const uint8_t *) (intptr_t) _data_end,
        dst = (uint8_t *) (intptr_t) _sram_start;
        src < src_end;
        src++, dst++
    ) {
        val = *src;
        *dst = val;
    }
}

void _trm_init(void) {
    int ret;

    load_data_section();
    ret = main(mainargs);
    halt(ret);
}
