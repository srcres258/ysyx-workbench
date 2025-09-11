#include <iostream>
#include <iomanip>
#include <cstdint>
#include <format>
#include <print>
#include <sim_top.hpp>
#include <memory.hpp>
#include <utils.hpp>
#include <utils/Stage.hpp>

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
    bool trig = top->ioDPI_inst_jal;
    // 检测是否触发该指令，未触发则不执行操作
    if (!trig) {
        return;
    }

    if (sim_config.config_ftrace) {
        word_t imm;
        uint8_t rd;
        addr_t pc, destAddr;
        imm = top->ioDPI_imm;
        rd = top->ioDPI_rd;
        pc = top->io_pc;
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
    bool trig = top->ioDPI_inst_jalr;
    // 检测是否触发该指令，未触发则不执行操作
    if (!trig) {
        return;
    }

    if (sim_config.config_ftrace) {
        word_t src1, imm;
        uint8_t rd, rs1;
        addr_t pc, destAddr;
        src1 = top->ioDPI_rs1Data;
        imm = top->ioDPI_imm;
        rd = top->ioDPI_rd;
        rs1 = top->ioDPI_rs1;
        pc = top->io_pc;
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

static size_t dataStrobeToSize(uint8_t dataStrobe) {
    switch (dataStrobe & 0b1111) {
        case 0b0001:
            return 1;
        case 0b0011:
            return 2;
        case 0b1111:
            return 4;
    }

    return 0;
}

extern "C" void dpi_onMemWriteEnable(bool _memWriteEnable) {
    addr_t addr;
    word_t data;

    bool memWriteEnable = top->io_writeEnable;
    if (!memWriteEnable) {
        return;
    }

    if (sim_config.config_debugOutput)
        std::cout << "[sim] 处理器置写使能，将向主存写入数据..." << std::endl;

    addr = top->io_address;
    if (addr >= MEMORY_OFFSET && addr < MEMORY_OFFSET + MEMORY_SIZE) {
        data = top->io_writeData;
        if (sim_config.config_debugOutput)
            std::cout << "地址: 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << addr <<
                ", 数据: 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << data << std::endl;
        size_t len = dataStrobeToSize(top->io_dataStrobe);
        if (sim_config.config_debugOutput)
            std::cout << "[sim] 长度: " << std::dec << len << std::endl;
        writeMemory(addr, len, data);

        if (sim_config.config_mtrace) {
            std::string mtraceContent = std::format(
                "0x{:08x}: Memory write at 0x{:08x}, len {}, data 0x{:08x}",
                top->io_pc, addr, len, data
            );
            sim_state.mtrace_ofs << mtraceContent << std::endl;
            std::flush(sim_state.mtrace_ofs);
            if (sim_config.config_debugOutput)
                std::cout << "[sim] mtrace: " << mtraceContent << std::endl;
        }
    } else {
        if (sim_config.config_debugOutput)
            std::cerr << "[sim] 地址 0x" << std::setfill('0') << std::setw(8) << std::hex
                << addr << " 尚未初始化，跳过..." << std::endl;
    }
}

extern "C" void dpi_onMemReadEnable(bool _memReadEnable) {
    addr_t addr;
    word_t data;
    
    bool memReadEnable = top->io_readEnable;

    if (!memReadEnable) {
        return;
    }

    if (sim_config.config_debugOutput)
        std::cout << "[sim] 处理器置读使能，将从主存读取数据..." << std::endl;

    addr = top->io_address;
    if (sim_config.config_debugOutput)
        std::cout << "[sim] 地址: 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << addr << std::endl;
    if (addr >= MEMORY_OFFSET && addr < MEMORY_OFFSET + MEMORY_SIZE) {
        size_t len = dataStrobeToSize(top->io_dataStrobe);
        data = readMemory(addr, len);
        if (sim_config.config_debugOutput) {
            std::cout << "[sim] 数据: 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << data << std::endl;
            std::cout << "[sim] 长度: " << std::dec << len << std::endl;
        }
        top->io_readData = data;

        if (sim_config.config_mtrace) {
            std::string mtraceContent = std::format(
                "0x{:08x}: Memory read at 0x{:08x}, len {}, data 0x{:08x}",
                top->io_pc, addr, len, data
            );
            sim_state.mtrace_ofs << mtraceContent << std::endl;
            std::flush(sim_state.mtrace_ofs);
            if (sim_config.config_debugOutput)
                std::cout << "[sim] mtrace: " << mtraceContent << std::endl;
        }
    } else {
        if (sim_config.config_debugOutput)
            std::cerr << "[sim] 地址 0x" << std::setfill('0') << std::setw(8) << std::hex
                << addr << " 尚未初始化，跳过..." << std::endl;
        top->io_readData = 0;
    }
}

extern "C" void dpi_onStage(uint8_t _stage) {
    uint8_t stage = top->ioDPI_stage;

    if (sim_config.config_debugOutput) {
        std::cout << "[sim] 处理器当前阶段: ";
        switch (stage & 0b111) {
            case STAGE_IF:
                std::cout << "IF" << std::endl;
                break;
            case STAGE_ID:
                std::cout << "ID" << std::endl;
                break;
            case STAGE_EX:
                std::cout << "EX" << std::endl;
                break;
            case STAGE_MA:
                std::cout << "MA" << std::endl;
                break;
            case STAGE_WB:
                std::cout << "WB" << std::endl;
                break;
            case STAGE_UPC:
                std::cout << "UPC" << std::endl;
                break;
            default:
                std::cout << "Unknown" << std::endl;
        }
    }
}

extern "C" void dpi_onEcallEnable(bool _ecallEnable) {
    bool ecallEnable = top->ioDPI_ecallEnable;
    if (ecallEnable && sim_config.config_etrace) {
        // 记录 etrace
        std::string message = std::format("0x{:08x}: ecall detected", top->io_pc);
        sim_state.etrace_ofs << message << std::endl;
        if (sim_config.config_debugOutput) {
            std::cout << "[sim] etrace: " << message << std::endl;
        }
    }
}
