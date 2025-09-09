#ifndef __RISCV_INTR_H__
#define __RISCV_INTR_H__

#ifndef XLEN
# ifdef CONFIG_RV64
#   define XLEN 64
# else
#   define XLEN 32
# endif
#endif

/* mcause 的取值, 描述 RISC-V 异常和中断的原因. */

// ----- 中断 -----
/**
 * @brief 中断: S 模式软件中断。
 */
#define MCAUSE_S_MODE_SOFTWARE_INTR ((1 << XLEN) | 1)
/**
 * @brief 中断: 虚拟 S 模式软件中断。
 */
#define MCAUSE_VIRT_S_MODE_SOFTWARE_INTR ((1 << XLEN) | 2)
/**
 * @brief 中断: M 模式软件中断。
 */
#define MCAUSE_M_MODE_SOFTWARE_INTR ((1 << XLEN) | 3)
/**
 * @brief 中断: S 模式时钟中断。
 */
#define MCAUSE_S_MODE_TIMER_INTR ((1 << XLEN) | 5)
/**
 * @brief 中断: 虚拟 S 模式时钟中断。
 */
#define MCAUSE_VIRT_S_MODE_TIMER_INTR ((1 << XLEN) | 6)
/**
 * @brief 中断: M 模式时钟中断。
 */
#define MCAUSE_M_MODE_TIMER_INTR ((1 << XLEN) | 7)
/**
 * @brief 中断: S 模式外部中断。
 */
#define MCAUSE_S_MODE_EXTERNAL_INTR ((1 << XLEN) | 9)
/**
 * @brief 中断: 虚拟 S 模式外部中断。
 */
#define MCAUSE_VIRT_S_MODE_EXTERNAL_INTR ((1 << XLEN) | 10)
/**
 * @brief 中断: M 模式外部中断。
 */
#define MCAUSE_M_MODE_EXTERNAL_INTR ((1 << XLEN) | 11)

// ----- 异常 -----
/**
 * @brief 异常: 指令地址不对齐。
 */
#define MCAUSE_INST_ADDR_MISALIGNED 0
/**
 * @brief 异常: 指令访问故障。
 */
#define MCAUSE_INST_ACCESS_FAULT 1
/**
 * @brief 异常: 非法指令。
 */
#define MCAUSE_ILLEGAL_INST 2
/**
 * @brief 异常: 断点。
 */
#define MCAUSE_BREAKPOINT 3
/**
 * @brief 异常: 读数地址不对齐。
 */
#define MCAUSE_LOAD_ADDR_MISALIGNED 4
/**
 * @brief 异常: 读数访问故障。
 */
#define MCAUSE_LOAD_ACCESS_FAULT 5
/**
 * @brief 异常: 存数/AMO 地址不对齐。
 */
#define MCAUSE_STORE_ADDR_MISALIGNED 6
/**
 * @brief 异常: 存数/AMO 访问故障。
 */
#define MCAUSE_STORE_ACCESS_FAULT 7
/**
 * @brief 异常: U/VU 模式环境调用。
 */
#define MCAUSE_ECALL_FROM_U_MODE 8
/**
 * @brief 异常: HS (S) 模式环境调用。
 */
#define MCAUSE_ECALL_FROM_S_MODE 9
/**
 * @brief 异常: VS 模式环境调用。
 */
#define MCAUSE_ECALL_FROM_VS_MODE 10
/**
 * @brief 异常: M 模式环境调用。
 */
#define MCAUSE_ECALL_FROM_M_MODE 11
/**
 * @brief 异常: 指令页故障。
 */
#define MCAUSE_INST_PAGE_FAULT 12
/**
 * @brief 异常: 读数页故障。
 */
#define MCAUSE_LOAD_PAGE_FAULT 13
/**
 * @brief 异常: 存数/AMO 页故障。
 */
#define MCAUSE_STORE_PAGE_FAULT 15
/**
 * @brief 异常: Double trap.
 */
#define MCAUSE_DOUBLE_TRAP 16
/**
 * @brief 异常: 软件检查。
 */
#define MCAUSE_SOFTWARE_CHECK 18
/**
 * @brief 异常: 硬件错误。
 */
#define MCAUSE_HARDWARE_ERROR 19
/**
 * @brief 异常: 指令客户页故障。
 */
#define MCAUSE_INST_GUEST_PAGE_FAULT 20
/**
 * @brief 异常: 读数客户页故障。
 */
#define MCAUSE_LOAD_GUEST_PAGE_FAULT 21
/**
 * @brief 异常: 虚拟指令。
 */
#define MCAUSE_VIRTUAL_INST 22
/**
 * @brief 异常: 存数/AMO 客户页故障。
 */
#define MCAUSE_STORE_GUEST_PAGE_FAULT 23

#endif
