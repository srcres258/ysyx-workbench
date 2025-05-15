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

#include <isa.h>
#include <cpu/cpu.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <utils.h>
#include <memory/vaddr.h>
#include "sdb.h"

static int is_batch_mode = false;

/**
 * @brief 工具函数，将给定的十六进制内存地址字符串（没有`0x`前缀）
 * 转换为数值表示的内存地址。
 * 
 * @param str 十六进制内存地址字符串。
 * @param success 若转换成功，设置该指针为`true`，否则为`false`。
 * @return int 转换后的数字。若转换失败返回`0`。
 */
static vaddr_t hex_addr_to_num(const char *str, bool *success) {
  const char *cur;
  char c;
  unsigned char uc;
  vaddr_t result, value;

  if (str == NULL || *str == '\0') {
    return -1; // 空指针或空字符串，转换失败
  }

  result = 0;
  for (cur = str; *cur != '\0'; cur++) {
    c = *cur;
    uc = (unsigned char) c;
    if (!isdigit(uc) && !isalpha(uc)) {
      // 既不是数字也不是字母，输入非法，转换失败
      *success = false;
      return 0;
    }
    if (isdigit(uc)) {
      value = c - '0';
    } else if (c >= 'A' && c <= 'F') {
      value = 10 + c - 'A';
    } else if (c >= 'a' && c <= 'f') {
      value = 10 + c - 'a';
    } else {
      *success = false;
      return 0; // 遇到非法字符，转换失败
    }

    result <<= 4;
    result |= value;
  }

  *success = true;
  return result;
}

static void print_bad_arguments(void) {
  printf("Bad command arguments. Please type 'help' for usage.\n");
}

void init_regex();
void init_wp_pool();
void print_wp_pool(void);

/* We use the `readline' library to provide more flexibility to read from stdin. */
static char* rl_gets() {
  static char *line_read = NULL;

  if (line_read) {
    free(line_read);
    line_read = NULL;
  }

  line_read = readline("(nemu) ");

  if (line_read && *line_read) {
    add_history(line_read);
  }

  return line_read;
}

static int cmd_c(char *args) {
  cpu_exec(-1);
  return 0;
}


static int cmd_q(char *args) {
  nemu_state.state = NEMU_QUIT;
  return -1;
}

static int cmd_help(char *args);

/**
 * @brief 单步执行
 * 
 * 让程序单步执行N条指令后暂停执行,
 * 当N没有给出时, 缺省为1
 * 
 * 格式：si [N]
 * 
 * 使用举例：si 10
 * 
 * @param args 指令的条数
 * @return int 始终返回0
 */
static int cmd_si(char *args) {
  int n;

  n = args ? atoi(args) : 1;
  cpu_exec(n);

  return 0;
}

/**
 * @brief 打印程序状态
 * 
 * 打印寄存器状态
 * 打印监视点信息
 * 
 * 格式：info SUBCMD
 * 
 * 使用举例：
 * info r
 * info w
 * 
 * @param args
 * @return int 始终返回0
 */
static int cmd_info(char *args) {
  if (args) {
    if (strcmp(args, "r") == 0) {
      isa_reg_display();
      return 0;
    } else if (strcmp(args, "w") == 0) {
      print_wp_pool();
      return 0;
    }
  }

  print_bad_arguments();
  return 0;
}

/**
 * @brief 扫描内存
 * 
 * 求出表达式EXPR的值, 将结果作为起始内存
 * 地址, 以十六进制形式输出连续的N个4字节
 * 
 * 注：GDB相比, 我们在这里做了简化, 更改了命令的格式
 * 
 * 格式：x N EXPR
 * 
 * 使用举例：x 10 $esp
 * 
 * @param args 
 * @return int 
 */
