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

#define SERIAL_MMIO_ADDR 0xa00003f8

#endif /* __MACRO_DEF_HPP__ */
