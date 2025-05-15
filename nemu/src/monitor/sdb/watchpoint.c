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

#include "sdb.h"

#define NR_WP 32
#define NR_EXPR_LEN 1024

typedef struct watchpoint {
  int NO;
  struct watchpoint *next;
  char expr[NR_EXPR_LEN];
} WP;

static WP wp_pool[NR_WP] = {};
// head: 正在被使用的watchpoint链表头; free_: 空闲的watchpoint链表头
static WP *head = NULL, *free_ = NULL;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
    memset(wp_pool[i].expr, 0, NR_EXPR_LEN * sizeof(char));
  }

  head = NULL;
  free_ = wp_pool;
}

void print_wp_pool(void) {
  WP *cur;

  cur = head;
  printf("Watchpoints:\n");
  if (cur) {
    while (cur) {
      printf("Watchpoint %d: %s\n", cur->NO, cur->expr);
      cur = cur->next;
    }
  } else {
    printf("No watchpoint at present.\n");
  }
}
