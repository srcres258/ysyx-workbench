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

#ifndef __DEBUG_H__
#define __DEBUG_H__

#include <common.h>
#include <stdio.h>
#include <utils.h>
#include <errno.h>

#define Log(format, ...) \
    _Log(ANSI_FMT("[%s:%d %s] " format, ANSI_FG_BLUE) "\n", \
        __FILE__, __LINE__, __func__, ## __VA_ARGS__)

#define Assert(cond, format, ...) \
  do { \
    if (!(cond)) { \
      MUXDEF(CONFIG_TARGET_AM, printf(ANSI_FMT(format, ANSI_FG_RED) "\n", ## __VA_ARGS__), \
        (fflush(stdout), fprintf(stderr, ANSI_FMT(format, ANSI_FG_RED) "\n", ##  __VA_ARGS__))); \
      IFNDEF(CONFIG_TARGET_AM, extern FILE* log_fp; fflush(log_fp)); \
      extern void assert_fail_msg(); \
      assert_fail_msg(); \
      assert(cond); \
    } \
  } while (0)

#define panic(format, ...) Assert(0, format, ## __VA_ARGS__)

#define TODO() panic("please implement me")

#define Log_err(msg, ...) Log(ANSI_FMT(msg, ANSI_FG_RED), ## __VA_ARGS__)

#define Log_warn(msg, ...) Log(ANSI_FMT(msg, ANSI_FG_YELLOW), ## __VA_ARGS__)

#define Log_info(msg, ...) Log(ANSI_FMT(msg, ANSI_FG_WHITE), ## __VA_ARGS__)

#ifdef CONFIG_NDEBUG

#define Log_debug(msg, ...)

#else

#define Log_debug(msg, ...) Log(ANSI_FMT(msg, ANSI_FG_BLACK), ## __VA_ARGS__)

#endif

#define check(cond, msg, ...)         \
  do {                                \
    if (!(cond)) {                    \
      Log_err(msg, ## __VA_ARGS__);   \
      errno = 0;                      \
      goto error;                     \
    }                                 \
  } while (0)

#define sentinel(msg, ...)            \
  do {                                \
    Log_err(msg, ## __VA_ARGS__);     \
    errno = 0;                        \
    goto error;                       \
  } while (0)

#define check_mem(cond) check((cond), "Out of memory.")

#define check_debug(cond, msg, ...)   \
  do {                                \
    if (!(cond)) {                    \
      Log_debug(msg, ## __VA_ARGS__); \
      errno = 0;                      \
      goto error;                     \
    }                                 \
  } while (0)

#endif
