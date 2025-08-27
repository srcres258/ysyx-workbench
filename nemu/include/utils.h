/***************************************************************************************
* Copyright (c) 2014-2024 Zihao Yu, Nanjing University
*
* NEMU is licensed under Mulan PSL v2.
* You can use this software according to the terms and conditions of the Mulan PSL v2.
* You may obtain a copy of Mulan PSL v2 at:
*          http://license.coscl.org.cn/MulanPSL2
*
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
* EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
* MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
*
* See the Mulan PSL v2 for more details.
***************************************************************************************/

#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdbool.h>
#include <stdint.h>
#include <common.h>
#include <utils/ringbuffer.h>
#include <utils/symbol.h>
#include <utils/call_stack.h>

// ----------- state -----------

enum { NEMU_RUNNING, NEMU_STOP, NEMU_END, NEMU_ABORT, NEMU_QUIT };

#define CALL_STACK_MAX_DEPTH 1024

typedef struct {
  int state;
  vaddr_t halt_pc;
  uint32_t halt_ret;
  IFDEF(CONFIG_ITRACE, RingBuffer *iringbuf);
  IFDEF(CONFIG_MTRACE, bool mtrace_available);
  IFDEF(CONFIG_MTRACE, char mtrace_logbuf[4096]);
  IFDEF(CONFIG_FTRACE, Symbol ftrace_func_syms[1024]);
  IFDEF(CONFIG_FTRACE, size_t ftrace_func_syms_size);
  IFDEF(CONFIG_FTRACE, CallStackInfo ftrace_call_stack[CALL_STACK_MAX_DEPTH]);
  IFDEF(CONFIG_FTRACE, size_t ftrace_call_stack_top);
  IFDEF(CONFIG_FTRACE, char ftrace_logbuf[4096]);
  IFDEF(CONFIG_FTRACE, bool ftrace_available);
} NEMUState;

extern NEMUState nemu_state;

#ifdef CONFIG_ITRACE

#define NEMU_IRINGBUF_SIZE 1024

bool nemu_iringbuf_init(void);

void nemu_iringbuf_destroy(void);

void nemu_iringbuf_dump(void);

#define nemu_iringbuf (nemu_state.iringbuf)

#endif

#ifdef CONFIG_FTRACE

bool nemu_ftrace_record_and_log(CallType type, word_t src_addr, word_t addr);

#endif

// ----------- timer -----------

uint64_t get_time();

// ----------- log -----------

#define ANSI_FG_BLACK   "\33[1;30m"
#define ANSI_FG_RED     "\33[1;31m"
#define ANSI_FG_GREEN   "\33[1;32m"
#define ANSI_FG_YELLOW  "\33[1;33m"
#define ANSI_FG_BLUE    "\33[1;34m"
#define ANSI_FG_MAGENTA "\33[1;35m"
#define ANSI_FG_CYAN    "\33[1;36m"
#define ANSI_FG_WHITE   "\33[1;37m"
#define ANSI_BG_BLACK   "\33[1;40m"
#define ANSI_BG_RED     "\33[1;41m"
#define ANSI_BG_GREEN   "\33[1;42m"
#define ANSI_BG_YELLOW  "\33[1;43m"
#define ANSI_BG_BLUE    "\33[1;44m"
#define ANSI_BG_MAGENTA "\33[1;45m"
#define ANSI_BG_CYAN    "\33[1;46m"
#define ANSI_BG_WHITE   "\33[1;47m"
#define ANSI_NONE       "\33[0m"

#define ANSI_FMT(str, fmt) fmt str ANSI_NONE

#define log_write(...) IFDEF(CONFIG_TARGET_NATIVE_ELF, \
  do { \
    extern FILE* log_fp; \
    extern bool log_enable(); \
    if (log_enable() && log_fp != NULL) { \
      fprintf(log_fp, __VA_ARGS__); \
      fflush(log_fp); \
    } \
  } while (0) \
)

#define _Log(...) \
  do { \
    printf(__VA_ARGS__); \
    log_write(__VA_ARGS__); \
  } while (0)

// ----------- monitor -----------

void sdb_eval_and_update_wp(void);

#endif
