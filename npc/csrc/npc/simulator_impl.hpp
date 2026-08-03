#ifndef NPC_SRC_SIMULATOR_IMPL_HPP
#define NPC_SRC_SIMULATOR_IMPL_HPP

// ── Internal header: NOT exposed in npc/include/npc/ ──
// This header declares the SimulatorImpl struct and the active-instance
// bridge so that library-internal code can resolve the currently running
// simulator.  Only one active simulator per process is supported in this
// pass because DPI callbacks, NVBoard, and the TUI event feed are still
// process-global.

#include <verilated.h>
#include <verilated_fst_c.h>
#include <cstdint>
#include <deque>
#include <common.hpp>
#include <sim_top.hpp>
#include <perf.hpp>
#include <difftest/dut.hpp>

#ifdef NPC_STANDALONE
#include "Vysyx_25070190__Syms.h"
#else
#include "VysyxSoCFull__Syms.h"
#endif

// ── Forward declaration ──
namespace npc {
class Simulator;
}

// ── Difftest function-pointer type aliases (internal, moved from dut.cpp) ──
using ref_difftest_memcpy_f_t = void (*)(
    addr_t addr, void *buf, size_t n, bool direction
);
using ref_difftest_regcpy_f_t = void (*)(
    void *dut, bool direction
);
using ref_difftest_exec_f_t = void (*)(uint64_t n);
using ref_difftest_raise_intr_f_t = void (*)(word_t NO);
using ref_difftest_set_mem_map_f_t = void (*)(
    const DiffTestMemRegion *regions, size_t nr_regions
);
using ref_difftest_get_mem_map_f_t = size_t (*)(
    DiffTestMemRegion *regions, size_t max_regions
);
using ref_difftest_set_reset_vector_f_t = void (*)(uint64_t reset_vector);
using ref_difftest_init_f_t = void (*)(int port);

// ── Library‑internal lifecycle state ──
// Authoritative storage lives in npc/csrc/npc/simulator.cpp.
// dpi.cpp needs direct (volatile) access to sim_halt for the DPI callback
// dpi_halt(), which is on the critical evaluation path.
extern volatile bool sim_halt;

// Active-instance bridge: lifecycle fields (VerilatedContext, top, etc.)
// live as file-scope globals in simulator.cpp; this struct now also holds
// perf, difftest, and device memory base state so those subsystems no longer
// depend on ownership globals defined in their own translation units.
struct SimulatorImpl {
    npc::Simulator* owner = nullptr;

    // ── Perf monitor (was: perf::g_perfMonitor in perf.cpp) ──
    perf::PerfMonitor perfMonitor;

    // ── Difftest state (was: static globals in difftest/dut.cpp) ──
    void* dlHandle = nullptr;
    ref_difftest_memcpy_f_t             ref_difftest_memcpy           = nullptr;
    ref_difftest_regcpy_f_t             ref_difftest_regcpy           = nullptr;
    ref_difftest_exec_f_t               ref_difftest_exec             = nullptr;
    ref_difftest_raise_intr_f_t         ref_difftest_raise_intr       = nullptr;
    ref_difftest_set_mem_map_f_t        ref_difftest_set_mem_map      = nullptr;
    ref_difftest_get_mem_map_f_t        ref_difftest_get_mem_map      = nullptr;
    ref_difftest_set_reset_vector_f_t   ref_difftest_set_reset_vector = nullptr;
    ref_difftest_init_f_t               ref_difftest_init             = nullptr;
    std::deque<DiffTestSkipEvent>       pendingSkipRefPcs;
    int skipDutNrInst = 0;

    // ── Device memory bases (was: globals in device/*.cpp) ──
    void* flash_io_base = nullptr;
    void* mrom_io_base  = nullptr;
    void* psram_io_base = nullptr;
    void* sdram_io_base = nullptr;
    void* sram_io_base  = nullptr;

    SimulatorImpl()                                 = default;
    SimulatorImpl(const SimulatorImpl&)             = delete;
    SimulatorImpl& operator=(const SimulatorImpl&)  = delete;
    SimulatorImpl(SimulatorImpl&&)                  = delete;
    SimulatorImpl& operator=(SimulatorImpl&&)       = delete;
};

// ── Config translation (Task 3: public API → internal legacy shape) ──
namespace npc {
struct SimulatorConfig;
void applySimulatorConfig(const SimulatorConfig &config);
} // namespace npc

// ── Active-instance bridge ──
SimulatorImpl* getActiveSimulator();
void           setActiveSimulator(SimulatorImpl* impl);

// ── Internal lifecycle helpers (shared between Simulator methods
//     and transitional compat wrappers in sim.cpp) ──
namespace npc::internal {

void simStepClockImpl();
void simStepImpl();
void simResetImpl(int n);
bool simExecOnceImpl();
void simExecImpl(uint64_t n);
void simExecClockPeriodImpl(uint64_t n);

} // namespace npc::internal

#endif // NPC_SRC_SIMULATOR_IMPL_HPP
