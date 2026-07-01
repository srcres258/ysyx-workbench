#include <iostream>
#include <iomanip>
#include <cstdint>
#include <format>
#include <print>
#include <svdpi.h>
#include <sim_top.hpp>
#include <utils.hpp>
#include <utils/Stage.hpp>
#include <utils/timer.hpp>
#include <device/mrom.hpp>
#include <device/flash.hpp>
#include <device/psram.hpp>
#include <macro-def.hpp>

static auto *dpi() {
    return getDPIModule();
}

extern "C" void dpi_halt(bool halt) {
    sim_halt = halt;

    if (sim_halt) {
        if (sim_config.config_debugOutput)
            std::cout << "[sim] 仿真环境置仿真终止信号，处理器下一次执行前将结束仿真！" << std::endl;
    }
}

static bool isAddrFuncSymStart(addr_t addr) {
    for (const auto &sym : sim_state.ftrace_funcSyms) {
        if (addr == sym.addr) {
            return true;
        }
    }

    return false;
}

static bool tryRecord(CallType type, addr_t pc, addr_t destAddr) {
    bool result;
    
    result = ftrace_tryRecord(type, pc, destAddr);
    if (result) {
        if (sim_config.config_debugOutput)
            std::cout << "[sim] ftrace: Detect succeeded." << std::endl;
    } else {
        if (sim_config.config_debugOutput)
            std::cout << "[sim] ftrace: Detect failed." << std::endl;
    }

    return result;
}

extern "C" void dpi_onInst_jal(bool _trig) {
    auto *dpi = getDPIModule();
    bool trig = dpi->idu_inst_jal;
    // 检测是否触发该指令，未触发则不执行操作
    if (!trig) {
        return;
    }

    if (sim_config.config_ftrace) {
        word_t imm;
        uint8_t rd;
        addr_t pc, destAddr;
        imm = dpi->idu_imm;
        rd = dpi->idu_rd;
        pc = dpi->core_pc;
        destAddr = pc + imm;

        if (rd == 1) {
            // 情况：rd 为 x1
            // 推测：该 jal 指令可能来源于 call 伪指令
            if (sim_config.config_debugOutput)
                std::println(
                    "[sim] ftrace: Detected call from jal at pc = {:#08x}, "
                        "dest_addr = {:#08x}",
                    pc, destAddr
                );
            tryRecord(CALL_TYPE_CALL, pc, destAddr);
        }
    }
}

extern "C" void dpi_onInst_jalr(bool _trig) {
    auto *dpi = getDPIModule();
    bool trig = dpi->idu_inst_jalr;
    // 检测是否触发该指令，未触发则不执行操作
    if (!trig) {
        return;
    }

    if (sim_config.config_ftrace) {
        word_t src1, imm;
        uint8_t rd, rs1;
        addr_t pc, destAddr;
        src1 = dpi->idu_rs1Data;
        imm = dpi->idu_imm;
        rd = dpi->idu_rd;
        rs1 = dpi->idu_rs1;
        pc = dpi->core_pc;
        destAddr = src1 + imm;

        if (isAddrFuncSymStart(destAddr) || (rd == 1 && rs1 == 1)) {
            // 情况：1. 目的地址是函数起始地址 
            //    或2. rd 为 x1， rs1 为 x1
            // 推测：该 jalr 指令可能来源于 call 伪指令
            // 情况：rd 为 x1
            // 推测：该 jal 指令可能来源于 call 伪指令
            if (sim_config.config_debugOutput)
                std::println(
                    "[sim] ftrace: Detected call from jalr at pc = {:#08x}, "
                        "dest_addr = {:#08x}",
                    pc, destAddr
                );
            tryRecord(CALL_TYPE_CALL, pc, destAddr);
        } else if (rd == 0 && rs1 == 1) {
            // 情况：rd 为 x0， rs1 为 x1
            // 推测：该 jalr 指令可能来源于 ret 伪指令
            if (sim_config.config_debugOutput)
                std::println(
                    "[sim] ftrace: Detected ret from jalr at pc = {:#08x}, "
                        "dest_addr = {:#08x}",
                    pc, destAddr
                );
            tryRecord(CALL_TYPE_RET, pc, destAddr);
        } else if (rd == 1 && (rs1 == 6 || rs1 == 7)) {
            // 情况：rd 为 x1， rs1 为 x6 或 x7
            // 推测：该 jalr 指令可能来源于 tail 伪指令
            if (sim_config.config_debugOutput)
                std::println(
                    "[sim] ftrace: Detected tail from jalr at pc = {:#08x}, "
                        "dest_addr = {:#08x}",
                    pc, destAddr
                );
            tryRecord(CALL_TYPE_TAIL, pc, destAddr);
        }
    }
}

static size_t lsTypeToSize(uint8_t lsType) {
    switch (lsType) {
        case 0: // LS_L_W
        case 5: // LS_S_W
            return 4;
        case 1: // LS_L_H
        case 2: // LS_L_HU
        case 6: // LS_S_H
            return 2;
        case 3: // LS_L_B
        case 4: // LS_L_BU
        case 7: // LS_S_B
            return 1;
    }

    return 4;
}

static inline addr_t dpiLogicVecToAddr(const svLogicVecVal *value) {
    return static_cast<addr_t>(value[0].aval);
}

static inline uint8_t dpiLogicVecToU8(const svLogicVecVal *value, uint8_t mask) {
    return static_cast<uint8_t>(value[0].aval & mask);
}

