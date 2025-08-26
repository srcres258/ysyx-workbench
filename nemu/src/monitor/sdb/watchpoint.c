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

static WP wp_pool[NR_WP] = {};
// head: 正在被使用的watchpoint链表头; free_: 空闲的watchpoint链表头
static WP *head = NULL, *free_ = NULL;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
    memset(wp_pool[i].expr, 0, NR_EXPR_LEN * sizeof(char));
    wp_pool[i].val = 0;
    wp_pool[i].evaluated = false;
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
      printf("Value: %ld, Evaluated: %s\n", cur->val, cur->evaluated ? "true" : "false");
      cur = cur->next;
    }
  } else {
    printf("No watchpoint at present.\n");
  }
}

WP *new_wp(void) {
  WP *result, *cur;

  if (!free_) {
    printf("Error: no free watchpoint.\n");
    return NULL;
  }

  result = free_;
  free_ = free_->next;
  result->next = NULL;
  result->evaluated = false;
  if (head) {
    for (cur = head; cur->next; cur = cur->next);
    cur->next = result;
  } else {
    head = result;
  }

  return result;
}

void free_wp(WP *wp) {
  WP *cur, *prev;

  if (!wp) {
    return;
  }
  if (!head) {
    return;
  }

  prev = NULL;
  for (cur = head; cur; prev = cur, cur = cur->next) {
    if (cur == wp) {
      if (prev) {
        prev->next = cur->next;
      } else {
        head = cur->next;
      }
      cur->next = NULL;
      break;
    }
  }
  if (!cur) {
    return;
  }
  if (free_) {
    for (cur = free_; cur->next; cur = cur->next);
    cur->next = wp;
  } else {
    free_ = wp;
  }
}

WP *find_wp(int NO) {
  WP *cur;

  for (cur = head; cur; cur = cur->next) {
    if (cur->NO == NO) {
      break;
    }
  }

  return cur;
}

void sdb_eval_and_update_wp(void) {
  WP *cur;
  int64_t val;
  bool success;

  for (cur = head; cur; cur = cur->next) {
    val = expr(cur->expr, &success);
    if (!success) {
      continue;
    }
    if (cur->evaluated && val != cur->val) {
      nemu_state.state = NEMU_STOP;
      printf("Watchpoint %d triggered: %s\n", cur->NO, cur->expr);
      printf("Old value: %ld, new value: %ld\n", cur->val, val);
    }
    cur->val = val;
    cur->evaluated = true;
  }
}
