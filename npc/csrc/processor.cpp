#include <iostream>
#include <iomanip>
#include <format>
#include <utility>
#include <sim_top.hpp>
#include <isa.hpp>
#include <processor.hpp>
#include "npc/gpr_fields.hpp"

void ProcessorState::dump() const {
    int i;

    std::cout << "Registers:" << std::endl;
    for (i = 0; i < RISCV_GPR_NUM; i++) {
        auto regName = std::format("x{}", i);
        std::cout << std::setfill(' ') << std::setw(4) << regName << ": 0x"
            << std::setfill('0') << std::setw(8) << std::hex << gpr[i]
            << std::dec << std::endl;
    }

    std::cout << "PC is currently at 0x" << std::setfill('0') << std::setw(8)
        << std::hex << pc << std::dec << std::endl;

    std::cout << "CSRs:" << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "mstatus" << ": 0x"
        << std::setfill('0') << std::setw(8) << std::hex << csr[CSR_MSTATUS]
        << std::dec << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "mtvec" << ": 0x"
        << std::setfill('0') << std::setw(8) << std::hex << csr[CSR_MTVEC]
        << std::dec << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "mepc" << ": 0x"
        << std::setfill('0') << std::setw(8) << std::hex << csr[CSR_MEPC]
        << std::dec << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "mcause" << ": 0x"
        << std::setfill('0') << std::setw(8) << std::hex << csr[CSR_MCAUSE]
        << std::dec << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "mtval" << ": 0x"
        << std::setfill('0') << std::setw(8) << std::hex << csr[CSR_MTVAL]
        << std::dec << std::endl; std::cout << std::setfill(' ') << std::setw(4)
        << "mvendorid" << ": 0x" << std::setfill('0') << std::setw(8)
        << std::hex << csr[CSR_MVENDORID] << std::dec << std::endl;
    std::cout << std::setfill(' ') << std::setw(4) << "marchid" << ": 0x"
        << std::setfill('0') << std::setw(8) << std::hex << csr[CSR_MARCHID]
        << std::dec << std::endl;
}

/**
 * @brief 获取当前仿真环境的处理器状态。
 *
 * @return ProcessorState 处理器状态
 */
ProcessorState getProcessorState() {
    auto *dpi = getDPIModule();
#define NPC_GPR_FIELD(idx) dpi->gpr_gprs_##idx,
    ProcessorState state = {
        .gpr = {
            NPC_GPR_FIELDS_0_15(NPC_GPR_FIELD)
#ifndef CONFIG_RVE
            NPC_GPR_FIELDS_16_31(NPC_GPR_FIELD)
#endif
        },
        .pc = dpi->core_pc,
        .csr = { 0 }
    };
#undef NPC_GPR_FIELD
    state.csr[CSR_MSTATUS] = dpi->csr_csr_mstatus;
    state.csr[CSR_MTVEC] = dpi->csr_csr_mtvec;
    state.csr[CSR_MEPC] = dpi->csr_csr_mepc;
    state.csr[CSR_MCAUSE] = dpi->csr_csr_mcause;
    state.csr[CSR_MTVAL] = dpi->csr_csr_mtval;
    state.csr[CSR_MVENDORID] = dpi->csr_csr_mvendorid;
    state.csr[CSR_MARCHID] = dpi->csr_csr_marchid;
    return std::move(state);
};
