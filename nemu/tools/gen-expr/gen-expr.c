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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <string.h>

// We use C23 standard here, so `bool` type is available.
#define true ((bool) 1)
#define false ((bool) 0)

// this should be enough
static char buf[65536] = {};
static int buf_len = 0;
static char code_buf[65536 + 128] = {}; // a little larger than `buf`
static char *code_format =
"#include <stdio.h>\n"
"int main() { "
"  unsigned result = %s; "
"  printf(\"%%u\", result); "
"  return 0; "
"}";
static int last_result = 0;

static inline uint32_t choose(uint32_t n) {
  return rand() % n;
}

static void gen(char c) {
  buf[buf_len++] = c;
  buf[buf_len] = '\0';
}

static int gen_num(bool avoid_zero) {
  int num, i;
  char num_str[32];

  if (avoid_zero) {
    num = last_result == 0 ? 0 : choose(last_result) + 1;
  } else {
    num = choose(10);
  }
  if (num == last_result) {
    num = (num + 1) % 10;
  }
  if (num == 0 && avoid_zero) {
    num = 1;
  }
  sprintf(num_str, "%d", num);
  for (i = 0; i < strlen(num_str); i++) {
    gen(num_str[i]);
  }

  return num;
}

static char gen_rand_op(void) {
  char op;

  switch (choose(4)) {
    case 0: op = '+'; break;
    case 1: op = '-'; break;
    case 2: op = '*'; break;
    default: op = '/';
  }
  gen(op);

  return op;
}

static int eval(int opn0, char op, int opn1) {
  switch (op) {
    case '+': return opn0 + opn1;
    case '-': return opn0 - opn1;
    case '*': return opn0 * opn1;
    case '/': return opn0 / opn1;
    default: return -1;
  }
}

static int gen_rand_expr(bool avoid_zero) { // 生成表达式，同时返回所生成的表达式的求值结果
  int result, opn0, opn1;
  char op;

  switch (choose(3)) {
    case 0:
      result = gen_num(avoid_zero);
      break;
    case 1:
      gen('(');
      result = gen_rand_expr(avoid_zero);
      gen(')');
      break;
    default:
      opn0 = gen_rand_expr(true);
      op = gen_rand_op();
      opn1 = gen_rand_expr((op == '*' || op == '/') ? true : avoid_zero);
      result = eval(opn0, op, opn1);
  }
  last_result = result;

  return result;
}

int main(int argc, char *argv[]) {
  int seed = time(0);
  srand(seed);
  int loop = 1;
  if (argc > 1) {
    sscanf(argv[1], "%d", &loop);
  }
  int i;
  for (i = 0; i < loop; i ++) {
    buf_len = 0;
    last_result = 0;
    gen_rand_expr(false);

    sprintf(code_buf, code_format, buf);

    FILE *fp = fopen("/tmp/.code.c", "w");
    assert(fp != NULL);
    fputs(code_buf, fp);
    fclose(fp);

    int ret = system("gcc /tmp/.code.c -o /tmp/.expr");
    if (ret != 0) continue;

    fp = popen("/tmp/.expr", "r");
    assert(fp != NULL);

    int result;
    ret = fscanf(fp, "%d", &result);
    pclose(fp);

    printf("%u %s\n", result, buf);
  }
  return 0;
}
