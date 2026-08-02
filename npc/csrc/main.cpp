#include <iostream>
#include <cstdlib>
#include <cstring>
#include <sim_top.hpp>
#include <utils.hpp>
#include <tui/tui_config.hpp>
#include <npc/simulator.hpp>

// ── Task 5: verContext lifecycle ownership moved into npc::Simulator ──
//   verContext is now created in Simulator::initialize() and destroyed
//   in Simulator::run() cleanup / ~Simulator().
//   sim_config / sim_state authoritative ownership is in
//   npc/csrc/npc/simulator.cpp (Task 3).
//   Runner builds npc::SimulatorConfig from env vars and hands it
//   to the library via Simulator::initialize() — no direct sim_config
//   mutation in main.cpp.

/**
 * @brief 从环境变量读取配置并构建 SimulatorConfig 对象。
 *
 * 环境变量解析完全留在运行器侧。
 * 返回值中的字段包含所有环境变量覆盖后的最终配置。
 */
static npc::SimulatorConfig buildConfigFromEnv() {
    npc::SimulatorConfig config;
    char *env;

    env = std::getenv("NPC_CONFIG_ITRACE");
    config.itraceEnabled = env && strcmp(env, "on") == 0;
    if (config.itraceEnabled) {
        std::cout << "[config] itrace 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_MTRACE");
    config.mtraceEnabled = env && strcmp(env, "on") == 0;
    if (config.mtraceEnabled) {
        std::cout << "[config] mtrace 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TRACE_FORMAT");
    if (env) {
        std::string format(env);
        if (format != "human" && format != "jsonl" && format != "both") {
            std::cerr << "[config] 无效的 trace 格式: " << format
                      << " (必须为 human, jsonl 或 both)" << std::endl;
            std::exit(EXIT_FAILURE);
        }
        config.traceFormat = std::move(format);
    }
    std::cout << "[config] trace 格式: " << config.traceFormat << std::endl;

    env = std::getenv("NPC_CONFIG_TRACE_DATA_MODE");
    if (env) {
        std::string mode(env);
        if (mode != "none" && mode != "stores" && mode != "all") {
            std::cerr << "[config] 无效的 trace 数据模式: " << mode
                      << " (必须为 none, stores 或 all)" << std::endl;
            std::exit(EXIT_FAILURE);
        }
        config.traceDataMode = std::move(mode);
    }
    std::cout << "[config] trace 数据模式: " << config.traceDataMode << std::endl;

    env = std::getenv("NPC_CONFIG_FTRACE");
    config.ftraceEnabled = env && strcmp(env, "on") == 0;
    if (config.ftraceEnabled) {
        std::cout << "[config] ftrace 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DTRACE");
    config.dtraceEnabled = env && strcmp(env, "on") == 0;
    if (config.dtraceEnabled) {
        std::cout << "[config] dtrace 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_ETRACE");
    config.etraceEnabled = env && strcmp(env, "on") == 0;
    if (config.etraceEnabled) {
        std::cout << "[config] etrace 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST");
    config.difftestEnabled = env && strcmp(env, "on") == 0;
    if (config.difftestEnabled) {
        std::cout << "[config] DiffTest 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DEVICE");
    config.deviceEnabled = env && strcmp(env, "on") == 0;
    if (config.deviceEnabled) {
        std::cout << "[config] 外部设备已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_WAVE");
    config.waveEnabled = env && strcmp(env, "on") == 0;
    if (config.waveEnabled) {
        std::cout << "[config] 波形文件输出已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DEBUG_OUTPUT");
    config.debugOutputEnabled = env && strcmp(env, "on") == 0;
    if (config.debugOutputEnabled) {
        std::cout << "[config] 调试信息输出已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_NVBOARD");
    config.nvboardEnabled = env && strcmp(env, "on") == 0;
    if (config.nvboardEnabled) {
        std::cout << "[config] NVBoard 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_VGA");
    config.vgaEnabled = env && strcmp(env, "on") == 0;
    if (config.vgaEnabled) {
        std::cout << "[config] VGA 窗口已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_MROM");
    config.mromEnabled = env && strcmp(env, "on") == 0;
    if (config.mromEnabled) {
        std::cout << "[config] MROM 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_PORT");
    try {
        config.difftestPort = env ? std::stoi(env) : 0;
        std::cout << "[config] DiffTest 端口已指定为 " <<
            std::dec << config.difftestPort << std::endl;
    } catch (const std::exception &e) {
        std::cout << "[config] DiffTest 端口设置失败！将使用默认端口 " <<
            std::dec << DEFAULT_DIFFTEST_PORT << std::endl;
        config.difftestPort = DEFAULT_DIFFTEST_PORT;
    }

    env = std::getenv("NPC_CONFIG_ITRACE_OUT_FILE_PATH");
    if (env) {
        config.itraceOutFilePath = std::move(std::string(env));
        std::cout << "[config] itrace 输出路径已指定为: " <<
            config.itraceOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_ITRACE_JSONL_OUT_FILE_PATH");
    if (env) {
        config.itraceJsonlOutFilePath = std::move(std::string(env));
        std::cout << "[config] itrace JSONL 输出路径已指定为: " <<
            config.itraceJsonlOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_MTRACE_OUT_FILE_PATH");
    if (env) {
        config.mtraceOutFilePath = std::move(std::string(env));
        std::cout << "[config] mtrace 输出路径已指定为: " <<
            config.mtraceOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_MTRACE_JSONL_OUT_FILE_PATH");
    if (env) {
        config.mtraceJsonlOutFilePath = std::move(std::string(env));
        std::cout << "[config] mtrace JSONL 输出路径已指定为: " <<
            config.mtraceJsonlOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_FTRACE_OUT_FILE_PATH");
    if (env) {
        config.ftraceOutFilePath = std::move(std::string(env));
        std::cout << "[config] ftrace 输出路径已指定为: " <<
            config.ftraceOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DTRACE_OUT_FILE_PATH");
    if (env) {
        config.dtraceOutFilePath = std::move(std::string(env));
        std::cout << "[config] dtrace 输出路径已指定为: " <<
            config.dtraceOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DTRACE_JSONL_OUT_FILE_PATH");
    if (env) {
        config.dtraceJsonlOutFilePath = std::move(std::string(env));
        std::cout << "[config] dtrace JSONL 输出路径已指定为: " <<
            config.dtraceJsonlOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_ETRACE_OUT_FILE_PATH");
    if (env) {
        config.etraceOutFilePath = std::move(std::string(env));
        std::cout << "[config] etrace 输出路径已指定为: " <<
            config.etraceOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_ETRACE_JSONL_OUT_FILE_PATH");
    if (env) {
        config.etraceJsonlOutFilePath = std::move(std::string(env));
        std::cout << "[config] etrace JSONL 输出路径已指定为: " <<
            config.etraceJsonlOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_FLASH_BIN_FILE_PATH");
    if (env) {
        config.flashBinFilePath = std::move(std::string(env));
        std::cout << "[config] FLASH BIN 文件路径已指定为: " <<
            config.flashBinFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_FLASH_ELF_FILE_PATH");
    if (env) {
        config.flashElfFilePath = std::move(std::string(env));
        std::cout << "[config] FLASH ELF 文件路径已指定为: " <<
            config.flashElfFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_MROM_BIN_FILE_PATH");
    if (env) {
        config.mromBinFilePath = std::move(std::string(env));
        std::cout << "[config] MROM BIN 文件路径已指定为: " <<
            config.mromBinFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_SO_FILE_PATH");
    if (env) {
        config.difftestSoFilePath = std::move(std::string(env));
        std::cout << "[config] DiffTest 动态链接库文件路径已指定为: " <<
            config.difftestSoFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_START_MODE");
    if (env) {
        std::string mode(env);
        if (mode != "reset" && mode != "payload") {
            std::cerr << "[config] 无效的 DiffTest 起始模式: " << mode
                      << " (必须为 reset 或 payload)" << std::endl;
            std::exit(EXIT_FAILURE);
        }
        config.difftestStartMode = std::move(mode);
    }
    std::cout << "[config] DiffTest 起始模式: "
              << config.difftestStartMode << std::endl;

    env = std::getenv("NPC_CONFIG_DIFFTEST_START_PC");
    try {
        config.difftestStartPC = env
            ? static_cast<std::uint32_t>(std::stoul(std::string(env), nullptr, 0))
            : static_cast<std::uint32_t>(DEFAULT_DIFFTEST_START_PC);
        std::cout << "[config] DiffTest 起始 PC: 0x" << std::hex
                  << config.difftestStartPC << std::dec << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "[config] DiffTest 起始 PC 解析失败: " << e.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH");
    if (env) {
        config.difftestPayloadBinFilePath = std::move(std::string(env));
        std::cout << "[config] DiffTest Payload BIN 文件路径已指定为: "
                  << config.difftestPayloadBinFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_PAYLOAD_LOAD_ADDR");
    try {
        config.difftestPayloadLoadAddr = env
            ? static_cast<std::uint32_t>(std::stoul(std::string(env), nullptr, 0))
            : static_cast<std::uint32_t>(DEFAULT_DIFFTEST_PAYLOAD_LOAD_ADDR);
        std::cout << "[config] DiffTest Payload 加载地址: 0x" << std::hex
                  << config.difftestPayloadLoadAddr << std::dec << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "[config] DiffTest Payload 加载地址解析失败: " << e.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_MEM_MODE");
    if (env) {
        std::string mode(env);
        if (mode != "auto" && mode != "psram" && mode != "sdram") {
            std::cerr << "[config] 无效的 DiffTest 内存模式: " << mode
                      << " (必须为 auto, psram 或 sdram)" << std::endl;
            std::exit(EXIT_FAILURE);
        }
        config.difftestMemMode = std::move(mode);
    }
    std::cout << "[config] DiffTest 内存模式: "
              << config.difftestMemMode << std::endl;

    env = std::getenv("NPC_CONFIG_WAVE_FILE_PATH");
    if (env) {
        config.waveFilePath = std::move(std::string(env));
        std::cout << "[config] 波形文件输出路径已指定为: " <<
            config.waveFilePath << std::endl;
    }

    // ---- TUI 配置 ----
    env = std::getenv("NPC_CONFIG_TUI");
    config.tuiEnabled = env && strcmp(env, "on") == 0;
    if (config.tuiEnabled) {
        std::cout << "[config] TUI 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TUI_CONFIG_FILE_PATH");
    if (env) {
        config.tuiConfigFilePath = std::move(std::string(env));
    }
    std::cout << "[config] TUI 配置文件路径: "
              << config.tuiConfigFilePath << std::endl;

    env = std::getenv("NPC_CONFIG_TUI_GENERATE_CONFIG");
    config.tuiGenerateConfig = env && strcmp(env, "on") == 0;
    if (config.tuiGenerateConfig) {
        std::cout << "[config] TUI 配置生成模式已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TUI_GENERATE_FULL_CONFIG");
    config.tuiGenerateFullConfig = env && strcmp(env, "on") == 0;
    if (config.tuiGenerateFullConfig) {
        std::cout << "[config] TUI 完整配置生成模式已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TUI_FORCE_OVERWRITE_CONFIG");
    config.tuiForceOverwriteConfig = env && strcmp(env, "on") == 0;
    if (config.tuiForceOverwriteConfig) {
        std::cout << "[config] TUI 配置强制覆盖已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TUI_PRINT_CONFIG_SCHEMA");
    config.tuiPrintConfigSchema = env && strcmp(env, "on") == 0;
    if (config.tuiPrintConfigSchema) {
        std::cout << "[config] TUI 打印配置 schema 模式已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TUI_PRINT_DEFAULT_CONFIG");
    config.tuiPrintDefaultConfig = env && strcmp(env, "on") == 0;
    if (config.tuiPrintDefaultConfig) {
        std::cout << "[config] TUI 打印默认配置模式已启用" << std::endl;
    }

    // ---- 性能计数器配置 ----
    env = std::getenv("NPC_CONFIG_PERF");
    if (!env) {
        config.perfEnabled = true; // default on
    } else if (strcmp(env, "on") == 0) {
        config.perfEnabled = true;
    } else if (strcmp(env, "off") == 0) {
        config.perfEnabled = false;
    } else {
        std::cerr << "[config] 无效的 RUN_CONFIG_PERF 值: \"" << env
                  << "\" (必须为 on 或 off)" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "[config] 性能计数器已"
              << (config.perfEnabled ? "启用" : "禁用") << std::endl;

    return config;
}

/**
 * @brief 对已构建的 SimulatorConfig 做跨字段和必需字段验证。
 *
 * @return true 通过验证
 * @return false 验证失败
 */
static bool validateConfig(const npc::SimulatorConfig &config) {
    // 跨字段验证: payload 模式下必须指定 payload 二进制文件路径
    if (config.difftestEnabled
        && config.difftestStartMode == "payload"
        && config.difftestPayloadBinFilePath.empty()) {
        std::cerr << "[config] payload 模式已启用，但未指定 NPC_CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH 环境变量!"
                  << std::endl;
        return false;
    }

    // 必需配置验证: 设备模式下需要 flash bin/elf 路径
    if (!config.deviceEnabled) {
        return true;
    }
    if (config.flashBinFilePath.empty()) {
        std::cerr << "未指定 NPC_CONFIG_FLASH_BIN_FILE_PATH 环境变量, 请指定 FLASH BIN 文件路径!" << std::endl;
        return false;
    }
    if (config.flashElfFilePath.empty()) {
        std::cerr << "未指定 NPC_CONFIG_FLASH_ELF_FILE_PATH 环境变量, 请指定 FLASH ELF 文件路径!" << std::endl;
        return false;
    }

    return true;
}

/**
 * @brief 程序的入口函数。
 *
 * @param argc 程序参数数量
 * @param argv 程序参数
 * @return int 程序退出状态码
 */
int main(int argc, const char *argv[]) {
    const char *sdbEnabled;
    bool sdb, result;

    Verilated::commandArgs(argc, argv);

    std::cout << "正在加载配置选项..." << std::endl;
    sdbEnabled = std::getenv("NPC_SDB_ENABLED");
    if (sdbEnabled && strcmp(sdbEnabled, "true") == 0) {
        std::cout << "SDB 已启用!" << std::endl;
        sdb = true;
    } else {
        sdb = false;
    }

    // ── Runner-side env parsing → npc::SimulatorConfig ──
    npc::SimulatorConfig config = buildConfigFromEnv();
    if (!validateConfig(config)) {
        return EXIT_FAILURE;
    }

    // ── TUI 配置操作 (schema / print / generate) ──
    // 这些操作不需要硬件仿真器, 直接使用 runner 构建的 config.
    if (config.tuiPrintConfigSchema) {
        tui::printTuiConfigSchema(std::cout);
        return EXIT_SUCCESS;
    }
    if (config.tuiPrintDefaultConfig) {
        tui::printTuiDefaultConfig(std::cout);
        return EXIT_SUCCESS;
    }
    if (config.tuiGenerateConfig) {
        if (!tui::generateTuiConfig(
            config.tuiConfigFilePath,
            config.tuiForceOverwriteConfig,
            config.tuiGenerateFullConfig
        )) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    // TUI 和 SDB 不能同时启用
    if (config.tuiEnabled && sdb) {
        std::cerr << "[config] 错误: TUI 模式与 SDB 模式不能同时启用!"
                  << std::endl;
        return EXIT_FAILURE;
    }

    // ── 将配置交给库端, 库端只接收已构造好的数据 ──
    // initialize() 内部调用 applySimulatorConfig() 把公共配置
    // 翻译为内部 SimConfig / SimState, 并创建 VerilatedContext,
    // 构造顶层模型, 打开波形, 初始化设备和 DiffTest,
    // 进行处理器重置等完整初始化工作.
    npc::Simulator sim;
    sim.initialize(config, argc, argv);

    // ── 加载 / 生成 TUI 配置文件 (sim_config 由 initialize 填充) ──
    if (config.tuiEnabled) {
        if (!tui::loadOrGenerateTuiConfig(sim_config.config_tuiConfigFilePath)) {
            std::cerr << "[tui] 配置文件加载失败, 退出." << std::endl;
            return EXIT_FAILURE;
        }
    }

    result = simulate(sdb);

    return result ? EXIT_SUCCESS : EXIT_FAILURE;
}
