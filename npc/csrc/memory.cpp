#include <iostream>
#include <fstream>
#include <utils.hpp>
#include <device/mmio.hpp>
#include <memory.hpp>

uint8_t memory[PHYS_MEMORY_SIZE] = { 0 };

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
    std::cout << "文件大小: " << std::dec << size << std::endl;
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
 * @param len 读取长度（单位为字节）
 * @return word_t 读取到的内容
 */
word_t readMemory(addr_t addr, int len) {
    uint32_t result;

    result = 0;

    if (isPhysMemoryAddr(addr)) {
        Assert(len == 1 || len == 2 || len == 4, "Invalid memory read length");

        result |= memory[addr - MEMORY_OFFSET];
        if (len >= 2) {
            result |= memory[addr - MEMORY_OFFSET + 1] << 8;
        }
        if (len >= 4) {
            result |= memory[addr - MEMORY_OFFSET + 2] << 16;
            result |= memory[addr - MEMORY_OFFSET + 3] << 24;
        }
        return result;
    }

    result = device_mmio_read(addr, len);
    return result;
}

/**
 * @brief 向主存中写入内容。
 * 
 * @param addr 主存地址（包含了内存地址偏移的）
 * @param len 写入长度（单位为字节）
 * @param data 将要写入的内容
 */
void writeMemory(addr_t addr, int len, word_t data) {
    word_t val;
    size_t i;

    val = data;

    if (isPhysMemoryAddr(addr)) {
        for (i = 0; i < len; i++) {
            memory[addr - MEMORY_OFFSET + i] = val & 0xFF;
            val >>= 8;
        }
        return;
    }
    
    device_mmio_write(addr, len, val);
}
