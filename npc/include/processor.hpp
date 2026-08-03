#ifndef __PROCESSOR_HPP__
#define __PROCESSOR_HPP__ 1

#include <common.hpp>
#include <macro-def.hpp>
#include <cstddef>

/**
 * @brief 处理器状态。(主要用于描述处理器寄存器的状态)
 *
 * 此结构的二进制布局（ABI）与 NEMU riscv32_CPU_state 完全兼容，用于 DiffTest
 * 的 memcpy 同步。以下 static_assert 在编译时强制执行此约定；若布局发生偏差，编译将失败。
 */
extern "C" struct ProcessorState {
    /**
     * @brief 通用寄存器。
     */
    word_t gpr[RISCV_GPR_NUM];
    /**
     * @brief 程序计数器。
     */
    addr_t pc;
    /**
     * @brief 控制与状态寄存器。
     */
    word_t csr[RISCV_CSR_NUM];

    /**
     * @brief 将处理器状态进行可读化输出。
     */
    void dump() const;
};

static_assert(
    sizeof(ProcessorState) == sizeof(word_t) * RISCV_GPR_NUM + sizeof(addr_t) + sizeof(word_t) * RISCV_CSR_NUM,
     "ProcessorState size mismatch: DiffTest ABI broken"
    );
static_assert(
    offsetof(ProcessorState, gpr) == 0,
    "ProcessorState gpr offset mismatch: DiffTest ABI broken"
);
static_assert(
    offsetof(ProcessorState, pc) == sizeof(word_t) * RISCV_GPR_NUM,
    "ProcessorState pc offset mismatch: DiffTest ABI broken"
);
static_assert(
    offsetof(ProcessorState, csr) == sizeof(word_t) * RISCV_GPR_NUM + sizeof(addr_t),
    "ProcessorState csr offset mismatch: DiffTest ABI broken"
);

/**
 * @brief 获取当前仿真环境的处理器状态。
 * 
 * @return ProcessorState 处理器状态
 */
ProcessorState getProcessorState();

#endif /* __PROCESSOR_HPP__ */
