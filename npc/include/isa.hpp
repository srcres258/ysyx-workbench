#ifndef __ISA_HPP__
#define __ISA_HPP__ 1

#include <common.hpp>
#include <processor.hpp>

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

/**
 * @brief ISA：检查目的处理器状态中的寄存器状态
 * 是否与当前仿真环境中的处理器的寄存器状态一致。
 * 
 * @param state 目的处理器状态
 * @param useNextPC PC 值是否采用下次 PC。因
 * 为我们的处理器只在 IF 阶段前更新 PC，所以执行
 * 完指令后 PC 是不会更新的，得下一次 IF 阶段时
 * 才更新。如果要与其他参考状态作比较的话，则要将
 * PC 的下次取值拿来进行对比，而非本次取值。
 * @return true 状态一致
 * @return false 状态不一致
 */
bool isaCheckRegisters(ProcessorState *state);

#endif /* __ISA_HPP__ */