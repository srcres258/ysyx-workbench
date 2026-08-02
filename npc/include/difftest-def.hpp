#ifndef __DIFFTEST_DEF_HPP__
#define __DIFFTEST_DEF_HPP__ 1

#include <cstdint>
#include <cstddef>

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

enum DiffTestMemRegionType {
    DIFFTEST_MEM_REGION_RAM = 0,
    DIFFTEST_MEM_REGION_MMIO = 1,
};

enum DiffTestSkipReason {
    DIFFTEST_SKIP_REASON_MMIO = 0,
    DIFFTEST_SKIP_REASON_TIMER_DEVICE = 1,
    DIFFTEST_SKIP_REASON_NONDETERMINISTIC = 2,
};

struct DiffTestMemRegion {
    uint64_t base;
    uint64_t size;
    DiffTestMemRegionType type;
};

// 除通用寄存器外还要算 pc, 所以总共寄存器数量要 +1
#define DIFFTEST_REG_SIZE (sizeof(RISCV_GPR_TYPE) * (RISCV_GPR_NUM + 1))

#endif /* __DIFFTEST_DEF_HPP__ */
