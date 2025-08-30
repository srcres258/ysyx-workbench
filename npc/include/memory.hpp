#ifndef __MEMORY_HPP__
#define __MEMORY_HPP__ 1

#include <common.hpp>

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
#define PAGE_MASK (PAGE_SIZE - 1)

/**
 * @brief 模拟内存地址偏移，处理器默认从该地址处开始执行（取第一条指令）
 */
static const uint32_t MEMORY_OFFSET = 0x80000000;
/**
 * @brief 定义计算机总共主存大小 (包括用于 MMIO 的部分)，单位为字节。
 */
static const uint32_t MEMORY_SIZE = 1 * 1024 * 1024 * 1024; // 1GB
/**
 * @brief 模拟计算机物理主存大小，单位为字节。
 */
static const uint32_t PHYS_MEMORY_SIZE = 1024 * 64;

/**
 * 模拟计算机主存，内含一段 RV32I 指令集的机器码，以小端序存放。
 */
extern uint8_t memory[];

/**
 * @brief 判断给定主存地址是否位于物理主存地址范围内。
 * 
 * @param addr 主存地址
 * @return true 位于物理主存地址范围内
 * @return false 位于物理主存地址范围外
 */
static bool isPhysMemoryAddr(addr_t addr) {
    return addr - MEMORY_OFFSET < PHYS_MEMORY_SIZE;
}

/**
 * @brief 从给定二进制文件（bin）加载内容到主存中。
 * 
 * @param filename 文件名
 * @param fileSize 文件大小缓冲区，用以保存读取到的文件大小数据
 * @return true 成功
 * @return false 失败
 */
bool initMemory(const char *filename, size_t *fileSize);

/**
 * @brief 从主存中读取内容。
 * 
 * @param addr 主存地址（包含了内存地址偏移的）
 * @param len 读取长度（单位为字节）
 * @return word_t 读取到的内容
 */
word_t readMemory(addr_t addr, int len);

/**
 * @brief 向主存中写入内容。
 * 
 * @param addr 主存地址（包含了内存地址偏移的）
 * @param len 写入长度（单位为字节）
 * @param data 将要写入的内容
 */
void writeMemory(addr_t addr, int len, word_t data);

#endif /* __MEMORY_HPP__ */