extern "C" void dpi_onMemAccess(
    const svLogicVecVal *pc, svLogic memWriteEnable, svLogic memReadEnable,
    const svLogicVecVal *memAddr, const svLogicVecVal *memData,
    const svLogicVecVal *memStrobe, const svLogicVecVal *memResp,
    const svLogicVecVal *memLsType
) {
    if (memWriteEnable != sv_1 && memReadEnable != sv_1) {
        return;
    }

    const bool isWrite = memWriteEnable == sv_1;
    const int len = (int) lsTypeToSize(dpiLogicVecToU8(memLsType, 0x0f));
    trace_record_mtrace(
        dpiLogicVecToAddr(pc),
        isWrite,
        dpiLogicVecToAddr(memAddr),
        len,
        dpiLogicVecToAddr(memData),
        dpiLogicVecToU8(memStrobe, 0x0f),
        dpiLogicVecToU8(memResp, 0x03)
    );
}

extern "C" void dpi_onEcallEnable(const svLogicVecVal *pc, svLogic ecallEnable) {
    if (ecallEnable != sv_1 || !sim_config.config_etrace) {
        return;
    }

    auto *dpi = getDPIModule();
    trace_record_etrace(
        dpiLogicVecToAddr(pc),
        "exception",
        11,
        dpiLogicVecToAddr(pc),
        0,
        dpi->csr_csr_mtvec
    );
}

extern "C" void dpi_onEpcRecoverEnable(const svLogicVecVal *pc, svLogic epcRecoverEnable) {
    if (epcRecoverEnable != sv_1 || !sim_config.config_etrace) {
        return;
    }

    auto *dpi = getDPIModule();
    trace_record_etrace(
        dpiLogicVecToAddr(pc),
        "return",
        0,
        dpi->csr_csr_mepc,
        0,
        dpi->csr_csr_mepc
    );
}

extern "C" void dpi_onPosEdge_ifuInputValid(bool _ifuInputValid) {
    bool ifuInputValid = dpi()->core_ifuInputValid;

    if (ifuInputValid && sim_config.config_debugOutput) {
        std::cout << "[sim] ifuInputValid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_if_nextStage_valid(bool _if_nextStage_valid) {
    bool if_nextStage_valid = dpi()->ifu_if_nextStage_valid;

    if (if_nextStage_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] if_nextStage_valid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_id_nextStage_valid(bool _id_nextStage_valid) {
    bool id_nextStage_valid = dpi()->idu_id_nextStage_valid;

    if (id_nextStage_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] id_nextStage_valid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_ex_nextStage_valid(bool _ex_nextStage_valid) {
    bool ex_nextStage_valid = dpi()->exu_ex_nextStage_valid;

    if (ex_nextStage_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] ex_nextStage_valid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_mem_nextStage_valid(bool _mem_nextStage_valid) {
    bool mem_nextStage_valid = dpi()->memu_mem_nextStage_valid;

    if (mem_nextStage_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] mem_nextStage_valid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_wb_nextStage_valid(bool _wb_nextStage_valid) {
    bool wb_nextStage_valid = dpi()->wbu_wb_nextStage_valid;

    if (wb_nextStage_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] wb_nextStage_valid posedge detected." << std::endl;
    }
}

extern "C" word_t dpi_clint_onReadEnable(bool _clint_read_readEnable) {
    auto *dpi = getDPIModule();
    bool clint_read_readEnable = dpi->clint_read_readEnable;
    if (!clint_read_readEnable) {
        return 0;
    }

    if (sim_config.config_debugOutput) {
        std::cout << "[sim] read from CLINT..." << std::endl;
    }

    uint64_t us = timer_getTimeElapsedUSec();
    uint32_t result = 0;
    addr_t addr = dpi->clint_read_readAddress;
    if (addr == 0x0200bff8) {
        result = (uint32_t) us;
    } else if (addr == 0x0200bffc) {
        result = (uint32_t) (us >> 32);
    }
    return result;
}

extern "C" void dpi_clint_onWriteEnable(bool _clint_write_writeEnable) {
    bool clint_write_writeEnable = dpi()->clint_write_writeEnable;
    if (!clint_write_writeEnable) {
        return;
    }

    if (sim_config.config_debugOutput) {
        std::cout << "[sim] write to CLINT..." << std::endl;
    }
}

extern "C" void flash_read(addr_t addr, word_t *data) {
    addr_t realAddr = FLASH_ADDR + addr;
    if (sim_config.config_debugOutput) {
        std::string message = std::format(
            "[sim] read from FLASH, addr = 0x{:08x}, realAddr = 0x{:08x}",
            addr, realAddr
        );
        std::cout << message << std::endl;
    }
    *data = device_flash_read(realAddr, 4);
    if (sim_config.config_debugOutput) {
        std::string message = std::format("[sim] read from FLASH, data = 0x{:08x}", *data);
        std::cout << message << std::endl;
    }
}

extern "C" void mrom_read(addr_t addr, word_t *data) {
    if (sim_config.config_debugOutput) {
        std::string message = std::format("[sim] read from MROM, addr = 0x{:08x}", addr);
        std::cout << message << std::endl;
    }
    *data = device_mrom_read(addr, 4);
    if (sim_config.config_debugOutput) {
        std::string message = std::format("[sim] read from MROM, data = 0x{:08x}", *data);
        std::cout << message << std::endl;
    }
}

extern "C" void psram_read(uint32_t addr, uint8_t *data) {
    addr_t realAddr = PSRAM_ADDR + addr;
    *data = uint8_t(device_psram_read(realAddr, 1));
}

extern "C" void psram_write(uint32_t addr, uint8_t data) {
    addr_t realAddr = PSRAM_ADDR + addr;
    device_psram_write(realAddr, 1, data);
}
