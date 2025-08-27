#ifndef __UTILS_CALL_STACK_HPP__
#define __UTILS_CALL_STACK_HPP__ 1

#include <string>
#include <common.hpp>

/**
 * @brief 调用栈类型。
 * 
 */
enum CallType {
    CALL_TYPE_CALL,
    CALL_TYPE_TAIL,
    CALL_TYPE_RET
};

/**
 * @brief 调用栈信息。
 */
struct CallStackInfo {
    word_t addr;
    std::string name;
};

#endif
