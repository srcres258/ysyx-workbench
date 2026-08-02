#ifndef NPC_CONFIG_HPP
#define NPC_CONFIG_HPP

#include <cstdint>
#include <string>

namespace npc {

/**
 * @brief 仿真器配置 — 运行器传入的编程式配置对象。
 *
 * 字段语义与 @c SimConfig 完全一一对应，但作为 @c npc::Simulator 的
 * 公开初始化参数，不与任何内部全局变量耦合。
 */
struct SimulatorConfig {
    // ---- trace 开关 ----
    bool itraceEnabled = false;
    bool mtraceEnabled = false;
    bool ftraceEnabled = false;
    bool dtraceEnabled = false;
    bool etraceEnabled = false;

    // ---- 功能开关 ----
    bool difftestEnabled = false;
    bool deviceEnabled = false;
    bool waveEnabled = false;
    bool debugOutputEnabled = false;
    bool nvboardEnabled = false;
    bool vgaEnabled = false;
    bool mromEnabled = false;

    // ---- TUI 开关 ----
    bool tuiEnabled = false;
    bool perfEnabled = true;   // 默认打开 (与运行时行为一致)

    // ---- trace 格式 ----
    std::string traceFormat   = "human";   // "human" / "jsonl" / "both"
    std::string traceDataMode = "stores";  // "none" / "stores" / "all"

    // ---- TUI 配置 ----
    std::string tuiConfigFilePath        = "build/npc-tui.toml";
    bool        tuiGenerateConfig        = false;
    bool        tuiGenerateFullConfig    = false;
    bool        tuiForceOverwriteConfig  = false;
    bool        tuiPrintConfigSchema     = false;
    bool        tuiPrintDefaultConfig    = false;

    // ---- DiffTest ----
    int         difftestPort             = 12345;
    std::string difftestStartMode        = "reset";  // "reset" / "payload"
    std::uint32_t difftestStartPC        = 0;        // 默认值在 standalone 模式下会调整
    std::string difftestPayloadBinFilePath;
    std::uint32_t difftestPayloadLoadAddr = 0x80000000;
    std::string difftestMemMode          = "auto";   // "auto" / "psram" / "sdram"

    // ---- 输出文件路径 ----
    std::string itraceOutFilePath        = "build/itrace.log";
    std::string itraceJsonlOutFilePath   = "build/itrace.jsonl";
    std::string mtraceOutFilePath        = "build/mtrace.log";
    std::string mtraceJsonlOutFilePath   = "build/mtrace.jsonl";
    std::string ftraceOutFilePath        = "build/ftrace.log";
    std::string dtraceOutFilePath        = "build/dtrace.log";
    std::string dtraceJsonlOutFilePath   = "build/dtrace.jsonl";
    std::string etraceOutFilePath        = "build/etrace.log";
    std::string etraceJsonlOutFilePath   = "build/etrace.jsonl";
    std::string flashBinFilePath         = "build/flash.bin";
    std::string flashElfFilePath         = "build/flash.elf";
    std::string mromBinFilePath          = "build/mrom.bin";
    std::string difftestSoFilePath       = "build/riscv32-nemu-interpreter-so";
    std::string waveFilePath             = "build/sim.fst";
};

} // namespace npc

#endif // NPC_CONFIG_HPP
