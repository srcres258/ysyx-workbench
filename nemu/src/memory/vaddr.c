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
#include <memory/paddr.h>
#include <memory/vaddr.h>

word_t vaddr_ifetch(vaddr_t addr, int len) {
  return paddr_read_mtrace(addr, len, false);
}

word_t vaddr_read(vaddr_t addr, int len) {
  return vaddr_read_mtrace(addr, len, true);
}

word_t vaddr_read_mtrace(vaddr_t addr, int len, bool mtrace_on) {
  return paddr_read_mtrace(addr, len, mtrace_on);
}

void vaddr_write(vaddr_t addr, int len, word_t data) {
  return vaddr_write_mtrace(addr, len, data, true);
}

void vaddr_write_mtrace(vaddr_t addr, int len, word_t data, bool mtrace_on) {
  paddr_write_mtrace(addr, len, data, mtrace_on);
}
