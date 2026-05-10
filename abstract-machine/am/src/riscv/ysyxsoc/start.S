# ysyxSoC Bootloader (第一阶段加载器)
#
# 运行在 FLASH 上 (VMA = LMA = 0x30000000).
# 负责将 .text / .data 段从 FLASH (LMA) 复制到 SRAM (VMA),
# 然后跳转到 SRAM 中的 _trm_init 继续执行.
#
# .rodata 段保留在 FLASH 中 (XIP 读取), 因为 SRAM 只有 8KB,
# 而 .text + .rodata 超过了 8KB 容量.
# .rodata 主要是 printf 格式字符串等只读数据, 从 FLASH 读取的
# 延迟对程序整体性能影响很小.
#
# 注意: 该平台使用 RV32E (16 个寄存器 x0-x15).
# 可用寄存器: zero, ra, sp, gp, tp, t0-t2, s0-s1, a0-a5.
#
# 复制策略: 使用 lw/sw (32 位字复制) 替代 lbu/sb (字节复制).
# 这样可以避免 CPU RTL 中可能存在的字节写入 SRAM 的问题,
# 同时将复制速度提高约 4 倍。.text 段大小 (7360 bytes) 恰好为 4 的倍数。

.section .boot, "ax"
.globl _start
.type _start, @function

_start:
    # 1. 设置栈指针 (栈位于 SRAM 顶部, _stack_pointer 符号的地址在链接时确定)
    la sp, _stack_pointer

    # 2. 将 .text 段从 FLASH (LMA) 复制到 SRAM (VMA)
    #    使用 lw/sw 进行 32 位字复制
    la t0, _text_start          # t0 = 目的地址 (SRAM VMA)
    la t1, _text_lma            # t1 = 源地址   (FLASH LMA)
    la t2, _text_end            # t2 = 结束地址 (SRAM VMA)
0:
    bge t0, t2, 1f
    lw a0, 0(t1)                # 从 FLASH 读取 32 位字
    sw a0, 0(t0)                # 写入 SRAM 32 位字
    addi t0, t0, 4              # 目的地址前进 4 字节
    addi t1, t1, 4              # 源地址前进 4 字节
    j 0b

1:
    # 3. 将 .data 段从 FLASH (LMA) 复制到 SRAM (VMA)
    la t0, _data_start
    la t1, _data_lma
    la t2, _data_end
2:
    bge t0, t2, 3f
    lw a0, 0(t1)
    sw a0, 0(t0)
    addi t0, t0, 4
    addi t1, t1, 4
    j 2b

3:
    # 4. 跳转到 SRAM 中的 _trm_init
    #    注意: 此处不能使用 call 伪指令, 因为 _trm_init 的 VMA 在 SRAM,
    #    与当前 FLASH 执行地址相距过远, 超出 auipc 的 ±1MB PC-relative 范围.
    #    因此使用 la (生成 lui+addi 绝对地址加载) + jalr 实现长跳转.
    la t0, _trm_init
    jalr zero, t0, 0

    # Bootloader 不应到达此处
    ebreak

.size _start, . - _start
