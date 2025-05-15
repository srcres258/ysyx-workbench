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

static void append_char(char c);
static void gen_non_zero_num(void);
static void gen_num(void);
static void gen_non_zero_factor(void);
static void gen_non_zero_term(void);
static void gen_non_zero_expr(void);
static void gen_factor(void);
static void gen_term(void);
static void gen_expr(void);
static void gen_rand_expr(void);

static void append_char(char c) {
  if (buf_len >= 65535) {
    return;
  }
  buf[buf_len++] = c;
  buf[buf_len] = '\0';
}

static void gen_non_zero_num(void) {
  char c;
  int count;

  c = '1' + rand() % 9;
  append_char(c);
  count = 1;
  while (rand() % 3 != 0 && count <= 3) {
    c = '0' + rand() % 10;
    append_char(c);
    count++;
  }
}

static void gen_num(void) {
  if (rand() % 10 == 0) {
    append_char('0');
  } else {
    gen_non_zero_num();
  }
}

static void gen_non_zero_term(void) {
  gen_non_zero_factor();
  if (rand() % 10 == 0) {
    append_char('*');
    gen_non_zero_term();
  }
}

static void gen_non_zero_expr(void) {
  gen_non_zero_term();
  if (rand() % 10 == 0) {
    append_char('+');
    gen_non_zero_expr();
  }
}

static void gen_non_zero_factor(void) {
  if (rand() % 2 == 0) {
    gen_non_zero_num();
  } else {
    append_char('(');
    gen_non_zero_expr();
    append_char(')');
  }
}

static void gen_factor(void) {
  if (rand() % 2 == 0) {
    gen_num();
  } else {
    append_char('(');
    gen_non_zero_expr();
    append_char(')');
  }
}

static void gen_term(void) {
  gen_factor();
  while (rand() % 10 == 0) {
    if (rand() % 2 == 0) {
      append_char('*');
      gen_factor();
    } else {
      append_char('/');
      gen_non_zero_factor();
    }
  }
}

static void gen_expr(void) {
  gen_term();
  while (rand() % 10 == 0) {
    if (rand() % 2 == 0) {
      append_char('+');
    } else {
      append_char('-');
    }
    gen_term();
  }
}

static void gen_rand_expr(void) {
  buf[0] = '\0';
  buf_len = 0;
  gen_expr();
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
    gen_rand_expr();

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
