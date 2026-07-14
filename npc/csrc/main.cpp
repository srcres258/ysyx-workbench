#include <iostream>
#include <cstdlib>
#include <cstring>
#include <sim_top.hpp>
#include <utils.hpp>
#include <tui/tui_config.hpp>

VerilatedContext *verContext = nullptr;

/**
 * @brief 从环境变量读取配置并加载进来。
 */
static void loadConfig() {
    char *env;

    env = std::getenv("NPC_CONFIG_ITRACE");
    sim_config.config_itrace = env && strcmp(env, "on") == 0;
    if (sim_config.config_itrace) {
        std::cout << "[config] itrace 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_MTRACE");
    sim_config.config_mtrace = env && strcmp(env, "on") == 0;
    if (sim_config.config_mtrace) {
        std::cout << "[config] mtrace 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_FTRACE");
    sim_config.config_ftrace = env && strcmp(env, "on") == 0;
    if (sim_config.config_ftrace) {
        std::cout << "[config] ftrace 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DTRACE");
    sim_config.config_dtrace = env && strcmp(env, "on") == 0;
    if (sim_config.config_dtrace) {
        std::cout << "[config] dtrace 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_ETRACE");
    sim_config.config_etrace = env && strcmp(env, "on") == 0;
    if (sim_config.config_etrace) {
        std::cout << "[config] etrace 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST");
    sim_config.config_difftest = env && strcmp(env, "on") == 0;
    if (sim_config.config_difftest) {
        std::cout << "[config] DiffTest 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DEVICE");
    sim_config.config_device = env && strcmp(env, "on") == 0;
    if (sim_config.config_device) {
        std::cout << "[config] 外部设备已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_WAVE");
    sim_config.config_wave = env && strcmp(env, "on") == 0;
    if (sim_config.config_wave) {
        std::cout << "[config] 波形文件输出已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DEBUG_OUTPUT");
    sim_config.config_debugOutput = env && strcmp(env, "on") == 0;
    if (sim_config.config_debugOutput) {
        std::cout << "[config] 调试信息输出已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_NVBOARD");
    sim_config.config_nvboard = env && strcmp(env, "on") == 0;
    if (sim_config.config_nvboard) {
        std::cout << "[config] NVBoard 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_MROM");
    sim_config.config_mrom = env && strcmp(env, "on") == 0;
    if (sim_config.config_mrom) {
        std::cout << "[config] MROM 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_PORT");
    try {
        sim_config.config_difftestPort = env ? std::stoi(env) : 0;
        std::cout << "[config] DiffTest 端口已指定为 " <<
            std::dec << sim_config.config_difftestPort << std::endl;
    } catch (const std::exception &e) {
        std::cout << "[config] DiffTest 端口设置失败！将使用默认端口 " <<
            std::dec << DEFAULT_DIFFTEST_PORT << std::endl;
        sim_config.config_difftestPort = DEFAULT_DIFFTEST_PORT;
    }

    env = std::getenv("NPC_CONFIG_ITRACE_OUT_FILE_PATH");
    if (env) {
        sim_config.config_itraceOutFilePath =
            std::move(std::string(env));
        std::cout << "[config] itrace 输出路径已指定为: " <<
            sim_config.config_itraceOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_MTRACE_OUT_FILE_PATH");
    if (env) {
        sim_config.config_mtraceOutFilePath =
            std::move(std::string(env));
        std::cout << "[config] mtrace 输出路径已指定为: " <<
            sim_config.config_mtraceOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_FTRACE_OUT_FILE_PATH");
    if (env) {
        sim_config.config_ftraceOutFilePath =
            std::move(std::string(env));
        std::cout << "[config] ftrace 输出路径已指定为: " <<
            sim_config.config_ftraceOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DTRACE_OUT_FILE_PATH");
    if (env) {
        sim_config.config_dtraceOutFilePath =
            std::move(std::string(env));
        std::cout << "[config] dtrace 输出路径已指定为: " <<
            sim_config.config_dtraceOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_ETRACE_OUT_FILE_PATH");
    if (env) {
        sim_config.config_etraceOutFilePath =
            std::move(std::string(env));
        std::cout << "[config] etrace 输出路径已指定为: " <<
            sim_config.config_etraceOutFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_FLASH_BIN_FILE_PATH");
    if (env) {
        sim_config.config_flashBinFilePath =
            std::move(std::string(env));
        std::cout << "[config] FLASH BIN 文件路径已指定为: " <<
            sim_config.config_flashBinFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_FLASH_ELF_FILE_PATH");
    if (env) {
        sim_config.config_flashElfFilePath =
            std::move(std::string(env));
        std::cout << "[config] FLASH ELF 文件路径已指定为: " <<
            sim_config.config_flashElfFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_MROM_BIN_FILE_PATH");
    if (env) {
        sim_config.config_mromBinFilePath =
            std::move(std::string(env));
        std::cout << "[config] MROM BIN 文件路径已指定为: " <<
            sim_config.config_mromBinFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_SO_FILE_PATH");
    if (env) {
        sim_config.config_difftestSoFilePath =
            std::move(std::string(env));
        std::cout << "[config] DiffTest 动态链接库文件路径已指定为: " <<
            sim_config.config_difftestSoFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_START_MODE");
    if (env) {
        std::string mode(env);
        if (mode != "reset" && mode != "payload") {
            std::cerr << "[config] 无效的 DiffTest 起始模式: " << mode
                      << " (必须为 reset 或 payload)" << std::endl;
            std::exit(EXIT_FAILURE);
        }
        sim_config.config_difftestStartMode = std::move(mode);
    }
    std::cout << "[config] DiffTest 起始模式: "
              << sim_config.config_difftestStartMode << std::endl;

    env = std::getenv("NPC_CONFIG_DIFFTEST_START_PC");
    try {
        sim_config.config_difftestStartPC = env
            ? static_cast<addr_t>(std::stoul(std::string(env), nullptr, 0))
            : DEFAULT_DIFFTEST_START_PC;
        std::cout << "[config] DiffTest 起始 PC: 0x" << std::hex
                  << sim_config.config_difftestStartPC << std::dec << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "[config] DiffTest 起始 PC 解析失败: " << e.what() << std::endl;
        std::exit(EXIT_FAILURE);
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH");
    if (env) {
        sim_config.config_difftestPayloadBinFilePath =
            std::move(std::string(env));
        std::cout << "[config] DiffTest Payload BIN 文件路径已指定为: "
                  << sim_config.config_difftestPayloadBinFilePath << std::endl;
    }

    env = std::getenv("NPC_CONFIG_DIFFTEST_PAYLOAD_LOAD_ADDR");
    try {
        sim_config.config_difftestPayloadLoadAddr = env
            ? static_cast<addr_t>(std::stoul(std::string(env), nullptr, 0))
            : DEFAULT_DIFFTEST_PAYLOAD_LOAD_ADDR;
        std::cout << "[config] DiffTest Payload 加载地址: 0x" << std::hex
                  << sim_config.config_difftestPayloadLoadAddr << std::dec << std::endl;
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
        sim_config.config_difftestMemMode = std::move(mode);
    }
    std::cout << "[config] DiffTest 内存模式: "
              << sim_config.config_difftestMemMode << std::endl;

    env = std::getenv("NPC_CONFIG_WAVE_FILE_PATH");
    if (env) {
        sim_config.config_waveFilePath =
            std::move(std::string(env));
        std::cout << "[config] 波形文件输出路径已指定为: " <<
            sim_config.config_waveFilePath << std::endl;
    }

    // ---- TUI 配置 ----
    env = std::getenv("NPC_CONFIG_TUI");
    sim_config.config_tui = env && strcmp(env, "on") == 0;
    if (sim_config.config_tui) {
        std::cout << "[config] TUI 已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TUI_CONFIG_FILE_PATH");
    if (env) {
        sim_config.config_tuiConfigFilePath =
            std::move(std::string(env));
    }
    std::cout << "[config] TUI 配置文件路径: "
              << sim_config.config_tuiConfigFilePath << std::endl;

    env = std::getenv("NPC_CONFIG_TUI_GENERATE_CONFIG");
    sim_config.config_tuiGenerateConfig = env && strcmp(env, "on") == 0;
    if (sim_config.config_tuiGenerateConfig) {
        std::cout << "[config] TUI 配置生成模式已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TUI_GENERATE_FULL_CONFIG");
    sim_config.config_tuiGenerateFullConfig = env && strcmp(env, "on") == 0;
    if (sim_config.config_tuiGenerateFullConfig) {
        std::cout << "[config] TUI 完整配置生成模式已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TUI_FORCE_OVERWRITE_CONFIG");
    sim_config.config_tuiForceOverwriteConfig = env && strcmp(env, "on") == 0;
    if (sim_config.config_tuiForceOverwriteConfig) {
        std::cout << "[config] TUI 配置强制覆盖已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TUI_PRINT_CONFIG_SCHEMA");
    sim_config.config_tuiPrintConfigSchema = env && strcmp(env, "on") == 0;
    if (sim_config.config_tuiPrintConfigSchema) {
        std::cout << "[config] TUI 打印配置 schema 模式已启用" << std::endl;
    }

    env = std::getenv("NPC_CONFIG_TUI_PRINT_DEFAULT_CONFIG");
    sim_config.config_tuiPrintDefaultConfig = env && strcmp(env, "on") == 0;
    if (sim_config.config_tuiPrintDefaultConfig) {
        std::cout << "[config] TUI 打印默认配置模式已启用" << std::endl;
    }

    // ---- 性能计数器配置 ----
    env = std::getenv("NPC_CONFIG_PERF");
    if (!env) {
        sim_config.config_perf = true; // default on
    } else if (strcmp(env, "on") == 0) {
        sim_config.config_perf = true;
    } else if (strcmp(env, "off") == 0) {
        sim_config.config_perf = false;
    } else {
        std::cerr << "[config] 无效的 RUN_CONFIG_PERF 值: \"" << env
                  << "\" (必须为 on 或 off)" << std::endl;
        std::exit(EXIT_FAILURE);
    }
    std::cout << "[config] 性能计数器已"
              << (sim_config.config_perf ? "启用" : "禁用") << std::endl;

    // 跨字段验证: payload 模式下必须指定 payload 二进制文件路径
    if (sim_config.config_difftest
        && sim_config.config_difftestStartMode == "payload"
        && sim_config.config_difftestPayloadBinFilePath.empty()) {
        std::cerr << "[config] payload 模式已启用，但未指定 NPC_CONFIG_DIFFTEST_PAYLOAD_BIN_FILE_PATH 环境变量!"
                  << std::endl;
        std::exit(EXIT_FAILURE);
    }
}

/**
 * @brief 检查必需的配置选项是否均已设置。
 *
 * @return true 必需的配置选项已设置
 * @return false 存在未设置的必需配置选项
 */
static bool checkRequiredConfig() {
    if (!sim_config.config_device) {
        return true;
    }
    if (sim_config.config_flashBinFilePath.empty()) {
        std::cerr << "未指定 NPC_CONFIG_FLASH_BIN_FILE_PATH 环境变量, 请指定 FLASH BIN 文件路径!" << std::endl;
        return false;
    }
    if (sim_config.config_flashElfFilePath.empty()) {
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

    verContext = new VerilatedContext;
    verContext->commandArgs(argc, argv);

    std::cout << "正在加载配置选项..." << std::endl;
    sdbEnabled = std::getenv("NPC_SDB_ENABLED");
    if (sdbEnabled && strcmp(sdbEnabled, "true") == 0) {
        std::cout << "SDB 已启用!" << std::endl;
        sdb = true;
    } else {
        sdb = false;
    }
    loadConfig();
    if (!checkRequiredConfig()) {
        return EXIT_FAILURE;
    }

    // ── TUI 配置操作 (schema / print / generate) ──
    if (sim_config.config_tuiPrintConfigSchema) {
        tui::printTuiConfigSchema(std::cout);
        return EXIT_SUCCESS;
    }
    if (sim_config.config_tuiPrintDefaultConfig) {
        tui::printTuiDefaultConfig(std::cout);
        return EXIT_SUCCESS;
    }
    if (sim_config.config_tuiGenerateConfig) {
        bool full = sim_config.config_tuiGenerateFullConfig;
        bool overwrite = sim_config.config_tuiForceOverwriteConfig;
        if (!tui::generateTuiConfig(sim_config.config_tuiConfigFilePath,
                                     overwrite, full)) {
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    // TUI 和 SDB 不能同时启用
    if (sim_config.config_tui && sdb) {
        std::cerr << "[config] 错误: TUI 模式与 SDB 模式不能同时启用!"
                  << std::endl;
        return EXIT_FAILURE;
    }

    // 加载 / 生成 TUI 配置文件
    if (sim_config.config_tui) {
        if (!tui::loadOrGenerateTuiConfig(sim_config.config_tuiConfigFilePath)) {
            std::cerr << "[tui] 配置文件加载失败, 退出." << std::endl;
            return EXIT_FAILURE;
        }
    }

    result = simulate(sdb);

    delete verContext;

    return result ? EXIT_SUCCESS : EXIT_FAILURE;
}
