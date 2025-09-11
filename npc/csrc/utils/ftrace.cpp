#include <iostream>
#include <iomanip>
#include <format>
#include <utils.hpp>

/**
 * @brief ftrace: 在符号表中查询指定内存地址处的符号。
 * 
 * @param dest 输出目的字符串缓冲区
 * @param addr 内存地址
 * @return true 查询成功
 * @return false 查询失败
 */
bool ftrace_queryNameThroughSymbolTable(
    std::string &dest, addr_t addr
) {
    for (const auto &sym : sim_state.ftrace_funcSyms) {
        if (sym.addr <= addr && addr < sym.addr + sym.size) {
            dest = sym.name;
            return true;
        }
    }

    return false;
}

/**
 * @brief ftrace: 尝试记录到指定内存地址处的函数调用信息。
 * 
 * @param type 函数调用类型
 * @param srcAddr 函数调用的源起地址
 * @param addr 函数调用的目标地址
 * @return true 记录成功（找到目标函数的符号信息）
 * @return false 记录失败（未找到目标函数信息，无法记录）
 */
bool ftrace_tryRecord(
    CallType type, addr_t srcAddr, addr_t addr
) {
    std::string funcName, destFuncName, message, tmpStr;
    size_t i;

    if (type == CALL_TYPE_CALL) {
        /* call 到函数的调用 */
        if (!ftrace_queryNameThroughSymbolTable(funcName, addr)) {
            return false;
        }

        // 记录入栈信息：调用至目的函数
        message = std::format("0x{:08x}: ", srcAddr);
        for (i = 0; i < sim_state.ftrace_callStack.size(); i++) {
            message += "  ";
        }
        tmpStr = std::format(
            "call to [{}@0x{:08x}]",
            funcName, addr
        );
        message += tmpStr;
        sim_state.ftrace_ofs << message << std::endl;
        std::flush(sim_state.ftrace_ofs);
        if (sim_config.config_debugOutput)
            std::cout << "[sim] ftrace: " << message << std::endl;

        // 将该函数入栈
        CallStackInfo info;
        info.addr = addr;
        info.name = funcName;
        sim_state.ftrace_callStack.push(std::move(info));

        return true;
    }

    if (type == CALL_TYPE_TAIL) {
        /* tail 从当前函数进行尾调用到另一个函数 */
        if (!ftrace_queryNameThroughSymbolTable(funcName, srcAddr)) {
            return false;
        }
        if (!ftrace_queryNameThroughSymbolTable(destFuncName, addr)) {
            return false;
        }

        // 先将当前函数出栈
        // 【注意】由于编译器/汇编器可能进行尾调用消除优化，出栈时要出到目标函数层级
        // （可能需要出不止一层栈）
        while (sim_state.ftrace_callStack.size() > 0) {
            const auto &info = sim_state.ftrace_callStack.top();
            if (info.name == destFuncName) {
                break;
            }
            // 没到达目标层级，则继续出栈
            sim_state.ftrace_callStack.pop();
        }

        // 记录信息：尾调用至另一个函数
        message = std::format("0x{:08x}: ", srcAddr);
        for (i = 0; i < sim_state.ftrace_callStack.size(); i++) {
            message += "  ";
        }
        tmpStr = std::format(
            "tail from [{}@0x{:08x}] to [{}@0x{:08x}]",
            funcName, srcAddr, destFuncName, addr
        );
        message += tmpStr;
        sim_state.ftrace_ofs << message << std::endl;
        std::flush(sim_state.ftrace_ofs);
        if (sim_config.config_debugOutput)
            std::cout << "[sim] ftrace: " << message << std::endl;

        // 再将目的函数入栈
        CallStackInfo info;
        info.addr = addr;
        info.name = destFuncName;
        sim_state.ftrace_callStack.push(std::move(info));

        return true;
    }

    if (type == CALL_TYPE_RET) {
        /* ret 从当前函数返回 */
        if (!ftrace_queryNameThroughSymbolTable(funcName, srcAddr)) {
            return false;
        }
        if (!ftrace_queryNameThroughSymbolTable(destFuncName, addr)) {
            destFuncName = "<unknown>";
        }

        // 将当前函数出栈
        // 【注意】由于编译器/汇编器可能进行尾调用消除优化，出栈时要出到目标函数层级
        // （可能需要出不止一层栈）
        if (destFuncName == "<unknown>") {
            sim_state.ftrace_callStack.pop();
        } else {
            while (sim_state.ftrace_callStack.size() > 0) {
                const auto &info = sim_state.ftrace_callStack.top();
                if (info.name == destFuncName) {
                    break;
                }
                // 没到达目标层级，则继续出栈
                sim_state.ftrace_callStack.pop();
            }
        }

        // 记录出栈信息：从当前函数返回
        // 【注意】返回到的目的地址不是函数的起始地址，而是在函数体内部
        //        所以不方便记录目的函数信息，只能记录当前函数信息（从哪里返回）
        message = std::format("0x{:08x}: ", srcAddr);
        for (i = 0; i < sim_state.ftrace_callStack.size(); i++) {
            message += "  ";
        }
        tmpStr = std::format(
            "ret from [{}@0x{:08x}] to [{}@0x{:08x}]",
            funcName, srcAddr, destFuncName, addr
        );
        message += tmpStr;
        sim_state.ftrace_ofs << message << std::endl;
        std::flush(sim_state.ftrace_ofs);
        if (sim_config.config_debugOutput)
            std::cout << "[sim] ftrace: " << message << std::endl;

        return true;
    }

    return false;
}
