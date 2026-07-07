#ifndef __UTILS__CALL_FRAME_INFO_HPP__
#define __UTILS__CALL_FRAME_INFO_HPP__ 1

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
struct CallFrameInfo {
    addr_t funcAddr;
    std::string funcName;
    addr_t retAddr;
    word_t callerSp;
};

#endif /* __UTILS__CALL_FRAME_INFO_HPP__ */
