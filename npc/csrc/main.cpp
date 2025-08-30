#include <iostream>
#include <cstdlib>
#include <cstring>
#include <sim_top.hpp>
#include <memory.hpp>
#include <utils.hpp>

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
    
    env = std::getenv("NPC_CONFIG_ELF_FILE_PATH");
    if (env) {
        sim_config.config_elfFilePath =
            std::move(std::string(env));
        std::cout << "[config] 二进制文件路径已指定为: " <<
            sim_config.config_elfFilePath << std::endl;
    }
    
    env = std::getenv("NPC_CONFIG_DIFFTEST_SO_FILE_PATH");
    if (env) {
        sim_config.config_difftestSoFilePath =
            std::move(std::string(env));
        std::cout << "[config] DiffTest 动态链接库文件路径已指定为: " <<
            sim_config.config_difftestSoFilePath << std::endl;
    }
    
    env = std::getenv("NPC_CONFIG_WAVE_FILE_PATH");
    if (env) {
        sim_config.config_waveFilePath =
            std::move(std::string(env));
        std::cout << "[config] 波形文件输出路径已指定为: " <<
            sim_config.config_waveFilePath << std::endl;
    }
}

/**
 * @brief 检查必需的配置选项是否均已设置。
 * 
 * @return true 必需的配置选项已设置
 * @return false 存在未设置的必需配置选项
 */
static bool checkRequiredConfig() {
    if (sim_config.config_elfFilePath.empty()) {
        std::cerr << "未指定 NPC_CONFIG_ELF_FILE_PATH 环境变量, 请指定二进制文件路径!" << std::endl;
        return false;
    }

    return true;
}

size_t binFileSize = 0;

/**
 * @brief 程序的入口函数。
 * 
 * @param argc 程序参数数量
 * @param argv 程序参数
 * @return int 程序退出状态码
 */
int main(int argc, const char *argv[]) {
    const char *binPath, *sdbEnabled;
    bool sdb, result;

    Verilated::commandArgs(argc, argv);

    verContext = new VerilatedContext;
    verContext->commandArgs(argc, argv);
    
    std::cout << "正在加载配置选项..." << std::endl;
    binPath = std::getenv("NPC_BIN_PATH");
    sdbEnabled = std::getenv("NPC_SDB_ENABLED");
    if (!binPath) {
        std::cerr << "未通过 NPC_BIN 环境变量指定二进制文件路径，将使用默认路径" DEFAULT_BIN_PATH << std::endl;
        binPath = DEFAULT_BIN_PATH;
    }
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

    std::cout << "正在加载二进制文件到主存..." << std::endl;
    if (!initMemory(binPath, &binFileSize)) {
        std::cerr << "二进制文件加载失败，退出..." << std::endl;
        return EXIT_FAILURE;
    }
    std::cout << "二进制文件加载成功，大小为 " <<
        std::dec << binFileSize << " 字节" << std::endl;

    result = simulate(sdb);

    delete verContext;

    return result ? EXIT_SUCCESS : EXIT_FAILURE;
}
