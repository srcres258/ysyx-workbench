#ifndef __MACRO_DEF_HPP__
#define __MACRO_DEF_HPP__ 1

#ifdef CONFIG_RV64
#define RISCV_GPR_TYPE uint64_t
#else
#define RISCV_GPR_TYPE uint32_t
#endif

#ifdef CONFIG_RVE
#define RISCV_GPR_NUM 16
#else
#define RISCV_GPR_NUM 32
#endif

#define SERIAL_MMIO_ADDR    0xa00003f8
#define RTC_MMIO_ADDR       0xa0000048
#define VGA_CTL_MMIO_ADDR   0xa0000100
#define VGA_FB_MMIO_ADDR    0xa1000000
#define KEYBOARD_MMIO_ADDR  0xa0000060

#define TIMER_HZ 60

#define VGA_SCREEN_W 400
#define VGA_SCREEN_H 300

#endif /* __MACRO_DEF_HPP__ */
