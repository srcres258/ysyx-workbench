#ifndef __PROCESSOR_HPP__
#define __PROCESSOR_HPP__ 1

#include <common.hpp>
#include <macro-def.hpp>

/**
 * @brief 处理器状态。(主要用于描述处理器寄存器的状态)
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

/**
 * @brief 获取当前仿真环境的处理器状态。
 * 
 * @return ProcessorState 处理器状态
 */
ProcessorState getProcessorState();

#endif /* __PROCESSOR_HPP__ */
