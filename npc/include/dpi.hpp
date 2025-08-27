#ifndef __DPI_HPP__
#define __DPI_HPP__ 1

#include <cstdint>

/**
 * @brief 访存类型位长。
 */
const uint32_t LS_TYPE_LEN = 4;

/**
 * @brief 访存单元的访存类型。
 */
enum LSType {
    LS_L_W = 0,
    LS_L_H = 1,
    LS_L_HU = 2,
    LS_L_B = 3,
    LS_L_BU = 4,
    LS_S_W = 5,
    LS_S_H = 6,
    LS_S_B = 7
};

const uint32_t LS_UNKNOWN = 1 << LS_TYPE_LEN - 1;

#endif /* __DPI_HPP__ */
