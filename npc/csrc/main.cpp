#include <iostream>
#include <cstdlib>
#include <cstring>
#include <sim_top.hpp>
#include <memory.hpp>
#include <utils.hpp>

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

/**
 * @brief 程序的入口函数。
 * 
 * @param argc 程序参数数量
 * @param argv 程序参数
 * @return int 程序退出状态码
 */
int main(int argc, const char *argv[]) {
    const char *binPath, *sdbEnabled;
    bool sdb;

    Verilated::commandArgs(argc, argv);
    
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
    if (!initMemory(binPath)) {
        std::cerr << "二进制文件加载失败，退出..." << std::endl;
        return EXIT_FAILURE;
    }

    return simulate(sdb) ? EXIT_SUCCESS : EXIT_FAILURE;
}
