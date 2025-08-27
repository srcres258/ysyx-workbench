#include <iostream>
#include <fstream>
#include <memory.hpp>

uint8_t memory[MEMORY_SIZE] = { 0 };

/**
 * @brief 从给定二进制文件（bin）加载内容到主存中。
 * 
 * @param filename 文件名
 * @param fileSize 文件大小缓冲区，用以保存读取到的文件大小数据
 * @return true 成功
 * @return false 失败
 */
bool initMemory(const char *filename, size_t *fileSize) {
    size_t size;

    std::ifstream f(filename, std::ios::binary | std::ios::ate);

    if (!f) {
        std::cerr << "无法打开文件: " << filename << std::endl;
        return false;
    }
    std::cout << "正在加载文件: " << filename << std::endl;
    size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (f.read((char *) memory, size)) {
        std::cout << "文件加载成功." << std::endl;
    } else {
        std::cerr << "文件加载失败!" << std::endl;
        return false;
    }

    if (fileSize) {
        *fileSize = size;
    }
    return true;
}

/**
 * @brief 从主存中读取内容。
 * 
 * @param addr 主存地址（包含了内存地址偏移的）
 * @return uint32_t 读取到的内容
 */
word_t readMemory(addr_t addr) {
    uint32_t result;

    result = memory[addr - MEMORY_OFFSET];
    result |= memory[addr - MEMORY_OFFSET + 1] << 8;
    result |= memory[addr - MEMORY_OFFSET + 2] << 16;
    result |= memory[addr - MEMORY_OFFSET + 3] << 24;

    return result;
}

/**
 * @brief 向主存中写入内容。
 * 
 * @param addr 主存地址（包含了内存地址偏移的）
 * @param data 将要写入的内容
 */
void writeMemory(addr_t addr, word_t data) {
    uint32_t val;

    val = data;
    memory[addr - MEMORY_OFFSET] = val & 0xFF;
    val >>= 8;
    memory[addr - MEMORY_OFFSET + 1] = val & 0xFF;
    val >>= 8;
    memory[addr - MEMORY_OFFSET + 2] = val & 0xFF;
    val >>= 8;
    memory[addr - MEMORY_OFFSET + 3] = val;
}
