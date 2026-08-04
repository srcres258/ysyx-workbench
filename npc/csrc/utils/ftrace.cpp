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
    CallType type, addr_t srcAddr, addr_t addr, addr_t retAddr, word_t callerSp
) {
    std::string funcName, destFuncName, message;
    size_t i;

    if (type == CALL_TYPE_CALL) {
        /* call 到函数的调用 */
        if (!ftrace_queryNameThroughSymbolTable(funcName, addr)) {
            funcName = "<unknown>";
        }

        // 记录入栈信息：调用至目的函数
        message = std::format("0x{:08x}: ", srcAddr);
        for (i = 0; i < sim_state.ftrace_callStack.size(); i++) {
            message += "  ";
        }
        message += std::format(
            "call to [{}@0x{:08x}], ret=0x{:08x}, caller_sp=0x{:08x}",
            funcName, addr, retAddr, callerSp
        );
        sim_state.ftrace_ofs << message << std::endl;
        sim_state.ftrace_ofs.flush();
        if (sim_config.config_debugOutput)
            std::cout << "[sim] ftrace: " << message << std::endl;

        // 将该函数入栈
        CallFrameInfo info;
        info.funcAddr = addr;
        info.funcName = funcName;
        info.retAddr = retAddr;
        info.callerSp = callerSp;
        sim_state.ftrace_callStack.push(std::move(info));

        return true;
    }

    if (type == CALL_TYPE_TAIL) {
        /* tail 从当前函数进行尾调用到另一个函数 */
        if (!ftrace_queryNameThroughSymbolTable(funcName, srcAddr)) {
            funcName = "<unknown>";
        }
        if (!ftrace_queryNameThroughSymbolTable(destFuncName, addr)) {
            destFuncName = "<unknown>";
        }

        CallFrameInfo baseFrame;
        if (!sim_state.ftrace_callStack.empty()) {
            baseFrame = sim_state.ftrace_callStack.top();
            sim_state.ftrace_callStack.pop();
        } else {
            baseFrame.funcAddr = srcAddr;
            baseFrame.funcName = funcName;
            baseFrame.retAddr = retAddr;
            baseFrame.callerSp = callerSp;
        }

        // 记录信息：尾调用至另一个函数
        message = std::format("0x{:08x}: ", srcAddr);
        for (i = 0; i < sim_state.ftrace_callStack.size(); i++) {
            message += "  ";
        }
        message += std::format(
            "tail from [{}@0x{:08x}] to [{}@0x{:08x}]",
            funcName, srcAddr, destFuncName, addr
        );
        sim_state.ftrace_ofs << message << std::endl;
        sim_state.ftrace_ofs.flush();
        if (sim_config.config_debugOutput)
            std::cout << "[sim] ftrace: " << message << std::endl;

        // 再将目的函数入栈
        CallFrameInfo info;
        info.funcAddr = addr;
        info.funcName = destFuncName;
        info.retAddr = baseFrame.retAddr;
        info.callerSp = baseFrame.callerSp;
        sim_state.ftrace_callStack.push(std::move(info));

        return true;
    }

    if (type == CALL_TYPE_RET) {
        /* ret 从当前函数返回 */
        if (!ftrace_queryNameThroughSymbolTable(funcName, srcAddr)) {
            funcName = "<unknown>";
        }
        if (!ftrace_queryNameThroughSymbolTable(destFuncName, addr)) {
            destFuncName = "<unknown>";
        }

        // 将当前函数出栈；若前面存在尾调用/符号缺失导致的栈偏移，则回退到匹配返回地址的位置。
        while (!sim_state.ftrace_callStack.empty()) {
            const auto &info = sim_state.ftrace_callStack.top();
            if (info.retAddr == addr) {
                break;
            }
            sim_state.ftrace_callStack.pop();
        }
        if (!sim_state.ftrace_callStack.empty()) {
            sim_state.ftrace_callStack.pop();
        }

        // 记录出栈信息：从当前函数返回
        // 【注意】返回到的目的地址不是函数的起始地址，而是在函数体内部
        //        所以不方便记录目的函数信息，只能记录当前函数信息（从哪里返回）
        message = std::format("0x{:08x}: ", srcAddr);
        for (i = 0; i < sim_state.ftrace_callStack.size(); i++) {
            message += "  ";
        }
        message += std::format(
            "ret from [{}@0x{:08x}] to [{}@0x{:08x}]",
            funcName, srcAddr, destFuncName, addr
        );
        sim_state.ftrace_ofs << message << std::endl;
        sim_state.ftrace_ofs.flush();
        if (sim_config.config_debugOutput)
            std::cout << "[sim] ftrace: " << message << std::endl;

        return true;
    }

    return false;
}
