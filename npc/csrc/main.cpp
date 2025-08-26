#include <iostream>
#include <cstdlib>
#include <cstring>
#include <sim_top.hpp>
#include <memory.hpp>

int main(int argc, const char *argv[]) {
    const char *binPath, *sdbEnabled;
    bool sdb;

    Verilated::commandArgs(argc, argv);
    
    std::cout << "正在加载二进制文件到主存..." << std::endl;
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

    if (!initMemory(binPath)) {
        std::cerr << "二进制文件加载失败，退出..." << std::endl;
        return EXIT_FAILURE;
    }

    return simulate(sdb) ? EXIT_SUCCESS : EXIT_FAILURE;
}
