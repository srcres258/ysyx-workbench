#include <iostream>
#include <iomanip>
#include <format>
#include <utility>
#include <sim_top.hpp>
#include <isa.hpp>
#include <processor.hpp>

void ProcessorState::dump() const {
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

    std::cout << "CSRs:" << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "mstatus" <<
        ": 0x" << std::setfill('0') << std::setw(8) <<
        std::hex << csr[CSR_MSTATUS] << std::dec << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "mtvec" <<
        ": 0x" << std::setfill('0') << std::setw(8) <<
        std::hex << csr[CSR_MTVEC] << std::dec << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "mepc" <<
        ": 0x" << std::setfill('0') << std::setw(8) <<
        std::hex << csr[CSR_MEPC] << std::dec << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "mcause" <<
        ": 0x" << std::setfill('0') << std::setw(8) <<
        std::hex << csr[CSR_MCAUSE] << std::dec << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "mtval" <<
        ": 0x" << std::setfill('0') << std::setw(8) <<
        std::hex << csr[CSR_MTVAL] << std::dec << std::endl;
}

/**
 * @brief 获取当前仿真环境的处理器状态。
 * 
 * @return ProcessorState 处理器状态
 */
ProcessorState getProcessorState() {
    ProcessorState state = {
        .gpr = {
            top->ioDPI_gprs_0,
            top->ioDPI_gprs_1,
            top->ioDPI_gprs_2,
            top->ioDPI_gprs_3,
            top->ioDPI_gprs_4,
            top->ioDPI_gprs_5,
            top->ioDPI_gprs_6,
            top->ioDPI_gprs_7,
            top->ioDPI_gprs_8,
            top->ioDPI_gprs_9,
            top->ioDPI_gprs_10,
            top->ioDPI_gprs_11,
            top->ioDPI_gprs_12,
            top->ioDPI_gprs_13,
            top->ioDPI_gprs_14,
            top->ioDPI_gprs_15,
#ifndef CONFIG_RVE
            top->ioDPI_gprs_16,
            top->ioDPI_gprs_17,
            top->ioDPI_gprs_18,
            top->ioDPI_gprs_19,
            top->ioDPI_gprs_20,
            top->ioDPI_gprs_21,
            top->ioDPI_gprs_22,
            top->ioDPI_gprs_23,
            top->ioDPI_gprs_24,
            top->ioDPI_gprs_25,
            top->ioDPI_gprs_26,
            top->ioDPI_gprs_27,
            top->ioDPI_gprs_28,
            top->ioDPI_gprs_29,
            top->ioDPI_gprs_30,
            top->ioDPI_gprs_31
#endif
        },
        .pc = top->io_pc,
        .csr = { 0 }
    };
    state.csr[CSR_MSTATUS] = top->ioDPI_csr_mstatus;
    state.csr[CSR_MTVEC] = top->ioDPI_csr_mtvec;
    state.csr[CSR_MEPC] = top->ioDPI_csr_mepc;
    state.csr[CSR_MCAUSE] = top->ioDPI_csr_mcause;
    state.csr[CSR_MTVAL] = top->ioDPI_csr_mtval;
    return std::move(state);
};
