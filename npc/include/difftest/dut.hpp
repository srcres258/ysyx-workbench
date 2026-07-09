#ifndef __DIFFTEST__DUT_HPP__
#define __DIFFTEST__DUT_HPP__ 1

#include <common.hpp>
#include <difftest-def.hpp>

/**
 * @brief DiffTest dut: 跳过在 DUT 上能执行但在 REF 上不能执行的一条指令。
 * 
 * 调用此函数后，下次调用 difftest_dut_step 函数时将仅在 DUT 上执行指令，
 * 跳过 REF 上相同指令的执行。
 */
void difftest_dut_skipRef(addr_t pc);

/**
 * @brief difftest dut: 校准 DUT 与 REF 上的指令执行情况。由于某些 REF
 * 会进行指令压缩或打包，导致在 DUT 上执行若干条指令时，在 REF 上仅需执行
 * 更少数量的指令即可达到相同效果。
 * 
 * 调用此函数后，将先尝试让 REF 单步执行若干次，再让 DUT 执行若干次（需之后
 * 再手动调用 difftest_dut_step 完成），之后预期 DUT 与 REF 达到相同的指
 * 令执行状态。
 * 
 * 调用此函数之后的若干次调用 difftest_dut_step 函数时将不再让 REF 同步
 * 执行指令。
 * 
 * @param nr_ref 在 REF 上需要执行的指令数量。
 * @param nr_dut 在 DUT 上将要执行的指令数量。
 */
void difftest_dut_skipDut(int nr_ref, int nr_dut);

void difftest_dut_setPatch(void (*fn)(void *arg), void *arg);

void difftest_dut_detach();

void difftest_dut_attach();

/**
 * @brief DiffTest dut: 初始化 DiffTest 执行环境。
 * 
 * @param refSoFile REF 的动态链接库文件路径。
 * @param port DUT 用于与 REF 通信的端口号。需要提前确保端口号未被占用。
 */
void difftest_dut_init(const char *refSoFile, int port);

/**
 * @brief DiffTest dut: 在 DUT 上已完成一步指令执行，通知 REF 同步执行
 * 在 DUT 上所执行的指令。
 * 
 * @param pc 当前 DUT 执行这条指令时的程序计数器的值。
 * @param npc 当前 DUT 执行完这条指令后的程序计数器的值。
 */
void difftest_dut_step(addr_t pc, addr_t npc);

/**
 * @brief DiffTest dut: 向 REF 传送当前处理器状态以保持状态同步。
 */
void difftest_dut_syncCurrentProcessorState();

/**
 * @brief DiffTest dut: 清除待处理的 skipRef 标志。
 *
 * 在 difftest 激活边界处使用, 以确保激活后的第一条指令不会被误判为需要跳过。
 * 同时清空所有等待退休的 MMIO skip 标记。
 */
void difftest_dut_clearSkipRef();

/**
 * @brief DiffTest dut: 将 DUT 的 PSRAM/SDRAM 内存内容同步到 REF。
 *
 * 在 difftest 激活边界处使用, 以确保 REF 在开始比较前具有与 DUT 相同的
 * 内存内容（bootloader 可能已将 payload 加载到这些区域中）。
 */
void difftest_dut_syncPayloadMemoryToRef();


/**
 * @brief 将 payload 二进制文件加载到 DUT 的 C++ 后备存储 (psram_io_base 或 sdram_io_base).
 *
 * Verilog PSRAM/SDRAM 行为模型不会通过 DPI 回调更新 C++ 侧的缓冲,
 * 因此必须在 difftest activation 同步之前将 payload 显式加载到该缓冲中,
 * 才能使 syncPayloadMemoryToRef() 将正确数据复制到 REF.
 *
 * @param binFilePath  Payload 二进制文件的路径; 为空时直接返回 true.
 * @param loadAddr     Payload 在内存中的加载起始地址 (PSRAM: 0x80000000 或 SDRAM: 0xa0000000).
 * @return true  加载成功.
 * @return false 加载失败 (文件不存在, 过大, 或地址无效).
 */
bool difftest_dut_loadPayloadToBackingStore(const char *binFilePath, addr_t loadAddr);

#endif /* __DIFFTEST__DUT_HPP__ */
