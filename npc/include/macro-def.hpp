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
// RISC-V 中的 CSR 编号: 0x000 - 0xFFF, 共 4096 个。
#define RISCV_CSR_NUM 4096

#define MEMORY_PAGE_SHIFT 12
#define MEMORY_PAGE_SIZE (1 << MEMORY_PAGE_SHIFT)
#define MEMORY_PAGE_MASK (~(MEMORY_PAGE_SIZE - 1))

#define MROM_ADDR  0x20000000
#define MROM_LEN   0x1000

#define FLASH_ADDR 0x30000000
#define FLASH_LEN  0x1000000

#define PSRAM_ADDR 0x80000000
#define PSRAM_LEN  0x400000

#define TIMER_HZ 60

#define VGA_SCREEN_W 400
#define VGA_SCREEN_H 300

#endif /* __MACRO_DEF_HPP__ */
