#include <perf.hpp>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace perf {

// ── Single point of truth: counter definition table ─────────────────────
// Positional order MUST match the perf::Idx:: constants.
static constexpr PerfCounterDef kCounterTable[PerfCounters::kNumCounters] = {
    /*  0 */ { "core.cycle",                            "cycle", "Core execution cycles (running=1)",                 "dpi->perf_core_running (polling)"               },
    /*  1 */ { "core.instret",                          "count", "Retired instructions",                               "dpi->perf_core_commitFire (polling)"            },
    /*  2 */ { "core.busy.cycle",                       "cycle", "Core busy cycles (not idle)",                         "dpi->perf_core_busy (polling)"                  },
    /*  3 */ { "core.stall.cycle",                      "cycle", "Core stall cycles (busy but not committing)",         "dpi->perf_core_stall (polling)"                 },
    /*  4 */ { "inst.class.alu.count",                  "count", "ALU / integer compute instructions retired",          "dpi->perf_inst_alu (polling)"                   },
    /*  5 */ { "inst.class.load.count",                 "count", "Load instructions retired",                           "dpi->perf_inst_load (polling)"                  },
    /*  6 */ { "inst.class.store.count",                "count", "Store instructions retired",                          "dpi->perf_inst_store (polling)"                 },
    /*  7 */ { "inst.class.branch.count",               "count", "Branch instructions retired",                         "dpi->perf_inst_branch (polling)"                },
    /*  8 */ { "inst.class.jal.count",                  "count", "JAL instructions retired",                            "dpi->perf_inst_jal (polling)"                   },
    /*  9 */ { "inst.class.jalr.count",                 "count", "JALR instructions retired",                           "dpi->perf_inst_jalr (polling)"                  },
    /* 10 */ { "inst.class.csr.count",                  "count", "CSR-access instructions retired",                     "dpi->perf_inst_csr (polling)"                   },
    /* 11 */ { "inst.class.muldiv.count",               "count", "Multiply/divide instructions retired",                "dpi->perf_inst_muldiv (polling, hardwired 0)"   },
    /* 12 */ { "state.fetch.cycle",                     "cycle", "Cycles IF stage was active",                          "dpi->perf_state_fetch_cycle (polling)"          },
    /* 13 */ { "state.decode.cycle",                    "cycle", "Cycles ID stage was active",                          "dpi->perf_state_decode_cycle (polling)"         },
    /* 14 */ { "state.execute.cycle",                   "cycle", "Cycles EX stage was active",                          "dpi->perf_state_execute_cycle (polling)"        },
    /* 15 */ { "state.memory.cycle",                    "cycle", "Cycles MEM stage was active",                         "dpi->perf_state_memory_cycle (polling)"         },
    /* 16 */ { "state.writeback.cycle",                 "cycle", "Cycles WB stage was active",                          "dpi->perf_state_writeback_cycle (polling)"      },
    /* 17 */ { "stall.ifetch.wait_resp.cycle",          "cycle", "IFU waiting for instruction-fetch response",          "dpi->perf_stall_ifetch_wait_resp (polling)"     },
    /* 18 */ { "stall.mem.wait_resp.cycle",             "cycle", "LSU waiting for memory response",                     "dpi->perf_stall_mem_wait_resp (polling)"        },
    /* 19 */ { "stall.mem.req_blocked.cycle",           "cycle", "LSU request blocked (backpressure)",                  "dpi->perf_stall_mem_req_blocked (polling)"      },
    /* 20 */ { "stall.structural.shared_mem.cycle",     "cycle", "Structural hazard: IFU+LSU competing for shared mem", "dpi->perf_stall_structural_shared_mem (polling)"},
    /* 21 */ { "stall.muldiv.busy.cycle",               "cycle", "Multi-cycle mul/div unit busy",                       "dpi->perf_stall_muldiv_busy (polling, hw 0)"    },
    /* 22 */ { "mem.load.req.count",                    "count", "Load requests fired to LSU",                          "dpi->perf_mem_load_req_fire (polling)"          },
    /* 23 */ { "mem.store.req.count",                   "count", "Store requests fired to LSU",                         "dpi->perf_mem_store_req_fire (polling)"         },
    /* 24 */ { "mem.mmio.req.count",                    "count", "MMIO (peripheral) requests",                          "dpi->perf_mem_mmio_req_fire (polling)"          },
    /* 25 */ { "trap.exception.count",                  "count", "Exceptions taken",                                     "dpi->perf_trap_exception_fire (polling)"        },
};

