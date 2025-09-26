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
    bool trig = top->ioDPI_idu_inst_jal;
    // 检测是否触发该指令，未触发则不执行操作
    if (!trig) {
        return;
    }

    if (sim_config.config_ftrace) {
        word_t imm;
        uint8_t rd;
        addr_t pc, destAddr;
        imm = top->ioDPI_idu_imm;
        rd = top->ioDPI_idu_rd;
        pc = top->ioDPI_core_pc;
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
    bool trig = top->ioDPI_idu_inst_jalr;
    // 检测是否触发该指令，未触发则不执行操作
    if (!trig) {
        return;
    }

    if (sim_config.config_ftrace) {
        word_t src1, imm;
        uint8_t rd, rs1;
        addr_t pc, destAddr;
        src1 = top->ioDPI_idu_rs1Data;
        imm = top->ioDPI_idu_imm;
        rd = top->ioDPI_idu_rd;
        rs1 = top->ioDPI_idu_rs1;
        pc = top->ioDPI_core_pc;
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

    bool memWriteEnable = top->ioDPI_physicalRAM_write_writeEnable;
    if (!memWriteEnable) {
        return;
    }

    if (sim_config.config_debugOutput)
        std::cout << "[sim] 处理器置写使能，将向主存写入数据..." << std::endl;

    addr = top->ioDPI_physicalRAM_write_writeAddress;
    if (addr >= MEMORY_OFFSET && addr < MEMORY_OFFSET + MEMORY_SIZE) {
        data = top->ioDPI_physicalRAM_write_writeData;
        if (sim_config.config_debugOutput)
            std::cout << "地址: 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << addr <<
                ", 数据: 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << data << std::endl;
        size_t len = dataStrobeToSize(top->ioDPI_physicalRAM_write_writeDataStrobe);
        if (sim_config.config_debugOutput)
            std::cout << "[sim] 长度: " << std::dec << len << std::endl;
        writeMemory(addr, len, data);

        if (sim_config.config_mtrace) {
            std::string mtraceContent = std::format(
                "0x{:08x}: Memory write at 0x{:08x}, len {}, data 0x{:08x}",
                top->ioDPI_core_pc, addr, len, data
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

extern "C" word_t dpi_onMemReadEnable(bool _memReadEnable) {
    addr_t addr;
    word_t data, readData;
    
    bool memReadEnable = top->ioDPI_physicalRAM_read_readEnable;

    if (!memReadEnable) {
        return 0;
    }

    if (sim_config.config_debugOutput)
        std::cout << "[sim] 处理器置读使能，将从主存读取数据..." << std::endl;

    addr = top->ioDPI_physicalRAM_read_readAddress;
    if (sim_config.config_debugOutput)
        std::cout << "[sim] 地址: 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << addr << std::endl;
    readData = 0;
    if (addr >= MEMORY_OFFSET && addr < MEMORY_OFFSET + MEMORY_SIZE) {
        size_t len = 4;
        data = readMemory(addr, len);
        if (sim_config.config_debugOutput) {
            std::cout << "[sim] 数据: 0x" << std::setfill('0') <<
                std::setw(8) << std::hex << data << std::endl;
            std::cout << "[sim] 长度: " << std::dec << len << std::endl;
        }
        readData = data;

        if (sim_config.config_mtrace) {
            std::string mtraceContent = std::format(
                "0x{:08x}: Memory read at 0x{:08x}, len {}, data 0x{:08x}",
                top->ioDPI_core_pc, addr, len, data
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
        readData = 0;
    }

    top->ioDPI_physicalRAM_read_readData = readData;
    return readData;
}

extern "C" void dpi_onEcallEnable(bool _ecallEnable) {
    bool ecallEnable = top->ioDPI_exu_ecallEnable;
    if (ecallEnable && sim_config.config_etrace) {
        // 记录 etrace
        std::string message = std::format("0x{:08x}: ecall detected", top->ioDPI_core_pc);
        sim_state.etrace_ofs << message << std::endl;
        if (sim_config.config_debugOutput) {
            std::cout << "[sim] etrace: " << message << std::endl;
        }
    }
}

extern "C" void dpi_onPosEdge_ifuInputValid(bool _ifuInputValid) {
    bool ifuInputValid = top->ioDPI_core_ifuInputValid;

    if (ifuInputValid && sim_config.config_debugOutput) {
        std::cout << "[sim] ifuInputValid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_if_nextStage_valid(bool _if_nextStage_valid) {
    bool if_nextStage_valid = top->ioDPI_ifu_if_nextStage_valid;

    if (if_nextStage_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] if_nextStage_valid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_id_nextStage_valid(bool _id_nextStage_valid) {
    bool id_nextStage_valid = top->ioDPI_idu_id_nextStage_valid;

    if (id_nextStage_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] id_nextStage_valid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_ex_nextStage_valid(bool _ex_nextStage_valid) {
    bool ex_nextStage_valid = top->ioDPI_exu_ex_nextStage_valid;

    if (ex_nextStage_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] ex_nextStage_valid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_ma_nextStage_valid(bool _ma_nextStage_valid) {
    bool ma_nextStage_valid = top->ioDPI_mau_ma_nextStage_valid;

    if (ma_nextStage_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] ma_nextStage_valid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_wb_nextStage_valid(bool _wb_nextStage_valid) {
    bool wb_nextStage_valid = top->ioDPI_wbu_wb_nextStage_valid;

    if (wb_nextStage_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] wb_nextStage_valid posedge detected." << std::endl;
    }
}

extern "C" void dpi_onPosEdge_upcu_pcOutput_valid(bool _upcu_pcOutput_valid) {
    bool upcu_pcOutput_valid = top->ioDPI_upcu_upcu_pcOutput_valid;

    if (upcu_pcOutput_valid && sim_config.config_debugOutput) {
        std::cout << "[sim] upcu_pcOutput_valid posedge detected." << std::endl;
    }
}

extern "C" word_t dpi_uart_onReadEnable(bool _uart_read_readEnable) {
    bool uart_read_readEnable = top->ioDPI_uart_read_readEnable;
    if (!uart_read_readEnable) {
        return 0;
    }

    if (sim_config.config_debugOutput) {
        std::cout << "[sim] read from UART..." << std::endl;
    }

    word_t result = 0xDEADBEEF;
    top->ioDPI_uart_read_readData = result;
    return result;
}

extern "C" void dpi_uart_onWriteEnable(bool _uart_write_writeEnable) {
    bool uart_write_writeEnable = top->ioDPI_uart_write_writeEnable;
    if (!uart_write_writeEnable) {
        return;
    }

    if (sim_config.config_debugOutput) {
        std::cout << "[sim] write to UART..." << std::endl;
    }

    word_t data = top->ioDPI_uart_write_writeData;
    char c = static_cast<char>(data & 0xFF);
    std::cerr << c;
    std::flush(std::cerr);
}
