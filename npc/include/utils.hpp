#ifndef __UTILS_HPP__
#define __UTILS_HPP__ 1

#include <stdint.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stack>
#include <utils/RingBuffer.hpp>
#include <utils/Symbol.hpp>
#include <utils/CallStackInfo.hpp>

// ----------- state -----------

enum SimStateEnum { SIM_RUNNING, SIM_STOP, SIM_END, SIM_ABORT, SIM_QUIT };

#define CALL_STACK_MAX_DEPTH 1024

#define ITRACE_IRINGBUF_SIZE 1024

#define DEFAULT_ITRACE_OUT_FILE_PATH "build/itrace.log"
#define DEFAULT_MTRACE_OUT_FILE_PATH "build/mtrace.log"
#define DEFAULT_FTRACE_OUT_FILE_PATH "build/ftrace.log"
#define DEFAULT_ELF_FILE_PATH "build/program.elf"

struct SimConfig {
    bool config_itrace;
    bool config_mtrace;
    bool config_ftrace;

    std::string config_itraceOutFilePath;
    std::string config_mtraceOutFilePath;
    std::string config_ftraceOutFilePath;
    std::string config_elfFilePath;
};

struct SimState {
    SimStateEnum state;

    RingBuffer *itrace_iringbuf;
    std::vector<Symbol> ftrace_funcSyms;
    std::stack<CallStackInfo> ftrace_callStack;

    std::ofstream itrace_ofs;
    std::ofstream mtrace_ofs;
    std::ofstream ftrace_ofs;
};

extern SimConfig sim_config;

extern SimState sim_state;

/**
 * @brief 初始化用于 itrace 的环形缓冲区。
 */
void sim_state_itrace_iringbuf_init();

/**
 * @brief 释放用于 itrace 的环形缓冲区。
 */
void sim_state_itrace_iringbuf_destroy();

/**
 * @brief 输出用于 itrace 的环形缓冲区中的内容。
 */
void sim_state_itrace_iringbuf_dump();

/**
 * @brief 初始化所有用于 trace 记录的文件输出流（ofstream）。
 */
void sim_state_ofstream_init();

/**
 * @brief 关闭所有用于 trace 记录的文件输出流（ofstream）。
 */
void sim_state_ofstream_finalise();

/**
 * @brief 根据相关配置，加载程序中的函数符号信息。
 * 需提前确保 sim_config 中相关配置信息已正确填入。
 * 
 * @return true 加载成功
 * @return false 加载失败
 */
bool sim_state_ftrace_funcSyms_init();

// ----------- log -----------

#define ANSI_FG_BLACK   "\33[1;30m"
#define ANSI_FG_RED     "\33[1;31m"
#define ANSI_FG_GREEN   "\33[1;32m"
#define ANSI_FG_YELLOW  "\33[1;33m"
#define ANSI_FG_BLUE    "\33[1;34m"
#define ANSI_FG_MAGENTA "\33[1;35m"
#define ANSI_FG_CYAN    "\33[1;36m"
#define ANSI_FG_WHITE   "\33[1;37m"
#define ANSI_BG_BLACK   "\33[1;40m"
#define ANSI_BG_RED     "\33[1;41m"
#define ANSI_BG_GREEN   "\33[1;42m"
#define ANSI_BG_YELLOW  "\33[1;43m"
#define ANSI_BG_BLUE    "\33[1;44m"
#define ANSI_BG_MAGENTA "\33[1;45m"
#define ANSI_BG_CYAN    "\33[1;46m"
#define ANSI_BG_WHITE   "\33[1;47m"
#define ANSI_NONE       "\33[0m"

#define ANSI_FMT(str, fmt) fmt str ANSI_NONE

// ----------- array -----------

// calculate the length of an array
#define ARRLEN(arr) (int)(sizeof(arr) / sizeof(arr[0]))

// ----------- disasm -----------

/**
 * @brief 初始化反汇编工具。在使用本反汇编工具前须调用此函数。
 */
void disasm_init();

/**
 * @brief 使用反汇编工具反汇编一段代码。
 * 
 * @param str 输出目的字符串缓冲区
 * @param size 字符串缓冲区大小
 * @param pc 程序计数器
 * @param code 待反汇编的代码段
 * @param nbyte 待反汇编的代码段长度
 */
void disasm_disassemble(char *str, int size, uint64_t pc, uint8_t *code, int nbyte);

// ----------- ftrace -----------

/**
 * @brief ftrace: 在符号表中查询指定内存地址处的符号。
 * 
 * @param dest 输出目的字符串缓冲区
 * @param addr 内存地址
 * @return true 查询成功
 * @return false 查询失败
 */
bool ftrace_queryNameThroughSymbolTable(std::string &dest, addr_t addr);

/**
 * @brief ftrace: 尝试记录到指定内存地址处的函数调用信息。
 * 
 * @param type 函数调用类型
 * @param srcAddr 函数调用的源起地址
 * @param addr 函数调用的目标地址
 * @return true 记录成功（找到目标函数的符号信息）
 * @return false 记录失败（未找到目标函数信息，无法记录）
 */
bool ftrace_tryRecord(CallType type, addr_t srcAddr, addr_t addr);

#endif /* __UTILS_HPP__ */
