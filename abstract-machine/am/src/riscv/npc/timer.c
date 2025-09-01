#include <am.h>
#include <riscv/riscv.h>

#define RTC_MMIO_ADDR 0xa0000048

void __am_timer_init() {
}

void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
  volatile uintptr_t timer_addr;
  uint64_t result;

  timer_addr = (uintptr_t) RTC_MMIO_ADDR;
  result = 0;
  result = inl(timer_addr + sizeof(uint32_t));    // 先访问高字
  result <<= 32;
  result |= inl(timer_addr);                      // 再访问低字
  uptime->us = result;
}

void __am_timer_rtc(AM_TIMER_RTC_T *rtc) {
  rtc->second = 0;
  rtc->minute = 0;
  rtc->hour   = 0;
  rtc->day    = 0;
  rtc->month  = 0;
  rtc->year   = 1900;
}
