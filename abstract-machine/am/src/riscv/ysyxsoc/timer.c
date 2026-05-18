#include <am.h>
#include <riscv/riscv.h>

// ACLINT MTIME 设备寄存器基地址 (ysyxSoC CLINT 空间内)
#define ACLINT_MTIME_BASE 0x0200bff8

void __am_timer_init() {
    // MTIME 寄存器由硬件/DPI-C 自动递增，无需软件初始化
}

void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
    uint32_t hi1, hi2, lo;

    // RV32 读取 64-bit MTIME 的原子性保证：
    // 先读高位，再读低位，最后再读高位验证是否有进位
    // 若两次高位不同，说明低位发生了向高位的进位，需要重读
    do {
        hi1 = inl(ACLINT_MTIME_BASE + 4);  // 读 MTIME 高 32 位 (0x0200bffc)
        lo  = inl(ACLINT_MTIME_BASE);      // 读 MTIME 低 32 位 (0x0200bff8)
        hi2 = inl(ACLINT_MTIME_BASE + 4);  // 再读 MTIME 高 32 位
    } while (hi1 != hi2);

    uptime->us = ((uint64_t)hi1 << 32) | lo;
}

void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  rtc->second = 0;
  rtc->minute = 0;
  rtc->hour   = 0;
  rtc->day    = 0;
  rtc->month  = 0;
  rtc->year   = 1900;
}