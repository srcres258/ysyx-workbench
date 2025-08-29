#include <am.h>
#include <nemu.h>

void __am_timer_init() {
  // NEMU 启动时会自动初始化 timer 硬件，此处无需 AM 再手动去初始化了
}

void __am_timer_uptime(AM_TIMER_UPTIME_T *uptime) {
  volatile uintptr_t timer_addr;
  uint64_t result;

  /*
  注意访存顺序：先访问高字，再访问低字。

  根据 NEMU 内部机制，访问高字时才会触发 timer
  数据的更新从而获得最新的时刻数据。
  */
  timer_addr = (uintptr_t) RTC_ADDR;
  result = 0;
  result = inl(timer_addr + sizeof(uint32_t));   // 先访问高字
  result <<= 32;
  result |= inl(timer_addr);      // 再访问低字
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
