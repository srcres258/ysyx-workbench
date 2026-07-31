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

#ifndef __SDB_H__
#define __SDB_H__

#include <common.h>
#include "sdb_core.h"

typedef struct watchpoint {
  int NO;
  struct watchpoint *next;

  char *expr;
  SdbExpr *expr_ast;
  SdbExpr *cond_ast;
  SdbValue val;
  bool evaluated;
  bool enabled;
  bool temporary;
  int hit_count;
  int ignore_count;
  int stop_count;
} WP;

word_t expr(char *e, bool *success);
WP *new_wp(void);
void free_wp(WP *wp);
WP *find_wp(int NO);

void sdb_eval_and_update_wp(void);
const SdbTargetOps *sdb_nemu_target_ops(void);

#endif
