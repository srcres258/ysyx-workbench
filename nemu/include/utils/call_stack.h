#ifndef __UTILS_CALL_STACK_H__
#define __UTILS_CALL_STACK_H__

#include <common.h>

typedef enum {
    CALL_TYPE_CALL,
    CALL_TYPE_RET
} CallType;

typedef struct {
    word_t addr;
    char name[256];
} CallStackInfo;

#endif