static int cmd_x(char *args) {
  char *N_str, *EXPR_str, *hex_str;
  vaddr_t addr, cur_addr;
  int N, i;
  uint32_t value;
  bool success;

  if (!args) {
    return 0;
  }

  N_str = strtok(args, " ");
  EXPR_str = N_str ? N_str + strlen(N_str) + 1 : NULL;
  hex_str = EXPR_str + 2; // 去掉前缀"0x"
  addr = hex_addr_to_num(hex_str, &success);
  N = atoi(N_str);

  if (!success || N <= 0) {
    print_bad_arguments();
    return 0;
  }

  printf("Memory scan: addr=%08X, N=%d\n", addr, N);
  cur_addr = addr;
  for (i = 0; i < N; i++) {
    value = vaddr_read(cur_addr, 4);
    printf("%08X: %08X\n", cur_addr, value);
    cur_addr += 4;
  }

  return 0;
}

/**
 * @brief 表达式求值
 * 
 * 求出表达式EXPR的值
 * 
 * 格式：p EXPR
 * 
 * 使用举例：p $eax + 1
 * 
 * @param args 
 * @return int 
 */
static int cmd_p(char *args) {
  // TODO

  return 0;
}

/**
 * @brief 设置监视点
 * 
 * 当表达式EXPR的值发生变化时, 暂停程序执行
 * 
 * 格式：w EXPR
 * 
 * 使用举例：w *0x2000
 * 
 * @param args 
 * @return int 
 */
static int cmd_w(char *args) {
  // TODO

  return 0;
}

/**
 * @brief 删除监视点
 * 
 * 删除序号为N的监视点
 * 
 * 格式：d N
 * 
 * 使用举例：d 2
 * 
 * @param args 
 * @return int 
 */
static int cmd_d(char *args) {
  // TODO

  return 0;
}

static struct {
  const char *name;
  const char *description;
  int (*handler) (char *);
} cmd_table [] = {
  { "help", "Display information about all supported commands", cmd_help },
  { "c", "Continue the execution of the program", cmd_c },
  { "q", "Exit NEMU", cmd_q },
  { "si", "Run the given number of instructions of the program and pause", cmd_si },
  { "info", "Display information about registers or watchpoints", cmd_info },
  { "x", "Display the contents of memory", cmd_x },
  { "p", "Evaluate an expression and display the result", cmd_p },
  { "w", "Set a watchpoint on an expression", cmd_w },
  { "d", "Delete a watchpoint", cmd_d }
};

#define NR_CMD ARRLEN(cmd_table)

static int cmd_help(char *args) {
  /* extract the first argument */
  char *arg = strtok(NULL, " ");
  int i;

  if (arg == NULL) {
    /* no argument given */
    for (i = 0; i < NR_CMD; i ++) {
      printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
    }
  }
  else {
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(arg, cmd_table[i].name) == 0) {
        printf("%s - %s\n", cmd_table[i].name, cmd_table[i].description);
        return 0;
      }
    }
    printf("Unknown command '%s'\n", arg);
  }
  return 0;
}

void sdb_set_batch_mode() {
  is_batch_mode = true;
}

void sdb_mainloop() {
  if (is_batch_mode) {
    cmd_c(NULL);
    return;
  }

  for (char *str; (str = rl_gets()) != NULL; ) {
    char *str_end = str + strlen(str);

    /* extract the first token as the command */
    char *cmd = strtok(str, " ");
    if (cmd == NULL) { continue; }

    /* treat the remaining string as the arguments,
     * which may need further parsing
     */
    char *args = cmd + strlen(cmd) + 1;
    if (args >= str_end) {
      args = NULL;
    }

#ifdef CONFIG_DEVICE
    extern void sdl_clear_event_queue();
    sdl_clear_event_queue();
#endif

    int i;
    for (i = 0; i < NR_CMD; i ++) {
      if (strcmp(cmd, cmd_table[i].name) == 0) {
        if (cmd_table[i].handler(args) < 0) { return; }
        break;
      }
    }

    if (i == NR_CMD) { printf("Unknown command '%s'\n", cmd); }
  }
}

void init_sdb() {
  /* Compile the regular expressions. */
  init_regex();

  /* Initialize the watchpoint pool. */
  init_wp_pool();
}
