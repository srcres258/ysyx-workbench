#include <iostream>
#include <iomanip>
#include <cstdint>
#include <format>
#include <print>
#include <sim_top.hpp>
#include <memory.hpp>
#include <dpi.hpp>
#include <utils.hpp>

extern "C" void dpi_halt(bool halt) {
    sim_halt = halt;

    if (sim_halt) {
        std::cout << "[sim] 仿真环境置仿真终止信号，处理器下一次执行前将结束仿真！" << std::endl;
    }
}

extern "C" void dpi_onAddressUpdate(uint32_t _address) {
    uint32_t addr, data;

    std::cout << "[sim] 仿真环境检测到处理器寻址地址发生改变，正在从内存中读数据并提供给处理器..." << std::endl;

    addr = top->io_address;
    std::cout << "[sim] 地址: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << addr << std::endl;
    if (addr >= MEMORY_OFFSET && addr < MEMORY_OFFSET + MEMORY_SIZE) {
        data = readMemory(addr);
        std::cout << "[sim] 数据: 0x" << std::setfill('0') <<
            std::setw(8) << std::hex << data << std::endl;
        top->io_readData = data;
    } else {
        std::cerr << "[sim] 地址尚未初始化，置缺省值..." << std::endl;
        top->io_readData = 0;
    }
}

extern "C" void dpi_onLSTypeUpdate(uint8_t _lsType) {
    uint8_t lsType;
    bool memoryAccessed;
    std::string lsTypeStr, mtraceContent;

    std::cout << "[sim] 仿真环境检测到处理器访存类型发生改变，将记录 mtrace ..." << std::endl;
    lsType = top->ioDPI_lsType;

    switch (lsType) {
        case LS_L_W:
        case LS_L_H:
        case LS_L_HU:
        case LS_L_B:
        case LS_L_BU:
            lsTypeStr = "load";
            memoryAccessed = true;
            break;
        case LS_S_W:
        case LS_S_H:
        case LS_S_B:
            lsTypeStr = "store";
            memoryAccessed = true;
            break;
        default:
            memoryAccessed = false;
    }

    if (memoryAccessed) {
        size_t len;
        /*
        B: 字节 (1 byte )
        H: 半字 (2 bytes)
        W：字   (4 bytes)
        */
        len = 0;
        switch (lsType) {
            case LS_L_B:
            case LS_L_BU:
            case LS_S_B:
                len = 1;
                break;
            case LS_L_H:
            case LS_L_HU:
            case LS_S_H:
                len = 2;
                break;
            case LS_L_W:
            case LS_S_W:
                len = 4;
        }

        addr_t addr = top->io_address;
        word_t data;
        if (lsTypeStr == "store") {
            data = top->io_writeData;
        } else {
            data = readMemory(addr);
        }
        mtraceContent = std::format(
            "0x{:08x}: Memory {} at 0x{:08x}, len {}, data 0x{:08x}",
            top->ioDPI_pc, lsTypeStr, addr, len, data
        );

        if (sim_config.config_mtrace) {
            sim_state.mtrace_ofs << mtraceContent << std::endl;
            std::flush(sim_state.mtrace_ofs);
        }

        std::cout << "[sim] mtrace: " << mtraceContent << std::endl;
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
        std::cout << "[sim] ftrace: Detect succeeded." << std::endl;
    } else {
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

    word_t imm;
    uint8_t rd;
    addr_t pc, destAddr;
    imm = top->ioDPI_imm;
    rd = top->ioDPI_rd;
    pc = top->ioDPI_pc;
    destAddr = pc + imm;

    if (rd == 1) {
        // 情况：rd 为 x1
        // 推测：该 jal 指令可能来源于 call 伪指令
        std::println(
            "[sim] ftrace: Detected call from jal at pc = {:#08x}, "
                "dest_addr = {:#08x}",
            pc, destAddr
        );
        tryRecord(CALL_TYPE_CALL, pc, destAddr);
    }
}

extern "C" void dpi_onInst_jalr(bool _trig) {
    bool trig = top->ioDPI_inst_jalr;
    // 检测是否触发该指令，未触发则不执行操作
    if (!trig) {
        return;
    }

    word_t src1, imm;
    uint8_t rd, rs1;
    addr_t pc, destAddr;
    src1 = top->ioDPI_rs1Data;
    imm = top->ioDPI_imm;
    rd = top->ioDPI_rd;
    rs1 = top->ioDPI_rs1;
    pc = top->ioDPI_pc;
    destAddr = src1 + imm;

    if (isAddrFuncSymStart(destAddr) || (rd == 1 && rs1 == 1)) {
        // 情况：1. 目的地址是函数起始地址 
        //    或2. rd 为 x1， rs1 为 x1
        // 推测：该 jalr 指令可能来源于 call 伪指令
        // 情况：rd 为 x1
        // 推测：该 jal 指令可能来源于 call 伪指令
        std::println(
            "[sim] ftrace: Detected call from jalr at pc = {:#08x}, "
                "dest_addr = {:#08x}",
            pc, destAddr
        );
        tryRecord(CALL_TYPE_CALL, pc, destAddr);
    } else if (rd == 0 && rs1 == 1) {
        // 情况：rd 为 x0， rs1 为 x1
        // 推测：该 jalr 指令可能来源于 ret 伪指令
        std::println(
            "[sim] ftrace: Detected ret from jalr at pc = {:#08x}, "
                "dest_addr = {:#08x}",
            pc, destAddr
        );
        tryRecord(CALL_TYPE_RET, pc, destAddr);
    } else if (rd == 1 && (rs1 == 6 || rs1 == 7)) {
        // 情况：rd 为 x1， rs1 为 x6 或 x7
        // 推测：该 jalr 指令可能来源于 tail 伪指令
        std::println(
            "[sim] ftrace: Detected tail from jalr at pc = {:#08x}, "
                "dest_addr = {:#08x}",
            pc, destAddr
        );
        tryRecord(CALL_TYPE_TAIL, pc, destAddr);
    }
}
