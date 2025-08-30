#ifndef __UTILS__STAGE_HPP__
#define __UTILS__STAGE_HPP__ 1

#include <cstdint>

#define STAGE_LEN 3

enum Stage {
    STAGE_IF = 0,
    STAGE_ID = 1,
    STAGE_EX = 2,
    STAGE_MA = 3,
    STAGE_WB = 4,
    STAGE_UPC = 5
};

#endif
