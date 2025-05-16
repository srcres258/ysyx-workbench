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

#include <common.h>
#include <stdio.h>
#include <stdlib.h>

void init_monitor(int, char *[]);
void am_init_monitor();
void engine_start();
int is_exit_status_bad();

void init_regex(); // for test
word_t expr(char *e, bool *success); // for test

int main(int argc, char *argv[]) {
//   /* Initialize the monitor. */
// #ifdef CONFIG_TARGET_AM
//   am_init_monitor();
// #else
//   init_monitor(argc, argv);
// #endif

//   /* Start engine. */
//   engine_start();

//   return is_exit_status_bad();

  FILE *fp;
  char buffer[1024], *num, *expression;
  int expect, actual, line;
  bool success;

  init_regex();

  fp = fopen("/home/srcres/Coding/Learn/ysyx-workbench/nemu/tools/gen-expr/build/input", "r");
  if (!fp) {
    perror("Failed to open file");
    return EXIT_FAILURE;
  }

  line = 1;
  while (fgets(buffer, sizeof(buffer), fp)) {
    num = strtok(buffer, " ");
    expression = num + strlen(num) + 1;
    expression[strlen(expression) - 1] = '\0';
    expect = atoi(num);
    actual = expr(expression, &success);
    if (!success) {
      printf("Failed to evaluate expression at line %d:\n", line);
      printf("%s %s\n", num, expression);
      break;
    } else if (actual != expect) {
      printf("Failed to evaluate expression at line %d:\n", line);
      printf("%s %s\n", num, expression);
      printf("Expected: %d, Actual: %d\n", expect, actual);
      break;
    }
    printf("Passed: %s %s\n", num, expression);
    line++;
  }

  fclose(fp);

  return EXIT_SUCCESS;
}
