#include <iostream>
#include <iomanip>
#include <format>
#include <utility>
#include <sim_top.hpp>
#include <processor.hpp>

void ProcessorState::dump() {
    int i;

    std::cout << "Registers:" << std::endl;
    for (i = 0; i < RISCV_GPR_NUM; i++) {
        auto regName = std::format("x{}", i);
        std::cout << std::setfill(' ') << std::setw(4) << regName <<
        ": 0x" << std::setfill('0') << std::setw(8) <<
        std::hex << gpr[i] << std::dec << std::endl;
    }

    std::cout << "PC is currently at 0x" << std::setfill('0') <<
        std::setw(8) << std::hex << pc << std::dec << std::endl;
}

/**
 * @brief 获取当前仿真环境的处理器状态。
 * 
 * @return ProcessorState 处理器状态
 */
ProcessorState getProcessorState() {
    ProcessorState state = {
        .gpr = {
            top->ioDPI_registers_0,
            top->ioDPI_registers_1,
            top->ioDPI_registers_2,
            top->ioDPI_registers_3,
            top->ioDPI_registers_4,
            top->ioDPI_registers_5,
            top->ioDPI_registers_6,
            top->ioDPI_registers_7,
            top->ioDPI_registers_8,
            top->ioDPI_registers_9,
            top->ioDPI_registers_10,
            top->ioDPI_registers_11,
            top->ioDPI_registers_12,
            top->ioDPI_registers_13,
            top->ioDPI_registers_14,
            top->ioDPI_registers_15,
#ifndef CONFIG_RVE
            top->ioDPI_registers_16,
            top->ioDPI_registers_17,
            top->ioDPI_registers_18,
            top->ioDPI_registers_19,
            top->ioDPI_registers_20,
            top->ioDPI_registers_21,
            top->ioDPI_registers_22,
            top->ioDPI_registers_23,
            top->ioDPI_registers_24,
            top->ioDPI_registers_25,
            top->ioDPI_registers_26,
            top->ioDPI_registers_27,
            top->ioDPI_registers_28,
            top->ioDPI_registers_29,
            top->ioDPI_registers_30,
            top->ioDPI_registers_31
#endif
        },
        .pc = top->io_pc
    };
    return std::move(state);
};
