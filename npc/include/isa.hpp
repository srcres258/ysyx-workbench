#ifndef __ISA_HPP__
#define __ISA_HPP__ 1

#include <common.hpp>

/**
 * @brief 根据寄存器索引，读取寄存器的值。
 * 
 * @param idx 寄存器索引
 * @return word_t 寄存器的值；若寄存器不存在或读取失败则返回0
 */
word_t isaRegVal(size_t idx);

/**
 * @brief 根据寄存器索引，获取寄存器名称。
 * 
 * @param idx 寄存器索引
 * @return word_t 寄存器名称；若寄存器不存在或读取失败则返回空指针
 */
const char *isaRegName(size_t idx);

/**
 * @brief ISA：获取给定名称的寄存器中的值。
 * 
 * @param s 寄存器名称
 * @param success 操作是否成功
 * @return word_t 获取到的寄存器值；若获取失败则返回0
 */
word_t isaRegStr2Val(const char *s, bool *success);

/**
 * @brief ISA：将所有寄存器中的值打印到标准输出。
 */
void isaRegDisplay();

#endif /* __ISA_HPP__ */