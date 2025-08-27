#ifndef __MEMORY_HPP__
#define __MEMORY_HPP__ 1

#include <common.hpp>

/**
 * @brief 模拟内存地址偏移，处理器默认从该地址处开始执行（取第一条指令）
 */
const uint32_t MEMORY_OFFSET = 0x80000000;
/**
 * @brief 模拟计算机主存大小，单位为字节。
 */
const uint32_t MEMORY_SIZE = 1024 * 64;

/**
 * 模拟计算机主存，内含一段 RV32I 指令集的机器码，以小端序存放。
 */
extern uint8_t memory[MEMORY_SIZE];

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
 * @return uint32_t 读取到的内容
 */
word_t readMemory(addr_t addr);

/**
 * @brief 向主存中写入内容。
 * 
 * @param addr 主存地址（包含了内存地址偏移的）
 * @param data 将要写入的内容
 */
void writeMemory(addr_t addr, word_t data);

#endif /* __MEMORY_HPP__ */