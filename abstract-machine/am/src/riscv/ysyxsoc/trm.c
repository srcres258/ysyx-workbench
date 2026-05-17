#include <am.h>
#include <stdint.h>
#include <klib-macros.h>
#include <riscv/riscv.h>

int main(const char *args);

static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

#define UART_BASE_ADDR 0x10000000
#define UART_TX_ADDR UART_BASE_ADDR
#define UART_INTERRUPT_ENABLE_ADDR (UART_BASE_ADDR + 1)
#define UART_FIFO_CONTROL_ADDR (UART_BASE_ADDR + 2)
#define UART_LINE_CONTROL_ADDR (UART_BASE_ADDR + 3)
#define UART_MODEM_CONTROL_ADDR (UART_BASE_ADDR + 4)
#define UART_LINE_STATUS_ADDR (UART_BASE_ADDR + 5)

#define SYSTEM_CLOCK_FREQ 10000000
#define UART_BAUD_RATE 115200

extern char _bss_start, _bss_end;
extern char _heap_start, _heap_end;

Area heap = RANGE(&_heap_start, &_heap_end);

/* --- UART 16550 初始化 --- */

static void init_uart(void) {
    uint16_t divisor_latches_val;

    /* 初始化 UART 16550 的步骤 */

    // 步骤 1. 禁用所有中断 (optional but recommended, 避免初始化过程中被中断干扰)
    outb(UART_INTERRUPT_ENABLE_ADDR, 0b00000000);

    // 步骤 2. 设置波特率
    // 波特率 = 系统时钟频率 / (16 * 除数)
    // 在 UART 16550 的寄存器中, 不直接接受波特率值, 而接受除数.
    // 除数 = 系统时钟频率 / (16 * 波特率)
    // 计算除数
    divisor_latches_val = SYSTEM_CLOCK_FREQ / (16 * UART_BAUD_RATE);
    // 允许访问除数寄存器
    outb(UART_LINE_CONTROL_ADDR, 0b10000000);
    // 先写入高位字节 (MSB)
    outb(UART_BASE_ADDR + 1, (uint8_t) ((divisor_latches_val & 0xFF00) >> 8));
    // 再写入低位字节 (LSB)
    outb(UART_BASE_ADDR, (uint8_t) (divisor_latches_val & 0xFF));
    // 关闭访问除数寄存器, 同时设置数据位, 校验位, 停止位
    outb(UART_LINE_CONTROL_ADDR, 0b00000000);

    // 步骤 3. 配置数据格式
    // 最常用格式: 8 数据位, 无校验位, 1 停止位
    outb(UART_LINE_CONTROL_ADDR, 0b00000011);

    // 步骤 4. 配置 Modem 控制 (optional)
    // 这里置空即可; 我们目前不需要用到 Modem
    outb(UART_MODEM_CONTROL_ADDR, 0b00000000);

    // 步骤 5. 设置 FIFO
    // 这里留空, 也就是不启用 FIFO 功能.
    outb(UART_FIFO_CONTROL_ADDR, 0b00000000);

    // 步骤 6. 启用中断 (optional)
    // 这里留空, 因为我们目前不需要用到中断. 无需执行操作.
}

void putch(char ch) {
    // 在向 THR 写入数据之前, 必须确保 UART 的发送 FIFO 或者 THR 是空的.
    // 否则可能造成数据覆盖或者丢失.

    while ((inb(UART_LINE_STATUS_ADDR) & 0b00100000) == 0);

    for (volatile int i = 0; i < 10; i++);

    outb(UART_TX_ADDR, ch);
}

void halt(int code) {
    // GCC/Clang 内嵌汇编语法: asm 或 __asm__
    // 后面可加 volatile 或 __volatile__ 关键字, 表示该语句不应被优化, 保留原样
    asm volatile ("mv a0, %0" : : "r" (code));
    asm volatile ("ebreak");

    while (1);
}

#ifndef FLASH_XIP_BOOT
static void zero_bss_section(void) {
    volatile uint8_t *p;

    for (p = (volatile uint8_t *) (intptr_t) &_bss_start;
         p < (volatile uint8_t *) (intptr_t) &_bss_end;
         p++) {
        *p = 0;
    }
}
#endif

void _trm_init(void) {
    int ret;

    init_uart();
#ifndef FLASH_XIP_BOOT
    zero_bss_section();
#endif
    ret = main(mainargs);
    halt(ret);
}
