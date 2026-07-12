#ifndef __SIM_TOP_HPP__
#define __SIM_TOP_HPP__ 1

#include <cstdint>
#include <verilated.h>
#include <common.hpp>

#ifdef NPC_STANDALONE
#include "Vysyx_25070190__Syms.h"
extern Vysyx_25070190 *top;
Vysyx_25070190_GeneralDPIAdapter *getDPIModule();
void standalone_mem_loadBin(const char *path);
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

extern VerilatedContext *verContext;
extern volatile bool sim_halt;

#define DEFAULT_BIN_PATH "build/program.bin"

// ---- Performance counter accessors (for snapshot / TUI seam) ----
uint64_t getExecCount();
uint64_t getExecCountClockPeriod();
bool isDifftestActive();

void simStepClockPeriod();
void simStep();
void simReset(int n);
bool simExecOnce();
void simExecClockPeriod(uint64_t n);
void simExec(uint64_t n);
bool simulate(bool sdbEnabled);

#endif /* __SIM_TOP_HPP__ */
