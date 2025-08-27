#ifndef __DIFFTEST_DEF_HPP__
#define __DIFFTEST_DEF_HPP__ 1

#include <cstdint>

#define __EXPORT __attribute__((visibility("default")))
#define __EXPORT_C extern "C" __EXPORT

/**
 * @brief DiffTest 的类型。
 */
enum DiffTestType {
    /**
     * @brief DiffTest: 从 REF 到 DUT。
     */
    DIFFTEST_TO_DUT,
    /**
     * @brief DiffTest: 从 DUT 到 REF。
     */
    DIFFTEST_TO_REF
};

// 除通用寄存器外还要算 pc, 所以总共寄存器数量要 +1
#define DIFFTEST_REG_SIZE (sizeof(RISCV_GPR_TYPE) * (RISCV_GPR_NUM + 1))

#endif /* __DIFFTEST_DEF_HPP__ */
