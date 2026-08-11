#include <trace/observer.h>

static const ExecObserver *current_observer = NULL;

void exec_observer_set(const ExecObserver *observer) {
  current_observer = observer;
}

void exec_observer_clear(void) {
  current_observer = NULL;
}

void exec_observer_on_instruction(vaddr_t pc, vaddr_t next_pc) {
  if (current_observer != NULL && current_observer->on_instruction != NULL) {
    current_observer->on_instruction(pc, next_pc);
  }
}
