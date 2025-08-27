#ifndef __SIM_HPP__
#define __SIM_HPP__ 1

#include <verilated.h>

#include "VProcessorCore.h"

extern VProcessorCore *top;
extern bool sim_halt;

#define DEFAULT_BIN_PATH "build/program.bin"

/**
 * @brief 执行一步仿真，执行一个时钟周期。
 */
void simStep();

/**
 * @brief 重置仿真环境。
 * 
 * @param n 需要进行的时钟周期数
 */
void simReset(int n);

/**
 * @brief 执行一个时钟周期的仿真。
 */
bool simExecOnce();

/**
 * @brief 执行若干时钟周期的仿真。
 * 
 * @param n 需要进行仿真的时钟周期数
 */
void simExec(uint64_t n);

/**
 * @brief 开始仿真主流程。
 * 
 * @param sdbEnabled 是否启用 SDB
 * @return true 成功
 * @return false 失败
 */
bool simulate(bool sdbEnabled);

#endif /* __SIM_HPP__ */
