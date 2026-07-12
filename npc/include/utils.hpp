#ifndef __UTILS_HPP__
#define __UTILS_HPP__ 1

#include <stdint.h>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <stack>
#include <common.hpp>
#include <macro-def.hpp>
#include <utils/RingBuffer.hpp>
#include <utils/Symbol.hpp>
#include <utils/CallFrameInfo.hpp>

// ----------- state -----------

enum SimStateEnum { SIM_RUNNING, SIM_STOP, SIM_END, SIM_ABORT, SIM_QUIT };

#define CALL_STACK_MAX_DEPTH 1024

#define ITRACE_IRINGBUF_SIZE 1024

#define DEFAULT_DIFFTEST_PORT 12345
#define DEFAULT_DIFFTEST_START_MODE "reset"
#define DEFAULT_DIFFTEST_START_PC 0
#define DEFAULT_DIFFTEST_PAYLOAD_BIN_FILE_PATH ""
#define DEFAULT_DIFFTEST_PAYLOAD_LOAD_ADDR PSRAM_ADDR
#define DEFAULT_DIFFTEST_MEM_MODE "auto"
#define DEFAULT_ITRACE_OUT_FILE_PATH "build/itrace.log"
#define DEFAULT_MTRACE_OUT_FILE_PATH "build/mtrace.log"
#define DEFAULT_FTRACE_OUT_FILE_PATH "build/ftrace.log"
#define DEFAULT_DTRACE_OUT_FILE_PATH "build/dtrace.log"
#define DEFAULT_ETRACE_OUT_FILE_PATH "build/etrace.log"
#define DEFAULT_FLASH_BIN_FILE_PATH "build/flash.bin"
#define DEFAULT_FLASH_ELF_FILE_PATH "build/flash.elf"
#define DEFAULT_MROM_BIN_FILE_PATH "build/mrom.bin"
#define DEFAULT_DIFFTEST_SO_FILE_PATH "build/riscv32-nemu-interpreter-so"
#define DEFAULT_WAVE_FILE_PATH "build/sim.fst"
#define DEFAULT_TUI_CONFIG_FILE_PATH "build/npc-tui.toml"

struct SimConfig {
    bool config_itrace;
    bool config_mtrace;
    bool config_ftrace;
    bool config_dtrace;
    bool config_etrace;
    bool config_difftest;
    bool config_device;
    bool config_wave;
    bool config_debugOutput;
    bool config_nvboard;
    bool config_mrom;

    bool config_tui;
    std::string config_tuiConfigFilePath;
    bool config_tuiGenerateConfig;
    bool config_tuiGenerateFullConfig;
    bool config_tuiForceOverwriteConfig;
    bool config_tuiPrintConfigSchema;
    bool config_tuiPrintDefaultConfig;

    int config_difftestPort;

    std::string config_difftestStartMode;
    addr_t      config_difftestStartPC;
    std::string config_difftestPayloadBinFilePath;
    addr_t      config_difftestPayloadLoadAddr;
    std::string config_difftestMemMode;

    std::string config_itraceOutFilePath;
    std::string config_mtraceOutFilePath;
    std::string config_ftraceOutFilePath;
    std::string config_dtraceOutFilePath;
    std::string config_etraceOutFilePath;
    std::string config_flashBinFilePath;
    std::string config_flashElfFilePath;
    std::string config_mromBinFilePath;
    std::string config_difftestSoFilePath;
    std::string config_waveFilePath;
};

struct SimState {
    SimStateEnum state;
    addr_t haltPC;

    RingBuffer *itrace_iringbuf;
    std::vector<Symbol> ftrace_funcSyms;
    std::stack<CallFrameInfo> ftrace_callStack;

    std::ofstream itrace_ofs;
    std::ofstream mtrace_ofs;
    std::ofstream ftrace_ofs;
    std::ofstream dtrace_ofs;
    std::ofstream etrace_ofs;
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

// ----------- trace sinks -----------

void trace_record_mtrace(
    addr_t pc, bool isWrite, addr_t addr, int len, word_t data, uint8_t strobe, uint32_t resp
);

void trace_record_dtrace(
    addr_t pc, const char *device, bool isWrite, addr_t addr, int len, word_t data,
    const char *bus, const char *region
);

void trace_record_etrace(
    addr_t pc, const char *trapKind, word_t cause, word_t mepc, word_t mtval, word_t target
);

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
 * @param retAddr 当前调用返回时应跳转到的地址
 * @param callerSp 当前调用发生时的调用者栈指针
 * @return true 记录成功（包含未知符号时也会保留运行时栈信息）
 * @return false 仅当类型不支持时返回
 */
bool ftrace_tryRecord(CallType type, addr_t srcAddr, addr_t addr, addr_t retAddr, word_t callerSp);

// ----------- panic -----------

#define panic(...) do {                  \
	fprintf(stderr, "panic: %s:%u: %s:", \
		__FILE__, __LINE__, __func__);   \
	fprintf(stderr, " " __VA_ARGS__);	 \
	abort();                             \
} while (0)

// ----------- assert -----------

#define Assert(cond, ...) do {                              \
    if (!(cond)) {                                          \
        panic("assertion failed: " #cond "\n" __VA_ARGS__); \
    }                                                       \
} while (0)

// ----------- memory -----------

word_t memoryHostRead(const void *addr, int len);

void memoryHostWrite(void *addr, int len, word_t data);

// ----------- util macros -----------

// functional-programming-like macro (X-macro)
// apply the function `f` to each element in the container `c`
// NOTE1: `c` should be defined as a list like:
//   f(a0) f(a1) f(a2) ...
// NOTE2: each element in the container can be a tuple
#define MAP(c, f) c(f)

#endif /* __UTILS_HPP__ */
