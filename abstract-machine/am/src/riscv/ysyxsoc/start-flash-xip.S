# ysyxSoC Flash XIP Single-Stage Bootloader
#
# 运行在 FLASH 上 (VMA = LMA = 0x30000000).
# 负责设置栈, 将 .data 节从 FLASH (LMA) 复制到 SRAM (VMA),
# 清零 .bss 节, 然后跳转到 FLASH 中的 _trm_init 开始执行应用程序.
#
# 与 FSBL (start.S) 的区别:
#   - 只做一级引导 (无 SSBL 阶段), 因为应用程序 .text 直接在 FLASH 中 XIP
#   - 复制的是 .data 节而非 .ssbl 节
#   - 直接跳转到 _trm_init, 不经过 ssbl_entry
#
# 注意: 该平台使用 RV32E (16 个寄存器 x0-x15).
# 可用寄存器: zero, ra, sp, gp, tp, t0-t2, s0-s1, a0-a5.
#
# 复制策略: 使用 lw/sw (32 位字复制) 而非 lbu/sb (字节复制).
# 这样可以避免 CPU RTL 中可能存在的字节写入 SRAM 的问题,
# 同时将复制速度提高约 4 倍.

.section .boot, "ax"
.globl _start
.type _start, @function

_start:
    # 1. 设置栈指针 (栈位于 SRAM 顶部, _stack_pointer 符号的地址在链接时确定)
    la sp, _stack_pointer

    # 2. 将 .data 节从 FLASH (LMA) 复制到 SRAM (VMA)
    #    使用 lw/sw 进行 32 位字复制
    la t0, _data_start           # t0 = 目的地址 (SRAM VMA)
    la t1, _data_lma             # t1 = 源地址   (FLASH LMA)
    la t2, _data_end             # t2 = 结束地址 (SRAM VMA)
0:
    bge t0, t2, 1f
    lw a0, 0(t1)                 # 从 FLASH 读取 32 位字
    sw a0, 0(t0)                 # 写入 SRAM 32 位字
    addi t0, t0, 4               # 目的地址前进 4 字节
    addi t1, t1, 4               # 源地址前进 4 字节
    j 0b

1:
#     # 3. 将 .bss 节清零 (SRAM 中)
#     #    使用 sw zero 进行 32 位字清零
#     la t0, _bss_start
#     la t1, _bss_end
# 2:
#     bge t0, t1, 3f
#     sw zero, 0(t0)
#     addi t0, t0, 4
#     j 2b

3:
    # 4. 跳转到 FLASH 中的 _trm_init (也在 FLASH 中, XIP)
    #    使用 la + jalr 实现长跳转 (超出 auipc 的 ±1MB 范围)
    la t0, _trm_init
    jalr zero, t0, 0

    # 启动代码不应到达此处
    ebreak

.size _start, . - _start
