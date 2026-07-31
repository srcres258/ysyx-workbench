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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdb.h"

#define NR_WP 64

static WP wp_pool[NR_WP] = {};
static WP *head = NULL, *free_ = NULL;

void init_wp_pool() {
  int i;
  for (i = 0; i < NR_WP; i ++) {
    wp_pool[i].NO = i;
    wp_pool[i].next = (i == NR_WP - 1 ? NULL : &wp_pool[i + 1]);
    wp_pool[i].expr = NULL;
    wp_pool[i].expr_ast = NULL;
    wp_pool[i].cond_ast = NULL;
    wp_pool[i].val = sdb_value_make(0, sizeof(word_t) * 8, false);
    wp_pool[i].evaluated = false;
    wp_pool[i].enabled = true;
    wp_pool[i].temporary = false;
    wp_pool[i].hit_count = 0;
    wp_pool[i].ignore_count = 0;
    wp_pool[i].stop_count = 0;
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
      printf("Watchpoint %d: %s\n", cur->NO, cur->expr ? cur->expr : "<null>");
      printf(
        "Value: %ld, Evaluated: %s, Enabled: %s\n",
        (long)sdb_value_as_i64(cur->val),
        cur->evaluated ? "true" : "false",
        cur->enabled ? "true" : "false"
      );
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
  result->expr = NULL;
  result->expr_ast = NULL;
  result->cond_ast = NULL;
  result->evaluated = false;
  result->enabled = true;
  result->temporary = false;
  result->hit_count = 0;
  result->ignore_count = 0;
  result->stop_count = 0;
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
  free(wp->expr);
  sdb_expr_free(wp->expr_ast);
  sdb_expr_free(wp->cond_ast);
  wp->expr = NULL;
  wp->expr_ast = NULL;
  wp->cond_ast = NULL;
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
  SdbEvalResult result = {0};

  for (cur = head; cur; cur = cur->next) {
    if (!cur->enabled || !cur->expr_ast) {
      continue;
    }
    result.has_value = false;
    sdb_error_clear(&result.error);
    if (!sdb_expr_eval(cur->expr_ast, sdb_nemu_target_ops(), &result)) {
      continue;
    }
    if (cur->evaluated && result.value.bits != cur->val.bits) {
      nemu_state.state = NEMU_STOP;
      printf("Watchpoint %d triggered: %s\n", cur->NO, cur->expr ? cur->expr : "<null>");
      printf(
        "Old value: %ld, new value: %ld\n",
        (long)sdb_value_as_i64(cur->val), (long)sdb_value_as_i64(result.value)
      );
    }
    cur->val = result.value;
    cur->evaluated = true;
  }
}