// Compile-time guard: table size must match counter count.
static_assert(sizeof(kCounterTable) / sizeof(kCounterTable[0]) == PerfCounters::kNumCounters,
              "kCounterTable size must equal kNumCounters (26)");

// ── Pass accumulated core signals to counter positions ──────────────────
void PerfCounters::accumulateCore(uint64_t running, uint64_t commitFire,
                                   uint64_t busy, uint64_t stall) {
    m_values[Idx::CORE_CYCLE]       += running;
    m_values[Idx::CORE_INSTRET]     += commitFire;
    m_values[Idx::CORE_BUSY_CYCLE]  += busy;
    m_values[Idx::CORE_STALL_CYCLE] += stall;
}

void PerfCounters::accumulateInstClass(uint64_t alu, uint64_t load, uint64_t store,
                                        uint64_t branch, uint64_t jal, uint64_t jalr,
                                        uint64_t csr, uint64_t muldiv) {
    m_values[Idx::INST_CLASS_ALU_COUNT]    += alu;
    m_values[Idx::INST_CLASS_LOAD_COUNT]   += load;
    m_values[Idx::INST_CLASS_STORE_COUNT]  += store;
    m_values[Idx::INST_CLASS_BRANCH_COUNT] += branch;
    m_values[Idx::INST_CLASS_JAL_COUNT]    += jal;
    m_values[Idx::INST_CLASS_JALR_COUNT]   += jalr;
    m_values[Idx::INST_CLASS_CSR_COUNT]    += csr;
    m_values[Idx::INST_CLASS_MULDIV_COUNT] += muldiv;
}

void PerfCounters::accumulateState(uint64_t fetch, uint64_t decode, uint64_t execute,
                                    uint64_t memory, uint64_t writeback) {
    m_values[Idx::STATE_FETCH_CYCLE]     += fetch;
    m_values[Idx::STATE_DECODE_CYCLE]    += decode;
    m_values[Idx::STATE_EXECUTE_CYCLE]   += execute;
    m_values[Idx::STATE_MEMORY_CYCLE]    += memory;
    m_values[Idx::STATE_WRITEBACK_CYCLE] += writeback;
}

void PerfCounters::accumulateStall(uint64_t ifetchWaitResp, uint64_t memWaitResp,
                                    uint64_t memReqBlocked, uint64_t structSharedMem,
                                    uint64_t muldivBusy) {
    m_values[Idx::STALL_IFETCH_WAIT_RESP_CYCLE]  += ifetchWaitResp;
    m_values[Idx::STALL_MEM_WAIT_RESP_CYCLE]     += memWaitResp;
    m_values[Idx::STALL_MEM_REQ_BLOCKED_CYCLE]   += memReqBlocked;
    m_values[Idx::STALL_STRUCT_SHARED_MEM_CYCLE] += structSharedMem;
    m_values[Idx::STALL_MULDIV_BUSY_CYCLE]       += muldivBusy;
}

void PerfCounters::accumulateMem(uint64_t loadReqFire, uint64_t storeReqFire,
                                  uint64_t mmioReqFire) {
    m_values[Idx::MEM_LOAD_REQ_COUNT]  += loadReqFire;
    m_values[Idx::MEM_STORE_REQ_COUNT] += storeReqFire;
    m_values[Idx::MEM_MMIO_REQ_COUNT]  += mmioReqFire;
}

void PerfCounters::accumulateTrap(uint64_t exceptionFire) {
    m_values[Idx::TRAP_EXCEPTION_COUNT] += exceptionFire;
}

// ── Clear ────────────────────────────────────────────────────────────────
void PerfCounters::clear() {
    m_values.fill(0);
}

// ── Snapshot view ────────────────────────────────────────────────────────
std::vector<PerfCounterView> PerfCounters::view() const {
    std::vector<PerfCounterView> result;
    result.reserve(kNumCounters);
    for (size_t i = 0; i < kNumCounters; i++) {
        const auto &def = kCounterTable[i];
        result.push_back({def.name, def.unit, def.definition, m_values[i]});
    }
    return result;
}

// ── PerfMonitor lifecycle ────────────────────────────────────────────────
PerfMonitor g_perfMonitor;

