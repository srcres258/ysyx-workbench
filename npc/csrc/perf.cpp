#include <perf.hpp>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>
#include <cstring>
#include "npc/simulator_impl.hpp"

namespace perf {

// ── Single point of truth: counter definition table ─────────────────────
//
// APPEND-ONLY POLICY (contract freeze, T1):
//   1. New counters MUST be appended to the END of this table.
//   2. NEVER rename, reorder, delete, or insert in the middle.
//   3. Existing 26 counter names, positions (0–25), and semantics are frozen.
//   4. After appending, update kNumCounters in perf.hpp → add
//      new Idx:: constants at the TAIL → extend the corresponding
//      accumulate*() function (or add a new one) → rebuild.
//   5. The compile-time check below enforces no duplicate names.
//
// Positional order MUST match the perf::Idx:: constants.
static constexpr PerfCounterDef kCounterTable[PerfCounters::kNumCounters] = {
    /*   0 */ { "core.cycle",                                     "cycle", "Core clock cycles (!reset)",                                              "dpi->perf_core_running (polling)"                          },
    /*   1 */ { "core.instret",                                   "count", "Retired instructions",                                                    "dpi->perf_core_commitFire (polling)"                       },
    /*   2 */ { "core.busy.cycle",                                "cycle", "Core busy cycles (not idle)",                                             "dpi->perf_core_busy (polling)"                             },
    /*   3 */ { "core.stall.cycle",                               "cycle", "Core stall cycles (busy but not committing)",                             "dpi->perf_core_stall (polling)"                            },
    /*   4 */ { "inst.class.alu.count",                           "count", "ALU / integer compute instructions retired",                              "dpi->perf_inst_alu (polling)"                              },
    /*   5 */ { "inst.class.load.count",                          "count", "Load instructions retired",                                               "dpi->perf_inst_load (polling)"                             },
    /*   6 */ { "inst.class.store.count",                         "count", "Store instructions retired",                                              "dpi->perf_inst_store (polling)"                            },
    /*   7 */ { "inst.class.branch.count",                        "count", "Branch instructions retired",                                             "dpi->perf_inst_branch (polling)"                           },
    /*   8 */ { "inst.class.jal.count",                           "count", "JAL instructions retired",                                                "dpi->perf_inst_jal (polling)"                              },
    /*   9 */ { "inst.class.jalr.count",                          "count", "JALR instructions retired",                                               "dpi->perf_inst_jalr (polling)"                             },
    /*  10 */ { "inst.class.csr.count",                           "count", "CSR-access instructions retired",                                         "dpi->perf_inst_csr (polling)"                              },
    /*  11 */ { "inst.class.muldiv.count",                        "count", "Multiply/divide instructions retired",                                    "dpi->perf_inst_muldiv (polling, hardwired 0)"              },
    /*  12 */ { "state.fetch.cycle",                              "cycle", "Cycles IF stage was active",                                              "dpi->perf_state_fetch_cycle (polling)"                     },
    /*  13 */ { "state.decode.cycle",                             "cycle", "Cycles ID stage was active",                                              "dpi->perf_state_decode_cycle (polling)"                    },
    /*  14 */ { "state.execute.cycle",                            "cycle", "Cycles EX stage was active",                                              "dpi->perf_state_execute_cycle (polling)"                   },
    /*  15 */ { "state.memory.cycle",                             "cycle", "Cycles MEM stage was active",                                             "dpi->perf_state_memory_cycle (polling)"                    },
    /*  16 */ { "state.writeback.cycle",                          "cycle", "Cycles WB stage was active",                                              "dpi->perf_state_writeback_cycle (polling)"                 },
    /*  17 */ { "stall.ifetch.wait_resp.cycle",                   "cycle", "IFU waiting for instruction-fetch response",                              "dpi->perf_stall_ifetch_wait_resp (polling)"                },
    /*  18 */ { "stall.mem.wait_resp.cycle",                      "cycle", "LSU waiting for memory response",                                         "dpi->perf_stall_mem_wait_resp (polling)"                   },
    /*  19 */ { "stall.mem.req_blocked.cycle",                    "cycle", "LSU request blocked (backpressure)",                                      "dpi->perf_stall_mem_req_blocked (polling)"                 },
    /*  20 */ { "stall.structural.shared_mem.cycle",              "cycle", "Structural hazard: IFU+LSU competing for shared mem",                     "dpi->perf_stall_structural_shared_mem (polling)"           },
    /*  21 */ { "stall.muldiv.busy.cycle",                        "cycle", "Multi-cycle mul/div unit busy",                                           "dpi->perf_stall_muldiv_busy (polling, hw 0)"               },
    /*  22 */ { "mem.load.req.count",                             "count", "Load requests fired to LSU",                                              "dpi->perf_mem_load_req_fire (polling)"                     },
    /*  23 */ { "mem.store.req.count",                            "count", "Store requests fired to LSU",                                             "dpi->perf_mem_store_req_fire (polling)"                    },
    /*  24 */ { "mem.mmio.req.count",                             "count", "MMIO (peripheral) requests",                                              "dpi->perf_mem_mmio_req_fire (polling)"                     },
    /*  25 */ { "trap.exception.count",                           "count", "Exceptions taken",                                                        "dpi->perf_trap_exception_fire (polling)"                   },
    /*  26 */ { "reg.gpr.write.count",                            "count", "GPR register writeback events",                                           "dpi->perf_reg_gpr_wb_fire (polling)"                       },
    /*  27 */ { "reg.csr.write.count",                            "count", "CSR register writeback events (any)",                                     "dpi->perf_reg_csr_wb_fire (polling)"                       },
    /*  28 */ { "reg.gpr.src.alu.count",                          "count", "GPR writeback source: ALU output",                                        "dpi->perf_reg_gpr_src_alu (polling)"                       },
    /*  29 */ { "reg.gpr.src.dmem.count",                         "count", "GPR writeback source: data memory read",                                  "dpi->perf_reg_gpr_src_dmem (polling)"                      },
    /*  30 */ { "reg.gpr.src.imm.count",                          "count", "GPR writeback source: immediate",                                         "dpi->perf_reg_gpr_src_imm (polling)"                       },
    /*  31 */ { "reg.gpr.src.pc_next.count",                      "count", "GPR writeback source: PC+4/PC+offset",                                    "dpi->perf_reg_gpr_src_pc_next (polling)"                   },
    /*  32 */ { "reg.gpr.src.bcu.count",                          "count", "GPR writeback source: branch comparator",                                 "dpi->perf_reg_gpr_src_bcu (polling)"                       },
    /*  33 */ { "reg.gpr.src.csr.count",                          "count", "GPR writeback source: CSR read data",                                     "dpi->perf_reg_gpr_src_csr (polling)"                       },
    /*  34 */ { "reg.csr.mode.rw.count",                          "count", "CSR write mode: direct write (CSRRW)",                                    "dpi->perf_reg_csr_mode_rw (polling)"                       },
    /*  35 */ { "reg.csr.mode.rs.count",                          "count", "CSR write mode: bit-set (CSRRS)",                                         "dpi->perf_reg_csr_mode_rs (polling)"                       },
    /*  36 */ { "reg.csr.mode.rc.count",                          "count", "CSR write mode: bit-clear (CSRRC)",                                       "dpi->perf_reg_csr_mode_rc (polling)"                       },
    /*  37 */ { "reg.csr.mode.imm.count",                         "count", "CSR write mode: immediate form (CSRRWI/CSRRSI/CSRRCI)",                   "dpi->perf_reg_csr_mode_imm (polling)"                      },
    /*  38 */ { "gpr.read.rs1.count",                             "count", "GPR read port rs1 access count",                                          "dpi->perf_gpr_gpr_read_rs1 (polling)"                      },
    /*  39 */ { "gpr.read.rs2.count",                             "count", "GPR read port rs2 access count",                                          "dpi->perf_gpr_gpr_read_rs2 (polling)"                      },
    /*  40 */ { "gpr.read.both.count",                            "count", "Both GPR read ports accessed (per instruction)",                          "dpi->perf_gpr_gpr_read_both (polling)"                     },
    /*  41 */ { "gpr.read.rs1_x0.count",                          "count", "GPR rs1 read addressing x0 (zero register)",                              "dpi->perf_gpr_gpr_read_rs1_x0 (polling)"                   },
    /*  42 */ { "gpr.read.rs2_x0.count",                          "count", "GPR rs2 read addressing x0 (zero register)",                              "dpi->perf_gpr_gpr_read_rs2_x0 (polling)"                   },
    /*  43 */ { "gpr.read.rs1_eq_rs2.count",                      "count", "GPR rs1==rs2 (non-x0), same register read twice",                         "dpi->perf_gpr_gpr_read_rs1_eq_rs2 (polling)"               },
    /*  44 */ { "gpr.read.rs2_unused.count",                      "count", "GPR rs2 semantically unused by instruction",                              "dpi->perf_gpr_gpr_read_rs2_unused (polling)"               },
    /*  45 */ { "gpr.read.upper16.count",                         "count", "GPR read accessing upper 16 registers (x16–x31)",                         "dpi->perf_gpr_gpr_read_upper16 (polling)"                  },
    /*  46 */ { "gpr.write.suppressed_x0.count",                  "count", "GPR write suppressed because rd==x0",                                     "dpi->perf_gpr_gpr_write_suppressed_x0 (polling)"           },
    /*  47 */ { "csr.read.port1.enable.count",                    "count", "CSR read port 1 active (instruction-driven addr)",                        "dpi->perf_csr_csr_read_port1_enable (polling)"             },
    /*  48 */ { "csr.read.port2.enable.count",                    "count", "CSR read port 2 active (fixed mepc)",                                     "dpi->perf_csr_csr_read_port2_enable (polling)"             },
    /*  49 */ { "csr.read.port3.enable.count",                    "count", "CSR read port 3 active (fixed mtvec)",                                    "dpi->perf_csr_csr_read_port3_enable (polling)"             },
    /*  50 */ { "csr.read.concurrent_2port.count",                "count", "2+ CSR read ports active simultaneously",                                 "dpi->perf_csr_csr_read_concurrent_2port (polling)"         },
    /*  51 */ { "csr.read.concurrent_3port.count",                "count", "3 CSR read ports active simultaneously",                                  "dpi->perf_csr_csr_read_concurrent_3port (polling)"         },
    /*  52 */ { "csr.write.normal.count",                         "count", "CSR write: normal CSR instruction (non-trap)",                            "dpi->perf_csr_csr_write_normal (polling)"                  },
    /*  53 */ { "csr.write.trap.count",                           "count", "CSR write: trap entry (ecall → mepc/mcause)",                             "dpi->perf_csr_csr_write_trap (polling)"                    },
    /*  54 */ { "csr.write.return.count",                         "count", "CSR write: trap return (mret)",                                           "dpi->perf_csr_csr_write_return (polling)"                  },
    /*  55 */ { "csr.addr.mstatus.count",                         "count", "CSR write targeting mstatus (0x300)",                                     "dpi->perf_csr_csr_addr_mstatus (polling)"                  },
    /*  56 */ { "csr.addr.mtvec.count",                           "count", "CSR write targeting mtvec (0x305)",                                       "dpi->perf_csr_csr_addr_mtvec (polling)"                    },
    /*  57 */ { "csr.addr.mepc.count",                            "count", "CSR write targeting mepc (0x341)",                                        "dpi->perf_csr_csr_addr_mepc (polling)"                     },
    /*  58 */ { "csr.addr.mcause.count",                          "count", "CSR write targeting mcause (0x342)",                                      "dpi->perf_csr_csr_addr_mcause (polling)"                   },
    /*  59 */ { "csr.addr.mtval.count",                           "count", "CSR write targeting mtval (0x343)",                                       "dpi->perf_csr_csr_addr_mtval (polling)"                    },
    /*  60 */ { "csr.addr.mvendorid.count",                       "count", "CSR write targeting mvendorid (0xF11, read-only)",                        "dpi->perf_csr_csr_addr_mvendorid (polling)"                },
    /*  61 */ { "csr.addr.marchid.count",                         "count", "CSR write targeting marchid (0xF12, read-only)",                          "dpi->perf_csr_csr_addr_marchid (polling)"                  },
    /*  62 */ { "ifetch.request.count",                           "count", "IFetch requests initiated (executionInfo.fire)",                          "dpi->perf_ifetch_request_fire (polling)"                   },
    /*  63 */ { "ifetch.lsu_req.fire.count",                      "count", "IFU→LSU ifetch request handshakes",                                       "dpi->perf_ifetch_lsu_req_fire (polling)"                   },
    /*  64 */ { "ifetch.axi_ar.fire.count",                       "count", "AXI AR channel fires attributed to IFetch",                               "dpi->perf_ifetch_axi_ar_fire (polling)"                    },
    /*  65 */ { "ifetch.axi_r.fire.count",                        "count", "AXI R channel fires attributed to IFetch",                                "dpi->perf_ifetch_axi_r_fire (polling)"                     },
    /*  66 */ { "ifetch.response.fire.count",                     "count", "LSU→IFU ifetch response handshakes",                                      "dpi->perf_ifetch_response_fire (polling)"                  },
    /*  67 */ { "ifetch.consumer_ready_at_response.count",        "count", "Consumer (ID) was ready when IFetch response arrived",                    "dpi->perf_ifetch_consumer_ready_at_response (polling)"     },
    /*  68 */ { "ifetch.response_consumed_first_cycle.count",     "count", "IFetch response consumed in 1st cycle after arrival",                     "dpi->perf_ifetch_response_consumed_first_cycle (polling)"  },
    /*  69 */ { "ifetch.phase.accept_pc.cycle",                   "cycle", "IFU idle, waiting for PC (s_idle)",                                       "dpi->perf_ifetch_phase_accept_pc (polling)"                },
    /*  70 */ { "ifetch.phase.prepare_request.cycle",             "cycle", "IFU internal preparation (s_waitData)",                                   "dpi->perf_ifetch_phase_prepare_request (polling)"          },
    /*  71 */ { "ifetch.phase.request_blocked.cycle",             "cycle", "IFU sending fetch request to LSU (s_sendFetchReq)",                       "dpi->perf_ifetch_phase_request_blocked (polling)"          },
    /*  72 */ { "ifetch.phase.wait_response.cycle",               "cycle", "IFU waiting for LSU response (s_waitResp)",                               "dpi->perf_ifetch_phase_wait_response (polling)"            },
    /*  73 */ { "ifetch.phase.response_buffered.cycle",           "cycle", "IFU data buffered, consumer ready (s_wait_nextStage_ready & ready)",      "dpi->perf_ifetch_phase_response_buffered (polling)"        },
    /*  74 */ { "ifetch.phase.output_blocked.cycle",              "cycle", "IFU data buffered, consumer blocked (s_wait_nextStage_ready & !ready)",   "dpi->perf_ifetch_phase_output_blocked (polling)"           },
    /*  75 */ { "lsu.load.byte.count",                            "count", "LSU load: byte (LB/LBU)",                                                 "dpi->perf_lsu_load_byte_fire (polling)"                    },
    /*  76 */ { "lsu.load.half.count",                            "count", "LSU load: halfword (LH/LHU)",                                             "dpi->perf_lsu_load_half_fire (polling)"                    },
    /*  77 */ { "lsu.load.word.count",                            "count", "LSU load: word (LW)",                                                     "dpi->perf_lsu_load_word_fire (polling)"                    },
    /*  78 */ { "lsu.load.aligned.count",                         "count", "LSU load: naturally aligned (single AXI transaction)",                    "dpi->perf_lsu_load_aligned_fire (polling)"                 },
    /*  79 */ { "lsu.load.unaligned.count",                       "count", "LSU load: unaligned word (byte-split)",                                   "dpi->perf_lsu_load_unaligned_fire (polling)"               },
    /*  80 */ { "lsu.store.byte.count",                           "count", "LSU store: byte (SB)",                                                    "dpi->perf_lsu_store_byte_fire (polling)"                   },
    /*  81 */ { "lsu.store.half.count",                           "count", "LSU store: halfword (SH)",                                                "dpi->perf_lsu_store_half_fire (polling)"                   },
    /*  82 */ { "lsu.store.word.count",                           "count", "LSU store: word (SW)",                                                    "dpi->perf_lsu_store_word_fire (polling)"                   },
    /*  83 */ { "lsu.store.aligned.count",                        "count", "LSU store: naturally aligned (single AXI transaction)",                   "dpi->perf_lsu_store_aligned_fire (polling)"                },
    /*  84 */ { "lsu.store.unaligned.count",                      "count", "LSU store: unaligned word (byte-split)",                                  "dpi->perf_lsu_store_unaligned_fire (polling)"              },
    /*  85 */ { "lsu.unaligned.extra_transaction.count",          "count", "Extra AXI transactions for unaligned word split",                         "dpi->perf_lsu_unaligned_extra_transaction (polling)"       },
    /*  86 */ { "lsu.axi.ar.fire.count",                          "count", "Total AXI AR channel handshakes (all transactions)",                      "dpi->perf_lsu_axi_ar_fire (polling)"                       },
    /*  87 */ { "lsu.axi.aw.fire.count",                          "count", "Total AXI AW channel handshakes (all transactions)",                      "dpi->perf_lsu_axi_aw_fire (polling)"                       },
    /*  88 */ { "lsu.axi.w.fire.count",                           "count", "Total AXI W channel handshakes (all transactions)",                       "dpi->perf_lsu_axi_w_fire (polling)"                        },
    /*  89 */ { "lsu.axi.r.fire.count",                           "count", "Total AXI R channel handshakes (all transactions)",                       "dpi->perf_lsu_axi_r_fire (polling)"                        },
    /*  90 */ { "lsu.axi.b.fire.count",                           "count", "Total AXI B channel handshakes (all transactions)",                       "dpi->perf_lsu_axi_b_fire (polling)"                        },
    /*  91 */ { "lsu.store.concurrent_ready_opportunity.cycle",   "cycle", "Cycles where AW+W channels were both ready but serialized",               "dpi->perf_lsu_concurrent_ready_opportunity (polling)"      },
    /*  92 */ { "lsu.store.aw_done_wait_w.cycle",                 "cycle", "Cycles LSU waited for W after AW completed",                              "dpi->perf_lsu_aw_done_wait_w (polling)"                    },
    /*  93 */ { "lsu.store.w_done_wait_aw.cycle",                 "cycle", "Cycles LSU waited for next AW after W completed (split)",                 "dpi->perf_lsu_w_done_wait_aw (polling)"                    },
    /*  94 */ { "alu.op.add.count",                               "count", "ALU ADD/ADDI/AUIPC/load/store/branch operations retired",                 "dpi->perf_ex_alu_op_add (polling)"                         },
    /*  95 */ { "alu.op.sub.count",                               "count", "ALU SUB operations retired",                                              "dpi->perf_ex_alu_op_sub (polling)"                         },
    /*  96 */ { "alu.op.sll.count",                               "count", "ALU SLL/SLLI operations retired",                                         "dpi->perf_ex_alu_op_sll (polling)"                         },
    /*  97 */ { "alu.op.srl.count",                               "count", "ALU SRL/SRLI operations retired",                                         "dpi->perf_ex_alu_op_srl (polling)"                         },
    /*  98 */ { "alu.op.sra.count",                               "count", "ALU SRA/SRAI operations retired",                                         "dpi->perf_ex_alu_op_sra (polling)"                         },
    /*  99 */ { "alu.op.and.count",                               "count", "ALU AND/ANDI operations retired",                                         "dpi->perf_ex_alu_op_and (polling)"                         },
    /* 100 */ { "alu.op.or.count",                                "count", "ALU OR/ORI operations retired",                                           "dpi->perf_ex_alu_op_or (polling)"                          },
    /* 101 */ { "alu.op.xor.count",                               "count", "ALU XOR/XORI operations retired",                                         "dpi->perf_ex_alu_op_xor (polling)"                         },
    /* 102 */ { "ex.adder.compute.count",                         "count", "Adder used for arithmetic compute result",                                "dpi->perf_ex_adder_compute (polling)"                      },
    /* 103 */ { "ex.adder.agen_ls.count",                         "count", "Adder used for load/store address generation",                            "dpi->perf_ex_adder_agen_ls (polling)"                      },
    /* 104 */ { "ex.adder.agen_branch.count",                     "count", "Adder used for branch/jump target computation (taken)",                   "dpi->perf_ex_adder_agen_branch (polling)"                  },
    /* 105 */ { "ex.adder.agen_auipc.count",                      "count", "Adder used for AUIPC pc+imm computation",                                 "dpi->perf_ex_adder_agen_auipc (polling)"                   },
    /* 106 */ { "ex.concurrency.alu_only.count",                  "count", "EX stage: only ALU output consumed (no PC target)",                       "dpi->perf_ex_concurrency_alu_only (polling)"               },
    /* 107 */ { "ex.concurrency.pc_only.count",                   "count", "EX stage: only PC target consumed (no ALU)",                              "dpi->perf_ex_concurrency_pc_only (polling)"                },
    /* 108 */ { "ex.concurrency.both.count",                      "count", "EX stage: both ALU and PC target consumed concurrently",                  "dpi->perf_ex_concurrency_both (polling)"                   },
    /* 109 */ { "icache.request.count",                           "count", "I-cache CPU-side request handshakes (cpuReq.fire)",                       "dpi->perf_icache_request_fire (polling)"                   },
    /* 110 */ { "icache.hit.count",                               "count", "I-cache tag hit (valid + tag match, single-cycle resp)",                  "dpi->perf_icache_hit (polling)"                            },
    /* 111 */ { "icache.miss.count",                              "count", "I-cache tag miss (not valid or tag mismatch, cacheable addr)",            "dpi->perf_icache_miss (polling)"                           },
    /* 112 */ { "icache.bypass.count",                            "count", "I-cache bypass (non-cacheable address, forwarded directly)",              "dpi->perf_icache_bypass (polling)"                         },
    /* 113 */ { "icache.lower_req.count",                         "count", "I-cache lower-memory request handshakes (lowerReq.fire)",                 "dpi->perf_icache_lower_req_fire (polling)"                 },
    /* 114 */ { "icache.lower_resp.count",                        "count", "I-cache lower-memory response handshakes (lowerResp.fire)",               "dpi->perf_icache_lower_resp_fire (polling)"                },
    /* 115 */ { "icache.refill.count",                            "count", "I-cache line refill (valid bit set on cacheable-miss OKAY resp)",         "dpi->perf_icache_refill_fire (polling)"                    },
    /* 116 */ { "icache.response.count",                          "count", "I-cache CPU-side response handshakes (cpuResp.fire)",                     "dpi->perf_icache_response_fire (polling)"                  },
    /* 117 */ { "icache.response_blocked.cycle",                  "cycle", "I-cache CPU response valid but downstream not ready",                     "dpi->perf_icache_response_blocked (polling)"               },
};

// Compile-time guard: table size must match counter count.
static_assert(
    sizeof(kCounterTable) / sizeof(kCounterTable[0]) == PerfCounters::kNumCounters,
    "kCounterTable size must equal kNumCounters"
);

// Compile-time guard: no duplicate counter names.
constexpr bool cstr_eq(const char *a, const char *b) noexcept {
    while (*a != '\0' && *b != '\0') {
        if (*a != *b)
            return false;
        ++a; ++b;
    }
    return *a == *b;
}

constexpr bool kCounterTable_has_unique_names() noexcept {
    for (size_t i = 0; i < PerfCounters::kNumCounters; i++) {
        for (size_t j = i + 1; j < PerfCounters::kNumCounters; j++) {
            if (cstr_eq(kCounterTable[i].name, kCounterTable[j].name))
                return false;
        }
    }
    return true;
}

static_assert(
    kCounterTable_has_unique_names(),
    "kCounterTable contains duplicate counter names — "
        "every counter name must be unique"
);

// ── Pass accumulated core signals to counter positions ──────────────────
void PerfCounters::accumulateCore(
    uint64_t running, uint64_t commitFire,
    uint64_t busy, uint64_t stall
) {
    m_values[Idx::CORE_CYCLE]       += running;
    m_values[Idx::CORE_INSTRET]     += commitFire;
    m_values[Idx::CORE_BUSY_CYCLE]  += busy;
    m_values[Idx::CORE_STALL_CYCLE] += stall;
}

void PerfCounters::accumulateInstClass(
    uint64_t alu, uint64_t load, uint64_t store,
    uint64_t branch, uint64_t jal, uint64_t jalr,
    uint64_t csr, uint64_t muldiv
) {
    m_values[Idx::INST_CLASS_ALU_COUNT]    += alu;
    m_values[Idx::INST_CLASS_LOAD_COUNT]   += load;
    m_values[Idx::INST_CLASS_STORE_COUNT]  += store;
    m_values[Idx::INST_CLASS_BRANCH_COUNT] += branch;
    m_values[Idx::INST_CLASS_JAL_COUNT]    += jal;
    m_values[Idx::INST_CLASS_JALR_COUNT]   += jalr;
    m_values[Idx::INST_CLASS_CSR_COUNT]    += csr;
    m_values[Idx::INST_CLASS_MULDIV_COUNT] += muldiv;
}

void PerfCounters::accumulateState(
    uint64_t fetch, uint64_t decode, uint64_t execute,
    uint64_t memory, uint64_t writeback
) {
    m_values[Idx::STATE_FETCH_CYCLE]     += fetch;
    m_values[Idx::STATE_DECODE_CYCLE]    += decode;
    m_values[Idx::STATE_EXECUTE_CYCLE]   += execute;
    m_values[Idx::STATE_MEMORY_CYCLE]    += memory;
    m_values[Idx::STATE_WRITEBACK_CYCLE] += writeback;
}

void PerfCounters::accumulateStall(
    uint64_t ifetchWaitResp, uint64_t memWaitResp,
    uint64_t memReqBlocked, uint64_t structSharedMem,
    uint64_t muldivBusy
) {
    m_values[Idx::STALL_IFETCH_WAIT_RESP_CYCLE]  += ifetchWaitResp;
    m_values[Idx::STALL_MEM_WAIT_RESP_CYCLE]     += memWaitResp;
    m_values[Idx::STALL_MEM_REQ_BLOCKED_CYCLE]   += memReqBlocked;
    m_values[Idx::STALL_STRUCT_SHARED_MEM_CYCLE] += structSharedMem;
    m_values[Idx::STALL_MULDIV_BUSY_CYCLE]       += muldivBusy;
}

void PerfCounters::accumulateMem(
    uint64_t loadReqFire, uint64_t storeReqFire,
    uint64_t mmioReqFire
) {
    m_values[Idx::MEM_LOAD_REQ_COUNT]  += loadReqFire;
    m_values[Idx::MEM_STORE_REQ_COUNT] += storeReqFire;
    m_values[Idx::MEM_MMIO_REQ_COUNT]  += mmioReqFire;
}

void PerfCounters::accumulateTrap(uint64_t exceptionFire) {
    m_values[Idx::TRAP_EXCEPTION_COUNT] += exceptionFire;
}

void PerfCounters::accumulateReg(
    uint64_t gprWbFire, uint64_t csrWbFire,
    uint64_t gprSrcAlu, uint64_t gprSrcDmem,
    uint64_t gprSrcImm, uint64_t gprSrcPcNext,
    uint64_t gprSrcBcu, uint64_t gprSrcCsr,
    uint64_t csrModeRw, uint64_t csrModeRs,
    uint64_t csrModeRc, uint64_t csrModeImm
) {
    m_values[Idx::REG_GPR_WRITE_COUNT]          += gprWbFire;
    m_values[Idx::REG_CSR_WRITE_COUNT]          += csrWbFire;
    m_values[Idx::REG_GPR_SRC_ALU_COUNT]        += gprSrcAlu;
    m_values[Idx::REG_GPR_SRC_DMEM_COUNT]       += gprSrcDmem;
    m_values[Idx::REG_GPR_SRC_IMM_COUNT]        += gprSrcImm;
    m_values[Idx::REG_GPR_SRC_PC_NEXT_COUNT]    += gprSrcPcNext;
    m_values[Idx::REG_GPR_SRC_BCU_COUNT]        += gprSrcBcu;
    m_values[Idx::REG_GPR_SRC_CSR_COUNT]        += gprSrcCsr;
    m_values[Idx::REG_CSR_MODE_RW_COUNT]        += csrModeRw;
    m_values[Idx::REG_CSR_MODE_RS_COUNT]        += csrModeRs;
    m_values[Idx::REG_CSR_MODE_RC_COUNT]        += csrModeRc;
    m_values[Idx::REG_CSR_MODE_IMM_COUNT]       += csrModeImm;
}

void PerfCounters::accumulateGprCsr(
    uint64_t gprReadRs1, uint64_t gprReadRs2,
    uint64_t gprReadBoth, uint64_t gprReadRs1X0,
    uint64_t gprReadRs2X0, uint64_t gprReadRs1EqRs2,
    uint64_t gprReadRs2Unused, uint64_t gprReadUpper16,
    uint64_t gprWriteSuppressedX0,
    uint64_t csrReadPort1, uint64_t csrReadPort2,
    uint64_t csrReadPort3, uint64_t csrReadConcurrent2,
    uint64_t csrReadConcurrent3,
    uint64_t csrWriteNormal, uint64_t csrWriteTrap,
    uint64_t csrWriteReturn,
    uint64_t csrAddrMstatus, uint64_t csrAddrMtvec,
    uint64_t csrAddrMepc, uint64_t csrAddrMcause,
    uint64_t csrAddrMtval, uint64_t csrAddrMvendorid,
    uint64_t csrAddrMarchid
) {
    m_values[Idx::GPR_READ_RS1_COUNT]            += gprReadRs1;
    m_values[Idx::GPR_READ_RS2_COUNT]            += gprReadRs2;
    m_values[Idx::GPR_READ_BOTH_COUNT]           += gprReadBoth;
    m_values[Idx::GPR_READ_RS1_X0_COUNT]         += gprReadRs1X0;
    m_values[Idx::GPR_READ_RS2_X0_COUNT]         += gprReadRs2X0;
    m_values[Idx::GPR_READ_RS1_EQ_RS2_COUNT]     += gprReadRs1EqRs2;
    m_values[Idx::GPR_READ_RS2_UNUSED_COUNT]     += gprReadRs2Unused;
    m_values[Idx::GPR_READ_UPPER16_COUNT]        += gprReadUpper16;
    m_values[Idx::GPR_WRITE_SUPPRESSED_X0_COUNT] += gprWriteSuppressedX0;
    m_values[Idx::CSR_READ_PORT1_ENABLE_COUNT]   += csrReadPort1;
    m_values[Idx::CSR_READ_PORT2_ENABLE_COUNT]   += csrReadPort2;
    m_values[Idx::CSR_READ_PORT3_ENABLE_COUNT]   += csrReadPort3;
    m_values[Idx::CSR_READ_CONCURRENT_2PORT]     += csrReadConcurrent2;
    m_values[Idx::CSR_READ_CONCURRENT_3PORT]     += csrReadConcurrent3;
    m_values[Idx::CSR_WRITE_NORMAL_COUNT]        += csrWriteNormal;
    m_values[Idx::CSR_WRITE_TRAP_COUNT]          += csrWriteTrap;
    m_values[Idx::CSR_WRITE_RETURN_COUNT]        += csrWriteReturn;
    m_values[Idx::CSR_ADDR_MSTATUS_COUNT]        += csrAddrMstatus;
    m_values[Idx::CSR_ADDR_MTVEC_COUNT]          += csrAddrMtvec;
    m_values[Idx::CSR_ADDR_MEPC_COUNT]           += csrAddrMepc;
    m_values[Idx::CSR_ADDR_MCAUSE_COUNT]         += csrAddrMcause;
    m_values[Idx::CSR_ADDR_MTVAL_COUNT]          += csrAddrMtval;
    m_values[Idx::CSR_ADDR_MVENDORID_COUNT]      += csrAddrMvendorid;
    m_values[Idx::CSR_ADDR_MARCHID_COUNT]        += csrAddrMarchid;
}

void PerfCounters::accumulateIfetchLsu(
    uint64_t ifetchReq, uint64_t ifetchLsuReqFire,
    uint64_t ifetchAxiArFire, uint64_t ifetchAxiRFire,
    uint64_t ifetchRespFire, uint64_t ifetchConsumerReadyAtResp,
    uint64_t ifetchRespConsumedFirstCycle,
    uint64_t ifetchPhaseAcceptPc, uint64_t ifetchPhasePrepareReq,
    uint64_t ifetchPhaseReqBlocked, uint64_t ifetchPhaseWaitResp,
    uint64_t ifetchPhaseRespBuffered, uint64_t ifetchPhaseOutputBlocked,
    uint64_t lsuLoadByte, uint64_t lsuLoadHalf,
    uint64_t lsuLoadWord, uint64_t lsuLoadAligned,
    uint64_t lsuLoadUnaligned, uint64_t lsuStoreByte,
    uint64_t lsuStoreHalf, uint64_t lsuStoreWord,
    uint64_t lsuStoreAligned, uint64_t lsuStoreUnaligned,
    uint64_t lsuUnalignedExtraTrans,
    uint64_t lsuAxiArFire, uint64_t lsuAxiAwFire,
    uint64_t lsuAxiWFire, uint64_t lsuAxiRFire,
    uint64_t lsuAxiBFire,
    uint64_t lsuConcurrentReadyOpp, uint64_t lsuAwDoneWaitW,
    uint64_t lsuWDoneWaitAw
) {
    m_values[Idx::IFETCH_REQUEST_COUNT]                         += ifetchReq;
    m_values[Idx::IFETCH_LSU_REQ_FIRE_COUNT]                    += ifetchLsuReqFire;
    m_values[Idx::IFETCH_AXI_AR_FIRE_COUNT]                     += ifetchAxiArFire;
    m_values[Idx::IFETCH_AXI_R_FIRE_COUNT]                      += ifetchAxiRFire;
    m_values[Idx::IFETCH_RESPONSE_FIRE_COUNT]                   += ifetchRespFire;
    m_values[Idx::IFETCH_CONSUMER_READY_AT_RESP_COUNT]          += ifetchConsumerReadyAtResp;
    m_values[Idx::IFETCH_RESPONSE_CONSUMED_FIRST_CYCLE_COUNT]   += ifetchRespConsumedFirstCycle;
    m_values[Idx::IFETCH_PHASE_ACCEPT_PC_CYCLE]                 += ifetchPhaseAcceptPc;
    m_values[Idx::IFETCH_PHASE_PREPARE_REQUEST_CYCLE]           += ifetchPhasePrepareReq;
    m_values[Idx::IFETCH_PHASE_REQUEST_BLOCKED_CYCLE]           += ifetchPhaseReqBlocked;
    m_values[Idx::IFETCH_PHASE_WAIT_RESPONSE_CYCLE]             += ifetchPhaseWaitResp;
    m_values[Idx::IFETCH_PHASE_RESPONSE_BUFFERED_CYCLE]         += ifetchPhaseRespBuffered;
    m_values[Idx::IFETCH_PHASE_OUTPUT_BLOCKED_CYCLE]            += ifetchPhaseOutputBlocked;
    m_values[Idx::LSU_LOAD_BYTE_COUNT]                          += lsuLoadByte;
    m_values[Idx::LSU_LOAD_HALF_COUNT]                          += lsuLoadHalf;
    m_values[Idx::LSU_LOAD_WORD_COUNT]                          += lsuLoadWord;
    m_values[Idx::LSU_LOAD_ALIGNED_COUNT]                       += lsuLoadAligned;
    m_values[Idx::LSU_LOAD_UNALIGNED_COUNT]                     += lsuLoadUnaligned;
    m_values[Idx::LSU_STORE_BYTE_COUNT]                         += lsuStoreByte;
    m_values[Idx::LSU_STORE_HALF_COUNT]                         += lsuStoreHalf;
    m_values[Idx::LSU_STORE_WORD_COUNT]                         += lsuStoreWord;
    m_values[Idx::LSU_STORE_ALIGNED_COUNT]                      += lsuStoreAligned;
    m_values[Idx::LSU_STORE_UNALIGNED_COUNT]                    += lsuStoreUnaligned;
    m_values[Idx::LSU_UNALIGNED_EXTRA_TRANSACTION_COUNT]        += lsuUnalignedExtraTrans;
    m_values[Idx::LSU_AXI_AR_FIRE_COUNT]                        += lsuAxiArFire;
    m_values[Idx::LSU_AXI_AW_FIRE_COUNT]                        += lsuAxiAwFire;
    m_values[Idx::LSU_AXI_W_FIRE_COUNT]                         += lsuAxiWFire;
    m_values[Idx::LSU_AXI_R_FIRE_COUNT]                         += lsuAxiRFire;
    m_values[Idx::LSU_AXI_B_FIRE_COUNT]                         += lsuAxiBFire;
    m_values[Idx::LSU_STORE_CONCURRENT_READY_OPPORTUNITY_CYCLE] += lsuConcurrentReadyOpp;
    m_values[Idx::LSU_STORE_AW_DONE_WAIT_W_CYCLE]               += lsuAwDoneWaitW;
    m_values[Idx::LSU_STORE_W_DONE_WAIT_AW_CYCLE]               += lsuWDoneWaitAw;
}

void PerfCounters::accumulateEx(
    uint64_t aluOpAdd, uint64_t aluOpSub,
    uint64_t aluOpSll, uint64_t aluOpSrl,
    uint64_t aluOpSra, uint64_t aluOpAnd,
    uint64_t aluOpOr,  uint64_t aluOpXor,
    uint64_t adderCompute, uint64_t adderAgenLs,
    uint64_t adderAgenBranch, uint64_t adderAgenAuipc,
    uint64_t concAluOnly, uint64_t concPcOnly,
    uint64_t concBoth
) {
    m_values[Idx::ALU_OP_ADD_COUNT]             += aluOpAdd;
    m_values[Idx::ALU_OP_SUB_COUNT]             += aluOpSub;
    m_values[Idx::ALU_OP_SLL_COUNT]             += aluOpSll;
    m_values[Idx::ALU_OP_SRL_COUNT]             += aluOpSrl;
    m_values[Idx::ALU_OP_SRA_COUNT]             += aluOpSra;
    m_values[Idx::ALU_OP_AND_COUNT]             += aluOpAnd;
    m_values[Idx::ALU_OP_OR_COUNT]              += aluOpOr;
    m_values[Idx::ALU_OP_XOR_COUNT]             += aluOpXor;
    m_values[Idx::EX_ADDER_COMPUTE_COUNT]       += adderCompute;
    m_values[Idx::EX_ADDER_AGEN_LS_COUNT]       += adderAgenLs;
    m_values[Idx::EX_ADDER_AGEN_BRANCH_COUNT]   += adderAgenBranch;
    m_values[Idx::EX_ADDER_AGEN_AUIPC_COUNT]    += adderAgenAuipc;
    m_values[Idx::EX_CONC_ALU_ONLY_COUNT]       += concAluOnly;
    m_values[Idx::EX_CONC_PC_ONLY_COUNT]        += concPcOnly;
    m_values[Idx::EX_CONC_BOTH_COUNT]           += concBoth;
}

void PerfCounters::accumulateIcache(
    uint64_t requestFire, uint64_t hit,
    uint64_t miss, uint64_t bypass,
    uint64_t lowerReqFire, uint64_t lowerRespFire,
    uint64_t refillFire, uint64_t responseFire,
    uint64_t responseBlocked
) {
    m_values[Idx::ICACHE_REQUEST_COUNT]           += requestFire;
    m_values[Idx::ICACHE_HIT_COUNT]               += hit;
    m_values[Idx::ICACHE_MISS_COUNT]              += miss;
    m_values[Idx::ICACHE_BYPASS_COUNT]            += bypass;
    m_values[Idx::ICACHE_LOWER_REQ_COUNT]         += lowerReqFire;
    m_values[Idx::ICACHE_LOWER_RESP_COUNT]        += lowerRespFire;
    m_values[Idx::ICACHE_REFILL_COUNT]            += refillFire;
    m_values[Idx::ICACHE_RESPONSE_COUNT]          += responseFire;
    m_values[Idx::ICACHE_RESPONSE_BLOCKED_CYCLE]  += responseBlocked;
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
// Ownership moved to SimulatorImpl (Task 6); this accessor resolves through
// the active simulator instance so all consumers (simulator lifecycle, TUI
// snapshot, dump paths) obtain the same monitor without owning it here.
PerfMonitor& getPerfMonitor() {
    return getActiveSimulator()->perfMonitor;
}

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

// ── Helper: percentage formatter ─────────────────────────────────────────
namespace {

inline double pct(uint64_t part, uint64_t whole) {
    return (whole > 0) ? 100.0 * static_cast<double>(part) / static_cast<double>(whole) : 0.0;
}

inline void printClosureCheck(
    std::ostream &os, const char *label,
    uint64_t actual, uint64_t expected,
    uint64_t slack = 1
) {
    int64_t diff = static_cast<int64_t>(actual) - static_cast<int64_t>(expected);
    bool ok = (diff >= 0) && (static_cast<uint64_t>(diff) <= slack);
    os << "    [closure] " << label << ": sum=" << actual
       << " expected=" << expected;
    if (!ok) {
        os << "  DELTA=" << diff << "  *** VIOLATION ***";
    } else if (diff != 0) {
        os << "  slack=+" << diff << "  OK";
    } else {
        os << "  OK";
    }
    os << '\n';
}

} // anonymous namespace

// ── Dump ─────────────────────────────────────────────────────────────────
void PerfMonitor::dumpSummary(std::ostream &os) const {
    auto cyc    = get(Idx::CORE_CYCLE);
    auto iret   = get(Idx::CORE_INSTRET);
    auto busyC  = get(Idx::CORE_BUSY_CYCLE);
    auto stallC = get(Idx::CORE_STALL_CYCLE);

    double cpi   = (iret > 0) ?
        static_cast<double>(cyc) / static_cast<double>(iret) :
        0.0;
    double ipc   = (cyc > 0) ?
        static_cast<double>(iret) / static_cast<double>(cyc) :
        0.0;
    double stallPct = (cyc > 0) ?
        100.0 * static_cast<double>(stallC) / static_cast<double>(cyc) :
        0.0;

    os << "─── Perf Counter Summary ───\n";
    os << "  cycles=" << cyc << "  instret=" << iret
       << "  CPI=" << std::fixed << std::setprecision(4) << cpi
       << "  IPC=" << std::fixed << std::setprecision(4) << ipc
       << "  Stall%=" << std::fixed << std::setprecision(1) << stallPct
       << "%\n\n";

    // ══════════════════════════════════════════════════════════════════════
    //  1. CPI Stage Decomposition
    // ══════════════════════════════════════════════════════════════════════
    auto fetchC = get(Idx::STATE_FETCH_CYCLE);
    auto decC   = get(Idx::STATE_DECODE_CYCLE);
    auto execC  = get(Idx::STATE_EXECUTE_CYCLE);
    auto memC   = get(Idx::STATE_MEMORY_CYCLE);
    auto wbC    = get(Idx::STATE_WRITEBACK_CYCLE);
    auto stateSum = fetchC + decC + execC + memC + wbC;

    os << "── 1. CPI Stage Decomposition ──\n";
    os << "  Stage     Cycles    %Busy    %Total\n";
    os << "  IF        " << std::setw(9) << fetchC
       << "  " << std::setw(6) << std::fixed << std::setprecision(1)
       << pct(fetchC, busyC) << "%"
       << "  " << std::setw(6) << pct(fetchC, cyc) << "%\n";
    os << "  ID        " << std::setw(9) << decC
       << "  " << std::setw(6) << pct(decC, busyC) << "%"
       << "  " << std::setw(6) << pct(decC, cyc) << "%\n";
    os << "  EX        " << std::setw(9) << execC
       << "  " << std::setw(6) << pct(execC, busyC) << "%"
       << "  " << std::setw(6) << pct(execC, cyc) << "%\n";
    os << "  MEM       " << std::setw(9) << memC
       << "  " << std::setw(6) << pct(memC, busyC) << "%"
       << "  " << std::setw(6) << pct(memC, cyc) << "%\n";
    os << "  WB        " << std::setw(9) << wbC
       << "  " << std::setw(6) << pct(wbC, busyC) << "%"
       << "  " << std::setw(6) << pct(wbC, cyc) << "%\n";
    os << "  stage-sum " << std::setw(7) << stateSum
       << "  (" << std::fixed << std::setprecision(1) << pct(stateSum, busyC)
       << "% of busy)\n";

    // Stall attribution
    os << "  Stall causes (% of total stalls):\n";
    auto ifWait   = get(Idx::STALL_IFETCH_WAIT_RESP_CYCLE);
    auto memWait  = get(Idx::STALL_MEM_WAIT_RESP_CYCLE);
    auto reqBlk   = get(Idx::STALL_MEM_REQ_BLOCKED_CYCLE);
    auto shared   = get(Idx::STALL_STRUCT_SHARED_MEM_CYCLE);
    auto muldivS  = get(Idx::STALL_MULDIV_BUSY_CYCLE);
    os << "    ifetch_wait=" << ifWait << " (" << pct(ifWait, stallC) << "%)\n";
    os << "    mem_wait=" << memWait << " (" << pct(memWait, stallC) << "%)\n";
    os << "    req_blocked=" << reqBlk << " (" << pct(reqBlk, stallC) << "%)\n";
    os << "    shared_mem=" << shared << " (" << pct(shared, stallC) << "%)\n";
    os << "    muldiv_busy=" << muldivS << " (" << pct(muldivS, stallC) << "%)\n";

    // Instruction class distribution
    auto aluI    = get(Idx::INST_CLASS_ALU_COUNT);
    auto loadI   = get(Idx::INST_CLASS_LOAD_COUNT);
    auto storeI  = get(Idx::INST_CLASS_STORE_COUNT);
    auto branchI = get(Idx::INST_CLASS_BRANCH_COUNT);
    auto jalI    = get(Idx::INST_CLASS_JAL_COUNT);
    auto jalrI   = get(Idx::INST_CLASS_JALR_COUNT);
    auto csrI    = get(Idx::INST_CLASS_CSR_COUNT);
    auto muldivI = get(Idx::INST_CLASS_MULDIV_COUNT);
    auto instSum = aluI + loadI + storeI + branchI + jalI + jalrI + csrI + muldivI;

    os << "  Instruction mix:\n";
    os << "    alu=" << aluI << " (" << pct(aluI, iret) << "%)"
       << "  load=" << loadI << " (" << pct(loadI, iret) << "%)"
       << "  store=" << storeI << " (" << pct(storeI, iret) << "%)\n";
    os << "    branch=" << branchI << " (" << pct(branchI, iret) << "%)"
       << "  jal=" << jalI << " (" << pct(jalI, iret) << "%)"
       << "  jalr=" << jalrI << " (" << pct(jalrI, iret) << "%)\n";
    os << "    csr=" << csrI << " (" << pct(csrI, iret) << "%)"
       << "  muldiv=" << muldivI << " (" << pct(muldivI, iret) << "%)\n";
    os << "    inst-class-sum=" << instSum << "  (instret=" << iret << ")\n";

    if (m_strict) {
        os << "  --- Strict Closure Checks ---\n";
        printClosureCheck(os, "inst-class sum == instret", instSum, iret, 1);
        printClosureCheck(os, "state sum == core.busy", stateSum, busyC, 5);
        // Instruction class closure: sum(inst.class.*) covers all retired instructions
        int64_t instDiff = static_cast<int64_t>(instSum) - static_cast<int64_t>(iret);
        if (instDiff < 0 || instDiff > 1) {
            os << "    *** inst-class closure VIOLATION: inst-class-sum="
               << instSum << " != instret=" << iret << " (delta=" << instDiff << ") ***\n";
        }
        // Stage cycle closure: sum(state.*) should approximate core.busy
        int64_t stateDiff = static_cast<int64_t>(stateSum) - static_cast<int64_t>(busyC);
        if (stateDiff < 0 || stateDiff > 5) {
            os << "    *** stage-cycle closure VIOLATION: stage-sum="
               << stateSum << " != core.busy=" << busyC << " (delta=" << stateDiff << ") ***\n";
        }
    }

    // ══════════════════════════════════════════════════════════════════════
    //  2. Per-Stage Phase Decomposition
    // ══════════════════════════════════════════════════════════════════════
    os << "\n── 2. Per-Stage Phase Decomposition ──\n";
    os << "  IFetch phases (partition of state.fetch.cycle=" << fetchC << "):\n";
    auto ifAcceptPc     = get(Idx::IFETCH_PHASE_ACCEPT_PC_CYCLE);
    auto ifPrepare      = get(Idx::IFETCH_PHASE_PREPARE_REQUEST_CYCLE);
    auto ifReqBlocked   = get(Idx::IFETCH_PHASE_REQUEST_BLOCKED_CYCLE);
    auto ifWaitResp     = get(Idx::IFETCH_PHASE_WAIT_RESPONSE_CYCLE);
    auto ifRespBuf      = get(Idx::IFETCH_PHASE_RESPONSE_BUFFERED_CYCLE);
    auto ifOutBlocked   = get(Idx::IFETCH_PHASE_OUTPUT_BLOCKED_CYCLE);
    auto ifPhaseSum     = ifPrepare + ifReqBlocked
                        + ifWaitResp + ifRespBuf + ifOutBlocked;
    os << "    accept_pc(idle)=" << ifAcceptPc << " (" << pct(ifAcceptPc, fetchC)
       << "%)\n";
    os << "    prepare_req=" << ifPrepare << " (" << pct(ifPrepare, fetchC)
       << "%)\n";
    os << "    req_blocked=" << ifReqBlocked << " (" << pct(ifReqBlocked, fetchC)
       << "%)\n";
    os << "    wait_resp=" << ifWaitResp << " (" << pct(ifWaitResp, fetchC)
       << "%)\n";
    os << "    resp_buffered=" << ifRespBuf << " (" << pct(ifRespBuf, fetchC)
       << "%)\n";
    os << "    output_blocked=" << ifOutBlocked << " (" << pct(ifOutBlocked, fetchC)
       << "%)\n";
    os << "    phase-sum=" << ifPhaseSum << "  (" << pct(ifPhaseSum, fetchC)
       << "% of state.fetch.cycle)\n";

    if (m_strict) {
        printClosureCheck(os, "IF phases == state.fetch.cycle", ifPhaseSum, fetchC, 2);
        int64_t ifDiff = static_cast<int64_t>(ifPhaseSum) - static_cast<int64_t>(fetchC);
        if (ifDiff < 0 || ifDiff > 2) {
            os << "    *** IFetch phase closure VIOLATION: phase-sum="
               << ifPhaseSum << " != state.fetch.cycle=" << fetchC
               << " (delta=" << ifDiff << ") ***\n";
        }
    }

    // IFetch transaction events
    auto ifReq    = get(Idx::IFETCH_REQUEST_COUNT);
    auto ifLsuReq = get(Idx::IFETCH_LSU_REQ_FIRE_COUNT);
    auto ifARFire = get(Idx::IFETCH_AXI_AR_FIRE_COUNT);
    auto ifRFire  = get(Idx::IFETCH_AXI_R_FIRE_COUNT);
    auto ifResp   = get(Idx::IFETCH_RESPONSE_FIRE_COUNT);
    auto ifConsReady = get(Idx::IFETCH_CONSUMER_READY_AT_RESP_COUNT);
    auto ifFirstCons = get(Idx::IFETCH_RESPONSE_CONSUMED_FIRST_CYCLE_COUNT);

    os << "  IFetch transactions:\n";
    os << "    requests=" << ifReq
       << "  lsu_req_fires=" << ifLsuReq
       << "  axi_ar_fires=" << ifARFire
       << "  axi_r_fires=" << ifRFire << "\n";
    os << "    response_fires=" << ifResp
       << "  consumer_ready_at_resp=" << ifConsReady
       << "  first_cycle_cons=" << ifFirstCons << "\n";

    if (m_strict) {
        printClosureCheck(os, "ifetch req == lsu_req", ifReq, ifLsuReq, 1);
        printClosureCheck(os, "ifetch lsu_req == axi_ar", ifLsuReq, ifARFire, 1);
        printClosureCheck(os, "ifetch axi_r == response", ifRFire, ifResp, 1);
    }

    // ══════════════════════════════════════════════════════════════════════
    //  3. Context (Register Writeback) Usage
    // ══════════════════════════════════════════════════════════════════════
    auto gprWb  = get(Idx::REG_GPR_WRITE_COUNT);
    auto csrWb  = get(Idx::REG_CSR_WRITE_COUNT);
    os << "\n── 3. Context (Register Writeback) Usage ──\n";
    os << "  Writeback events: gpr=" << gprWb << "  csr=" << csrWb
       << "  writes/inst=" << std::fixed << std::setprecision(3)
       << ((iret > 0) ?
            static_cast<double>(gprWb + csrWb) / static_cast<double>(iret) :
            0.0) << "\n";

    os << "  GPR writeback source decomposition:\n";
    auto gprAlu  = get(Idx::REG_GPR_SRC_ALU_COUNT);
    auto gprDmem = get(Idx::REG_GPR_SRC_DMEM_COUNT);
    auto gprImm  = get(Idx::REG_GPR_SRC_IMM_COUNT);
    auto gprPcn  = get(Idx::REG_GPR_SRC_PC_NEXT_COUNT);
    auto gprBcu  = get(Idx::REG_GPR_SRC_BCU_COUNT);
    auto gprCsr  = get(Idx::REG_GPR_SRC_CSR_COUNT);
    auto gprSrcSum = gprAlu + gprDmem + gprImm + gprPcn + gprBcu + gprCsr;
    os << "    alu=" << gprAlu << " (" << pct(gprAlu, gprWb) << "%)"
       << "  dmem=" << gprDmem << " (" << pct(gprDmem, gprWb) << "%)"
       << "  imm=" << gprImm << " (" << pct(gprImm, gprWb) << "%)\n";
    os << "    pc_next=" << gprPcn << " (" << pct(gprPcn, gprWb) << "%)"
       << "  bcu=" << gprBcu << " (" << pct(gprBcu, gprWb) << "%)"
       << "  csr=" << gprCsr << " (" << pct(gprCsr, gprWb) << "%)\n";
    os << "    gpr-src-sum=" << gprSrcSum << "  (gpr_wr=" << gprWb << ")\n";

    os << "  CSR write mode decomposition:\n";
    auto csrRw  = get(Idx::REG_CSR_MODE_RW_COUNT);
    auto csrRs  = get(Idx::REG_CSR_MODE_RS_COUNT);
    auto csrRc  = get(Idx::REG_CSR_MODE_RC_COUNT);
    auto csrImm = get(Idx::REG_CSR_MODE_IMM_COUNT);
    os << "    rw=" << csrRw << " (" << pct(csrRw, csrWb) << "%)"
       << "  rs=" << csrRs << " (" << pct(csrRs, csrWb) << "%)"
       << "  rc=" << csrRc << " (" << pct(csrRc, csrWb) << "%)"
       << "  imm=" << csrImm << " (" << pct(csrImm, csrWb) << "%)\n";

    if (m_strict) {
        printClosureCheck(os, "gpr src sum == gpr writes", gprSrcSum, gprWb, 1);
    }

    // ══════════════════════════════════════════════════════════════════════
    //  4. GPR Utilization
    // ══════════════════════════════════════════════════════════════════════
    os << "\n── 4. GPR Utilization ──\n";
    auto gprRs1    = get(Idx::GPR_READ_RS1_COUNT);
    auto gprRs2    = get(Idx::GPR_READ_RS2_COUNT);
    auto gprBoth   = get(Idx::GPR_READ_BOTH_COUNT);
    auto gprRs1X0  = get(Idx::GPR_READ_RS1_X0_COUNT);
    auto gprRs2X0  = get(Idx::GPR_READ_RS2_X0_COUNT);
    auto gprEq     = get(Idx::GPR_READ_RS1_EQ_RS2_COUNT);
    auto gprUnused = get(Idx::GPR_READ_RS2_UNUSED_COUNT);
    auto gprUp16   = get(Idx::GPR_READ_UPPER16_COUNT);
    auto gprSuppX0 = get(Idx::GPR_WRITE_SUPPRESSED_X0_COUNT);

    os << "  Read ports: rs1=" << gprRs1 << "  rs2=" << gprRs2
       << "  both=" << gprBoth << "\n";
    os << "  x0 usage: rs1_x0=" << gprRs1X0 << " (" << pct(gprRs1X0, gprRs1)
       << "% of rs1)"
       << "  rs2_x0=" << gprRs2X0 << " (" << pct(gprRs2X0, gprRs2)
       << "% of rs2)\n";
    os << "  rs1==rs2=" << gprEq << " (" << pct(gprEq, gprBoth)
       << "% of both-read)"
       << "  rs2_unused=" << gprUnused << " (" << pct(gprUnused, iret)
       << "% of instret)\n";
    os << "  upper16=" << gprUp16 << " (" << pct(gprUp16, gprRs1+gprRs2)
       << "% of all reads)"
       << "  wr_suppressed_x0=" << gprSuppX0
       << " (" << pct(gprSuppX0, gprWb+gprSuppX0) << "% of would-be writes)\n";

    if (m_strict) {
        bool gprOk = gprSuppX0 <= (gprWb + gprSuppX0);
        os << "    [closure] gpr.write.suppressed_x0 <= gpr.write.total: "
           << (gprOk ? "OK" : "*** VIOLATION ***") << '\n';
    }

    // ══════════════════════════════════════════════════════════════════════
    //  5. MEM Payload
    // ══════════════════════════════════════════════════════════════════════
    os << "\n── 5. MEM Payload ──\n";
    auto memLoad   = get(Idx::MEM_LOAD_REQ_COUNT);
    auto memStore  = get(Idx::MEM_STORE_REQ_COUNT);
    auto memMmio   = get(Idx::MEM_MMIO_REQ_COUNT);
    os << "  Requests: load=" << memLoad << "  store=" << memStore
       << "  mmio=" << memMmio << "\n";
    os << "  Mem-req/inst=" << std::fixed << std::setprecision(3)
       << ((iret > 0) ?
            static_cast<double>(memLoad + memStore) / static_cast<double>(iret) :
            0.0) << "\n";

    // ══════════════════════════════════════════════════════════════════════
    //  6. CSR Concurrency
    // ══════════════════════════════════════════════════════════════════════
    os << "\n── 6. CSR Concurrency ──\n";
    auto csrP1    = get(Idx::CSR_READ_PORT1_ENABLE_COUNT);
    auto csrP2    = get(Idx::CSR_READ_PORT2_ENABLE_COUNT);
    auto csrP3    = get(Idx::CSR_READ_PORT3_ENABLE_COUNT);
    auto csrC2    = get(Idx::CSR_READ_CONCURRENT_2PORT);
    auto csrC3    = get(Idx::CSR_READ_CONCURRENT_3PORT);
    auto csrWNorm = get(Idx::CSR_WRITE_NORMAL_COUNT);
    auto csrWTrap = get(Idx::CSR_WRITE_TRAP_COUNT);
    auto csrWRet  = get(Idx::CSR_WRITE_RETURN_COUNT);

    os << "  Read ports: port1=" << csrP1 << "  port2=" << csrP2
       << "  port3=" << csrP3 << "\n";
    os << "  Concurrent reads: 2port=" << csrC2 << " (" << pct(csrC2, iret)
       << "% of instret)"
       << "  3port=" << csrC3 << " (" << pct(csrC3, iret) << "% of instret)\n";
    os << "  Write classes: normal=" << csrWNorm
       << "  trap=" << csrWTrap << "  return=" << csrWRet << "\n";

    os << "  CSR address distribution:\n";
    os << "    mstatus=" << get(Idx::CSR_ADDR_MSTATUS_COUNT)
       << "  mtvec=" << get(Idx::CSR_ADDR_MTVEC_COUNT)
       << "  mepc=" << get(Idx::CSR_ADDR_MEPC_COUNT)
       << "  mcause=" << get(Idx::CSR_ADDR_MCAUSE_COUNT)
       << "  mtval=" << get(Idx::CSR_ADDR_MTVAL_COUNT) << "\n";
    os << "    mvendorid=" << get(Idx::CSR_ADDR_MVENDORID_COUNT)
       << "  marchid=" << get(Idx::CSR_ADDR_MARCHID_COUNT) << "\n";

    if (m_strict) {
        bool csrOk = csrC3 <= csrC2;
        os << "    [closure] concurrent_3port <= concurrent_2port: "
           << (csrOk ? "OK" : "*** VIOLATION ***") << '\n';
    }

    // ══════════════════════════════════════════════════════════════════════
    //  7. Arithmetic Concurrency
    // ══════════════════════════════════════════════════════════════════════
    os << "\n── 7. Arithmetic Concurrency ──\n";
    auto aluAdd = get(Idx::ALU_OP_ADD_COUNT);
    auto aluSub = get(Idx::ALU_OP_SUB_COUNT);
    auto aluSll = get(Idx::ALU_OP_SLL_COUNT);
    auto aluSrl = get(Idx::ALU_OP_SRL_COUNT);
    auto aluSra = get(Idx::ALU_OP_SRA_COUNT);
    auto aluAnd = get(Idx::ALU_OP_AND_COUNT);
    auto aluOr  = get(Idx::ALU_OP_OR_COUNT);
    auto aluXor = get(Idx::ALU_OP_XOR_COUNT);
    auto aluOpSum = aluAdd + aluSub + aluSll + aluSrl + aluSra + aluAnd +
        aluOr + aluXor;

    os << "  ALU op distribution:\n";
    os << "    add=" << aluAdd << " (" << pct(aluAdd, aluOpSum) << "%)"
       << "  sub=" << aluSub << " (" << pct(aluSub, aluOpSum) << "%)"
       << "  sll=" << aluSll << " (" << pct(aluSll, aluOpSum) << "%)"
       << "  srl=" << aluSrl << " (" << pct(aluSrl, aluOpSum) << "%)\n";
    os << "    sra=" << aluSra << " (" << pct(aluSra, aluOpSum) << "%)"
       << "  and=" << aluAnd << " (" << pct(aluAnd, aluOpSum) << "%)"
       << "  or=" << aluOr << " (" << pct(aluOr, aluOpSum) << "%)"
       << "  xor=" << aluXor << " (" << pct(aluXor, aluOpSum) << "%)\n";
    os << "    alu-op-sum=" << aluOpSum << "  (inst-class-alu=" << aluI
       << ")\n";

    auto addCompute = get(Idx::EX_ADDER_COMPUTE_COUNT);
    auto addAgenLs  = get(Idx::EX_ADDER_AGEN_LS_COUNT);
    auto addAgenBr  = get(Idx::EX_ADDER_AGEN_BRANCH_COUNT);
    auto addAgenAui = get(Idx::EX_ADDER_AGEN_AUIPC_COUNT);
    auto adderSum   = addCompute + addAgenLs + addAgenBr + addAgenAui;

    os << "  Adder intent:\n";
    os << "    compute=" << addCompute << " (" << pct(addCompute, adderSum)
       << "%)"
       << "  agen_ls=" << addAgenLs << " (" << pct(addAgenLs, adderSum) << "%)"
       << "  agen_branch=" << addAgenBr << " (" << pct(addAgenBr, adderSum)
       << "%)"
       << "  agen_auipc=" << addAgenAui << " (" << pct(addAgenAui, adderSum)
       << "%)\n";

    auto concAluOnly = get(Idx::EX_CONC_ALU_ONLY_COUNT);
    auto concPcOnly  = get(Idx::EX_CONC_PC_ONLY_COUNT);
    auto concBoth    = get(Idx::EX_CONC_BOTH_COUNT);
    auto concSum     = concAluOnly + concPcOnly + concBoth;

    os << "  Execution concurrency:\n";
    os << "    alu_only=" << concAluOnly << " (" << pct(concAluOnly, concSum) << "%)"
       << "  pc_only=" << concPcOnly << " (" << pct(concPcOnly, concSum) << "%)"
       << "  both=" << concBoth << " (" << pct(concBoth, concSum) << "%)\n";
    os << "    dual-adder-requirement-rate=" << std::fixed << std::setprecision(3)
       << ((concSum > 0) ? static_cast<double>(concBoth) / static_cast<double>(concSum) : 0.0)
       << "  single-adder-sufficient-rate=" << std::fixed << std::setprecision(3)
       << ((concSum > 0) ? static_cast<double>(concAluOnly + concPcOnly) / static_cast<double>(concSum) : 0.0) << "\n";

    if (m_strict) {
        printClosureCheck(os, "conc sum == instret", concSum, iret, 1);
    }

    // ══════════════════════════════════════════════════════════════════════
    //  8. IFetch Decomposition
    // ══════════════════════════════════════════════════════════════════════
    // (Already detailed in chapter 2 with phases. Here: efficiency summary.)
    os << "\n── 8. IFetch Decomposition ──\n";
    os << "  IFetch efficiency:\n";
    os << "    consumer-ready-at-response-rate=" << std::fixed << std::setprecision(3)
       << ((ifResp > 0) ?
            static_cast<double>(ifConsReady) / static_cast<double>(ifResp) :
            0.0) << "\n";
    os << "    first-cycle-consume-rate=" << std::fixed << std::setprecision(3)
       << ((ifResp > 0) ?
            static_cast<double>(ifFirstCons) / static_cast<double>(ifResp) :
            0.0) << "\n";
    os << "    wait-resp-fraction=" << std::fixed << std::setprecision(3)
       << ((fetchC > 0) ?
            static_cast<double>(ifWaitResp) / static_cast<double>(fetchC) :
            0.0) << "\n";
    os << "    output-blocked-fraction=" << std::fixed << std::setprecision(3)
       << ((fetchC > 0) ?
            static_cast<double>(ifOutBlocked) / static_cast<double>(fetchC) :
            0.0) << "\n";
    os << "    inst-per-fetch=" << std::fixed << std::setprecision(1)
       << ((ifResp > 0) ?
            static_cast<double>(iret) / static_cast<double>(ifResp) :
            0.0) << "\n";

    // ══════════════════════════════════════════════════════════════════════
    //  9. LSU Decomposition
    // ══════════════════════════════════════════════════════════════════════
    os << "\n── 9. LSU Decomposition ──\n";
    auto lsuLdByte = get(Idx::LSU_LOAD_BYTE_COUNT);
    auto lsuLdHalf = get(Idx::LSU_LOAD_HALF_COUNT);
    auto lsuLdWord = get(Idx::LSU_LOAD_WORD_COUNT);
    auto lsuLdAligned   = get(Idx::LSU_LOAD_ALIGNED_COUNT);
    auto lsuLdUnaligned = get(Idx::LSU_LOAD_UNALIGNED_COUNT);
    auto lsuLdSizeSum   = lsuLdByte + lsuLdHalf + lsuLdWord;

    os << "  Load decomposition:\n";
    os << "    byte=" << lsuLdByte << " (" << pct(lsuLdByte, lsuLdSizeSum)
       << "%)"
       << "  half=" << lsuLdHalf << " (" << pct(lsuLdHalf, lsuLdSizeSum)
       << "%)"
       << "  word=" << lsuLdWord << " (" << pct(lsuLdWord, lsuLdSizeSum)
       << "%)\n";
    os << "    aligned=" << lsuLdAligned << " (" << pct(lsuLdAligned, memLoad)
       << "% of mem.load)"
       << "  unaligned=" << lsuLdUnaligned << " (" << pct(lsuLdUnaligned, memLoad)
       << "%)\n";

    auto lsuStByte = get(Idx::LSU_STORE_BYTE_COUNT);
    auto lsuStHalf = get(Idx::LSU_STORE_HALF_COUNT);
    auto lsuStWord = get(Idx::LSU_STORE_WORD_COUNT);
    auto lsuStAligned   = get(Idx::LSU_STORE_ALIGNED_COUNT);
    auto lsuStUnaligned = get(Idx::LSU_STORE_UNALIGNED_COUNT);
    auto lsuStSizeSum   = lsuStByte + lsuStHalf + lsuStWord;

    os << "  Store decomposition:\n";
    os << "    byte=" << lsuStByte << " (" << pct(lsuStByte, lsuStSizeSum)
       << "%)"
       << "  half=" << lsuStHalf << " (" << pct(lsuStHalf, lsuStSizeSum)
       << "%)"
       << "  word=" << lsuStWord << " (" << pct(lsuStWord, lsuStSizeSum)
       << "%)\n";
    os << "    aligned=" << lsuStAligned << " (" << pct(lsuStAligned, memStore)
       << "% of mem.store)"
       << "  unaligned=" << lsuStUnaligned << " (" << pct(lsuStUnaligned, memStore)
       << "%)\n";

    auto lsuUnExtra = get(Idx::LSU_UNALIGNED_EXTRA_TRANSACTION_COUNT);
    os << "  Unaligned extra transactions: " << lsuUnExtra << "\n";

    os << "  AXI channel fires:\n";
    os << "    AR=" << get(Idx::LSU_AXI_AR_FIRE_COUNT)
       << "  AW=" << get(Idx::LSU_AXI_AW_FIRE_COUNT)
       << "  W=" << get(Idx::LSU_AXI_W_FIRE_COUNT)
       << "  R=" << get(Idx::LSU_AXI_R_FIRE_COUNT)
       << "  B=" << get(Idx::LSU_AXI_B_FIRE_COUNT) << "\n";

    auto lsuConcOpp = get(Idx::LSU_STORE_CONCURRENT_READY_OPPORTUNITY_CYCLE);
    auto lsuAwWaitW = get(Idx::LSU_STORE_AW_DONE_WAIT_W_CYCLE);
    auto lsuWWaitAw = get(Idx::LSU_STORE_W_DONE_WAIT_AW_CYCLE);

    os << "  Store serialization:\n";
    os << "    concurrent_ready_opportunity=" << lsuConcOpp
       << "  aw_done_wait_w=" << lsuAwWaitW
       << "  w_done_wait_aw=" << lsuWWaitAw << "\n";

    if (m_strict) {
        // Load closure: size sum should match aligned+unaligned (within aligned each is one transaction)
        auto lsuLdAlignSum = lsuLdAligned + lsuLdUnaligned;
        printClosureCheck(
            os, "lsu.load size-sum == aligned+unaligned",
            lsuLdAlignSum, lsuLdSizeSum, 1
        );
        auto lsuStAlignSum = lsuStAligned + lsuStUnaligned;
        printClosureCheck(
            os, "lsu.store size-sum == aligned+unaligned",
            lsuStAlignSum, lsuStSizeSum, 1
        );
        // AXI load/ifetch: total AR = ifetch_ar + lsu_ar
        auto totalAR = ifARFire + get(Idx::LSU_AXI_AR_FIRE_COUNT);
        os << "    [closure] total AXI AR = ifetch.axi_ar + lsu.axi_ar = "
           << totalAR << '\n';
    }

    // ══════════════════════════════════════════════════════════════════════
    // 10. I-cache Performance
    // ══════════════════════════════════════════════════════════════════════
    auto icReq       = get(Idx::ICACHE_REQUEST_COUNT);
    auto icHit       = get(Idx::ICACHE_HIT_COUNT);
    auto icMiss      = get(Idx::ICACHE_MISS_COUNT);
    auto icBypass    = get(Idx::ICACHE_BYPASS_COUNT);
    auto icLowerReq  = get(Idx::ICACHE_LOWER_REQ_COUNT);
    auto icLowerResp = get(Idx::ICACHE_LOWER_RESP_COUNT);
    auto icRefill    = get(Idx::ICACHE_REFILL_COUNT);
    auto icResp      = get(Idx::ICACHE_RESPONSE_COUNT);
    auto icRespBlk   = get(Idx::ICACHE_RESPONSE_BLOCKED_CYCLE);

    auto icReqClassSum = icHit + icMiss + icBypass;
    double icHitRate   = (icReq > 0) ?
        static_cast<double>(icHit) / static_cast<double>(icReq) : 0.0;
    double icMissRate  = (icReq > 0) ?
        static_cast<double>(icMiss) / static_cast<double>(icReq) : 0.0;
    double icBypassRate = (icReq > 0) ?
        static_cast<double>(icBypass) / static_cast<double>(icReq) : 0.0;

    os << "\n── 10. I-cache Performance ──\n";
    os << "  I-cache events:\n";
    os << "    requests=" << icReq
       << "  hit=" << icHit << " (" << std::fixed << std::setprecision(1)
       << (icHitRate * 100.0) << "%)"
       << "  miss=" << icMiss << " (" << (icMissRate * 100.0) << "%)"
       << "  bypass=" << icBypass << " (" << (icBypassRate * 100.0) << "%)\n";
    os << "    lower_req=" << icLowerReq
       << "  lower_resp=" << icLowerResp
       << "  refill=" << icRefill
       << "  response=" << icResp
       << "  response_blocked=" << icRespBlk << " cycles\n";

    os << "  AXI load reduction:\n";
    // With I-cache, lower_req ≪ icache.request (only miss + bypass generate lower requests)
    double icAxiReduction = (icReq > 0) ?
        100.0 * (1.0 - static_cast<double>(icLowerReq) / static_cast<double>(icReq)) :
        0.0;
    os << "    AXI load-req reduction: " << std::fixed << std::setprecision(1)
       << icAxiReduction << "%\n";
    os << "    lower_req/instruction=" << std::fixed << std::setprecision(4)
       << ((iret > 0) ? static_cast<double>(icLowerReq) / static_cast<double>(iret) : 0.0)
       << "\n";

    double icLowerReqRespRatio = (icLowerReq > 0) ?
        static_cast<double>(icLowerResp) / static_cast<double>(icLowerReq) :
        0.0;
    os << "    lower_resp/lower_req ratio=" << std::fixed << std::setprecision(2)
       << icLowerReqRespRatio << " (ideal=1.0 for single-outstanding)\n";

    double icMissWaitAvg = (icMiss > 0) ?
        static_cast<double>(icLowerResp) / static_cast<double>(icMiss + icBypass) :
        0.0;
    os << "    avg lower_resp per miss+bypass=" << std::fixed << std::setprecision(2)
       << icMissWaitAvg << " (ideal=1.0 for single-outstanding)\n";

    os << "    IFU wait after cache: "
       << std::fixed << std::setprecision(1)
       << pct(ifWait, fetchC) << "% of IF active\n";

    if (m_strict) {
        os << "  --- I-cache Closure Checks ---\n";
        // Request classification: every request is either hit, miss, or bypass
        printClosureCheck(
            os, "icache request == hit + miss + bypass",
            icReqClassSum, icReq, 1
        );
        // Response closure: each request eventually gets a response
        printClosureCheck(
            os, "icache response == request",
            icResp, icReq, 1
        );
        // Lower-memory request closure: only miss + bypass generate lower requests
        // lower_req should be <= request (and realistically <= miss + bypass)
        auto icLowerReqMax = icMiss + icBypass;
        if (icLowerReq > icLowerReqMax) {
            os << "    *** I-cache lower_req exceeds miss+bypass: lower_req="
               << icLowerReq << " > miss+bypass=" << icLowerReqMax
               << " (delta=" << (static_cast<int64_t>(icLowerReq) - static_cast<int64_t>(icLowerReqMax))
               << ") ***\n";
        } else {
            os << "    [closure] icache lower_req <= miss + bypass: lower_req="
               << icLowerReq << " <= miss+bypass=" << icLowerReqMax << "  OK\n";
        }
        // Refill closure: refills never exceed misses
        if (icRefill > icMiss) {
            os << "    *** I-cache refill exceeds miss: refill="
               << icRefill << " > miss=" << icMiss << " ***\n";
        } else {
            os << "    [closure] icache refill <= miss: refill="
               << icRefill << " <= miss=" << icMiss << "  OK\n";
        }
        // IFU↔ICache closure: IFetch AXI AR fires should roughly equal icache.lower_req fires
        // (the cache's lower requests are the sole source of IFetch AXI transactions)
        os << "    [closure] ifetch.axi_ar ≈ icache.lower_req: " << ifARFire
           << " vs " << icLowerReq;
        auto ifArVsLowerReq = static_cast<int64_t>(ifARFire) - static_cast<int64_t>(icLowerReq);
        os << " (delta=" << ifArVsLowerReq << ")";
        if (ifArVsLowerReq < -1 || ifArVsLowerReq > 1) {
            os << "  *** VIOLATION ***";
        } else {
            os << "  OK";
        }
        os << '\n';
    }

    // ══════════════════════════════════════════════════════════════════════
    // 11. Area–Performance Candidates
    // ══════════════════════════════════════════════════════════════════════
    os << "\n── 11. Area–Performance Candidates ──\n";
    // Derive from collected evidence only, no speculation.

    // Candidate 1: Dual-adder requirement
    double dualAdderRate = (concSum > 0) ?
        static_cast<double>(concBoth) / static_cast<double>(concSum) :
        0.0;
    os << "  ■ Dual-adder requirement rate: " << std::fixed << std::setprecision(1)
       << (dualAdderRate * 100.0) << "%\n";
    os << "    Cycles where both ALU and PC target were consumed concurrently.\n";
    os << "    If low (<5%), a single shared adder likely suffices for this workload.\n";

    // Candidate 2: IFetch bottleneck
    double ifWaitFrac = (fetchC > 0) ?
        static_cast<double>(ifWaitResp) / static_cast<double>(fetchC) :
        0.0;
    os << "  ■ IFetch wait-response fraction: " << std::fixed << std::setprecision(1)
       << (ifWaitFrac * 100.0) << "%\n";
    os << "    Fraction of IF active time spent waiting for memory response.\n";
    os << "    If high (>50%), consider prefetch buffers or wider fetch width.\n";

    // Candidate 3: Store serialization loss
    os << "  ■ Store serialization overhead:\n";
    os << "    concurrent_ready_opportunities=" << lsuConcOpp
        << " (cycles where AW+W were both ready)\n";
    os << "    aw_done_wait_w=" << lsuAwWaitW << "  w_done_wait_aw="
      << lsuWWaitAw << "\n";
    os << "    Total serialization wait: " << (lsuAwWaitW + lsuWWaitAw)
       << " cycles\n";

    // Candidate 4: GPR writeback efficiency
    double gprWrPerInst = (iret > 0) ?
        static_cast<double>(gprWb) / static_cast<double>(iret) :
        0.0;
    double gprSuppRate  = (gprWb + gprSuppX0 > 0) ?
        static_cast<double>(gprSuppX0) / static_cast<double>(gprWb + gprSuppX0) :
        0.0;
    os << "  ■ GPR writeback: " << std::fixed << std::setprecision(2)
       << gprWrPerInst << " writes/inst\n";
    os << "    x0-suppression-rate: " << std::fixed << std::setprecision(1)
       << (gprSuppRate * 100.0) << "%\n";
    os << "    If x0-suppression is high, register file write port pressure is lower than nominal.\n";

    // Candidate 5: CSR access frequency
    double csrRate = (iret > 0) ?
        static_cast<double>(csrI) / static_cast<double>(iret) :
        0.0;
    os << "  ■ CSR instruction rate: " << std::fixed << std::setprecision(2)
       << (csrRate * 100.0) << "%\n";
    os << "    Port-3 concurrent reads occur in " << std::fixed
       << std::setprecision(2)
       << pct(csrC3, iret) << "% of cycles.\n";

    // Candidate 6: Instruction mix profile
    os << "  ■ Instruction mix summary:\n";
    os << "    compute=" << pct(aluI, iret) << "%  load=" << pct(loadI, iret)
       << "%  store=" << pct(storeI, iret) << "%  branch=" << pct(branchI, iret)
       << "%\n";
    os << "    jump=" << pct(jalI + jalrI, iret) << "%  csr=" << pct(csrI, iret)
       << "%  muldiv=" << pct(muldivI, iret) << "%\n";

    // Candidate 7: CPI breakdown by origin
    os << "  ■ CPI floor analysis:\n";
    os << "    Ideal CPI (1 issue/cycle, no stalls) = 1.00\n";
    os << "    Actual CPI = " << std::fixed << std::setprecision(4) << cpi
       << "\n";
    os << "    Stall contribution = " << std::fixed << std::setprecision(4)
       << (cpi - 1.0)
       << " CPI (above ideal)\n";
    os << "    Memory wait contribution = " << std::fixed << std::setprecision(4)
       << ((iret > 0) ?
            static_cast<double>(memWait) / static_cast<double>(iret) :
            0.0)
       << " CPI\n";
    os << "    IFetch wait contribution = " << std::fixed << std::setprecision(4)
       << ((iret > 0) ?
            static_cast<double>(ifWait) / static_cast<double>(iret) :
            0.0)
       << " CPI\n";

    os << std::flush;
}

bool PerfMonitor::dumpJson(const std::string &path) const {
    try {
        namespace fs = std::filesystem;
        fs::path p(path);
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        if (ec) return false;

        std::ofstream ofs(path);
        if (!ofs.is_open()) return false;

        auto cyc  = get(Idx::CORE_CYCLE);
        auto iret = get(Idx::CORE_INSTRET);
        double ipc = (cyc > 0) ?
            static_cast<double>(iret) / static_cast<double>(cyc) :
            0.0;

        std::ostringstream json;
        json << std::fixed << std::setprecision(4);
        json << "{";
        json << "\"schema_version\":1";
        json << ",\"cycles\":" << cyc;
        json << ",\"instret\":" << iret;
        json << ",\"ipc\":" << ipc;
        json << ",\"perf_counters\":[";
        for (size_t i = 0; i < kNumCounters; i++) {
            if (i > 0) json << ",";
            const auto &def = counterDef(i);
            json << "{\"name\":\"" << def.name << "\""
                 << ",\"unit\":\"" << def.unit << "\""
                 << ",\"value\":" << get(i) << "}";
        }
        json << "]";
        json << "}\n";

        ofs << json.str();
        ofs.close();
        return ofs.good();
    } catch (...) {
        return false;
    }
}

} // namespace perf
