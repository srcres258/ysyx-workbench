#ifndef __TRACE_OBSERVER_H__
#define __TRACE_OBSERVER_H__

#include <common.h>

typedef struct ExecObserver {
  void (*on_instruction)(vaddr_t pc, vaddr_t next_pc);
} ExecObserver;

void exec_observer_set(const ExecObserver *observer);
void exec_observer_clear(void);
void exec_observer_on_instruction(vaddr_t pc, vaddr_t next_pc);

#endif