void PerfMonitor::onResetBegin() {
    m_counters.clear();
    m_resetting = true;
}

void PerfMonitor::onResetEnd() {
    m_resetting = false;
}

const PerfCounterDef &PerfMonitor::counterDef(size_t idx) {
    return kCounterTable[idx];
}

// ── Dump ─────────────────────────────────────────────────────────────────
void PerfMonitor::dumpSummary(std::ostream &os) const {
    auto cyc    = get(Idx::CORE_CYCLE);
    auto iret   = get(Idx::CORE_INSTRET);
    auto stallC = get(Idx::CORE_STALL_CYCLE);

    double cpi   = (iret > 0) ? static_cast<double>(cyc) / static_cast<double>(iret) : 0.0;
    double ipc   = (cyc > 0) ? static_cast<double>(iret) / static_cast<double>(cyc)    : 0.0;
    double stallPct = (cyc > 0) ? 100.0 * static_cast<double>(stallC) / static_cast<double>(cyc) : 0.0;

    os << "─── Perf Counter Summary ───\n";
    os << "  CPI  = " << std::fixed << std::setprecision(4) << cpi
       << "  IPC  = " << std::fixed << std::setprecision(4) << ipc
       << "  Stall% = " << std::fixed << std::setprecision(1) << stallPct << "%\n";

    auto instSum = get(Idx::INST_CLASS_ALU_COUNT)    + get(Idx::INST_CLASS_LOAD_COUNT)
                 + get(Idx::INST_CLASS_STORE_COUNT)  + get(Idx::INST_CLASS_BRANCH_COUNT)
                 + get(Idx::INST_CLASS_JAL_COUNT)    + get(Idx::INST_CLASS_JALR_COUNT)
                 + get(Idx::INST_CLASS_CSR_COUNT)    + get(Idx::INST_CLASS_MULDIV_COUNT);

    os << "  Domain totals:\n";
    os << "    core:   cycle=" << cyc << "  instret=" << iret
       << "  busy=" << get(Idx::CORE_BUSY_CYCLE)
       << "  stall=" << stallC << '\n';
    os << "    inst-class: alu=" << get(Idx::INST_CLASS_ALU_COUNT)
       << "  load=" << get(Idx::INST_CLASS_LOAD_COUNT)
       << "  store=" << get(Idx::INST_CLASS_STORE_COUNT)
       << "  branch=" << get(Idx::INST_CLASS_BRANCH_COUNT)
       << "  jal=" << get(Idx::INST_CLASS_JAL_COUNT)
       << "  jalr=" << get(Idx::INST_CLASS_JALR_COUNT)
       << "  csr=" << get(Idx::INST_CLASS_CSR_COUNT)
       << "  muldiv=" << get(Idx::INST_CLASS_MULDIV_COUNT)
       << "  (sum=" << instSum << ")\n";
    os << "    state:  fetch=" << get(Idx::STATE_FETCH_CYCLE)
       << "  decode=" << get(Idx::STATE_DECODE_CYCLE)
       << "  execute=" << get(Idx::STATE_EXECUTE_CYCLE)
       << "  memory=" << get(Idx::STATE_MEMORY_CYCLE)
       << "  writeback=" << get(Idx::STATE_WRITEBACK_CYCLE) << '\n';
    os << "    stall:  ifetch_wait=" << get(Idx::STALL_IFETCH_WAIT_RESP_CYCLE)
       << "  mem_wait=" << get(Idx::STALL_MEM_WAIT_RESP_CYCLE)
       << "  req_blocked=" << get(Idx::STALL_MEM_REQ_BLOCKED_CYCLE)
       << "  shared_mem=" << get(Idx::STALL_STRUCT_SHARED_MEM_CYCLE)
       << "  muldiv=" << get(Idx::STALL_MULDIV_BUSY_CYCLE) << '\n';
    os << "    mem:    load=" << get(Idx::MEM_LOAD_REQ_COUNT)
       << "  store=" << get(Idx::MEM_STORE_REQ_COUNT)
       << "  mmio=" << get(Idx::MEM_MMIO_REQ_COUNT) << '\n';
    os << "    trap:   exception=" << get(Idx::TRAP_EXCEPTION_COUNT) << '\n';

    os << std::flush;
}

} // namespace perf
