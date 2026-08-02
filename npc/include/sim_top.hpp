#ifndef __SIM_TOP_HPP__
#define __SIM_TOP_HPP__ 1

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

struct ExecInfo {
    addr_t pc;   // retired PC
    word_t inst; // retired instruction word
};

extern ExecInfo simExecInfo;

// ── Transitional compatibility: ownership lives in npc/csrc/npc/simulator.cpp ──
extern VerilatedContext *verContext;
extern VerilatedFstC    *tfp;
extern volatile bool     sim_halt;

extern uint64_t execCount;
extern uint64_t execCountClockPeriod;
extern bool     s_difftestActive;

#define DEFAULT_BIN_PATH "build/program.bin"

// ---- Performance counter accessors (for snapshot / TUI seam) ----
uint64_t getExecCount();
uint64_t getExecCountClockPeriod();
bool isDifftestActive();

// ---- Signal handler (ownership in simulator.cpp) ----
void install_signal_handlers();

void simStepClockPeriod();
void simStep();
void simReset(int n);
bool simExecOnce();
void simExecClockPeriod(uint64_t n);
void simExec(uint64_t n);
bool simulate(bool sdbEnabled);

#endif /* __SIM_TOP_HPP__ */
