#ifndef __SIM_TOP_HPP__
#define __SIM_TOP_HPP__ 1

// ═══════════════════════════════════════════════════════════════════════════
//  sim_top.hpp  —  Compatibility bridge between legacy consumers and the
//                  npc::Simulator library
//
//  Task‑10 cleaned this header of library‑internal lifecycle globals
//  (verContext, tfp, execCount, execCountClockPeriod, s_difftestActive,
//  sim_halt, install_signal_handlers, DEFAULT_BIN_PATH).  What remains is
//  either:
//    a)  Verilator‑model accessors needed by auto_bind.cpp, DPI callbacks,
//        device stubs, SDB adapter, and ISA helpers, or
//    b)  Shared data structures (ExecInfo) whose writers and readers still
//        live in separate translation units, or
//    c)  Compatibility free‑function wrappers (simExec, simStep, …) that
//        delegate to npc::internal::*Impl() and keep sdb.cpp / tui_control.cpp
//        compiling during the transition to npc::Simulator methods.
//
//  Authoritative ownership of all lifecycle resources lives in
//  npc/csrc/npc/simulator.cpp.  Do NOT add new extern declarations here.
// ═══════════════════════════════════════════════════════════════════════════

#include <cstddef>
#include <cstdint>
#include <verilated.h>
#include <verilated_fst_c.h>
#include <common.hpp>

#ifdef NPC_STANDALONE
#include "Vysyx_25070190__Syms.h"
extern Vysyx_25070190 *top;
Vysyx_25070190_GeneralDPIAdapter *getDPIModule();
void standalone_mem_loadBin(const char *path);
uint8_t *standalone_mem_getPmemBase();
size_t standalone_mem_getPmemSize();
size_t standalone_mem_getLoadedSize();
#else
#include "VysyxSoCFull__Syms.h"
extern VysyxSoCFull *top;
VysyxSoCFull_GeneralDPIAdapter *getDPIModule();
#endif

// ── Retired‑instruction snapshot (writers: dpi.cpp / simulator.cpp; readers: TUI / SDB) ──
struct ExecInfo {
    addr_t pc;   // retired PC
    word_t inst; // retired instruction word
};

extern ExecInfo simExecInfo;

// ── Performance counter accessors (library‑internal state, read‑only access) ──
uint64_t getExecCount();
uint64_t getExecCountClockPeriod();
bool isDifftestActive();

// ── Compatibility simulation wrappers (delegate to npc::internal::*Impl()) ──
void simStepClockPeriod();
void simStep();
void simReset(int n);
bool simExecOnce();
void simExecClockPeriod(uint64_t n);
void simExec(uint64_t n);
bool simulate(bool sdbEnabled);

#endif /* __SIM_TOP_HPP__ */
