#include <am.h>
#include <klib-macros.h>
#include <riscv/riscv.h>

extern char _heap_start;
int main(const char *args);

extern char _pmem_start;
#define PMEM_SIZE (128 * 1024 * 1024)
#define PMEM_END  ((uintptr_t)&_pmem_start + PMEM_SIZE)

Area heap = RANGE(&_heap_start, PMEM_END);
static const char mainargs[MAINARGS_MAX_LEN] = TOSTRING(MAINARGS_PLACEHOLDER); // defined in CFLAGS

#define SERIAL_MMIO_ADDR 0xa00003f8
#define RTC_MMIO_ADDR 0xa0000048

void putch(char ch) {
  outb(SERIAL_MMIO_ADDR, ch);
}

void halt(int code) {
  // GCC/Clang 内嵌汇编语法：asm 或 __asm__
  // 后面可加 volatile 或 __volatile__ 关键字，表示该语句不应被优化，保留原样
  asm volatile("mv a0, %0" : : "r" (code));
  asm volatile("ebreak");

  while (1);
}

void _trm_init() {
  int ret = main(mainargs);
  halt(ret);
}
