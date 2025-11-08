#include <am.h>
#include <riscv/riscv.h>
#include <klib.h>

static Context* (*user_handler)(Event, Context*) = NULL;

Context* __am_irq_handle(Context *c) {
  if (user_handler) {
    Event ev = {0};
    switch (c->mcause) {
      case MCAUSE_ECALL_FROM_M_MODE:
#ifdef __riscv_e
        if (((int) c->gpr[15]) == -1) {
#else
        if (((int) c->gpr[17]) == -1) {
#endif
          // a1 寄存器是 -1, 表明是 yield
          ev.event = EVENT_YIELD;
        } else {
          ev.event = EVENT_SYSCALL;
        }
        c->mepc += 4; // skip the instruction that caused the interrupt
        break;
      default:
        ev.event = EVENT_ERROR;
        break;
    }

    c = user_handler(ev, c);
    assert(c != NULL);
  }

  return c;
}

extern void __am_asm_trap(void);

bool cte_init(Context*(*handler)(Event, Context*)) {
  // initialize exception entry
  asm volatile("csrw mtvec, %0" : : "r"(__am_asm_trap));

  // register event handler
  user_handler = handler;

  return true;
}

Context *kcontext(Area kstack, void (*entry)(void *), void *arg) {
  Context *ctx = (Context *) (kstack.end - sizeof(Context));
  ctx->mstatus = 0x1800; // For difftest purpose, mstatus should be set.
  ctx->mepc = (uintptr_t) entry;
  ctx->gpr[10] = (uintptr_t) arg; // RISC-V ISA 的 x10 寄存器就是 a0, 传递函数的第一个参数的寄存器.
  return ctx;
}

void yield() {
#ifdef __riscv_e
  asm volatile("li a5, -1; ecall");
#else
  asm volatile("li a7, -1; ecall");
#endif
}

bool ienabled() {
  return false;
}

void iset(bool enable) {
}
